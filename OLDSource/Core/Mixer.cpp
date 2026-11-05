#include "Mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "DelayLine.h"
#include "Fader.h"
#include "Graph.h"
#include "Pdc.h"

namespace lemon::core
{
namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};
};

// RBJ cookbook. kind: 0 low shelf, 1 peak, 2 high shelf.
void designBand(int kind, double sr, double freq, double gainDb, Biquad& q)
{
    freq = std::min(std::max(freq, 20.0), sr * 0.45);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freq / sr;
    const double cs = std::cos(w0), sn = std::sin(w0);
    double b0, b1, b2, a0, a1, a2;

    if (kind == 1)
    {
        const double alpha = sn / (2.0 * 0.9);
        b0 = 1 + alpha * A;  b1 = -2 * cs;  b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;  a1 = -2 * cs;  a2 = 1 - alpha / A;
    }
    else
    {
        const double alpha = sn / 2.0 * std::sqrt(2.0);                 // shelf slope S = 1
        const double beta = 2.0 * std::sqrt(A) * alpha;
        if (kind == 0)
        {
            b0 = A * ((A + 1) - (A - 1) * cs + beta);
            b1 = 2 * A * ((A - 1) - (A + 1) * cs);
            b2 = A * ((A + 1) - (A - 1) * cs - beta);
            a0 = (A + 1) + (A - 1) * cs + beta;
            a1 = -2 * ((A - 1) + (A + 1) * cs);
            a2 = (A + 1) + (A - 1) * cs - beta;
        }
        else
        {
            b0 = A * ((A + 1) + (A - 1) * cs + beta);
            b1 = -2 * A * ((A - 1) + (A + 1) * cs);
            b2 = A * ((A + 1) + (A - 1) * cs - beta);
            a0 = (A + 1) - (A - 1) * cs + beta;
            a1 = 2 * ((A - 1) - (A + 1) * cs);
            a2 = (A + 1) - (A - 1) * cs - beta;
        }
    }
    q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
}

// State that must survive snapshot swaps (so rebuilding the topology never clicks the faders or EQs).
struct TrackPersist
{
    bool primed = false;
    float gl = 0.0f, gr = 0.0f;
    Biquad eq[kEqBands];
    bool eqActive[kEqBands] {};
    float eqG[kEqBands] {}, eqF[kEqBands] {};
};

struct SlotRt
{
    std::vector<float> dryL, dryR, keyL, keyR;
    StereoDelay dryPad, keyPad;
    int latency = 0;
    float mixCur = 1.0f;
};

struct SendRt
{
    StereoDelay pad;
    float gainCur = 0.0f;
};

struct TrackRt
{
    std::vector<float> inL, inR, postL, postR;
    std::vector<SlotRt> slots;
    std::vector<SendRt> sends;
    StereoDelay masterPad, extPad, outPad;
};

struct Snapshot
{
    MixerCfg cfg;
    std::vector<int> order;
    PdcPlan plan;
    std::vector<std::vector<int>> slotLat;
    std::vector<TrackRt> tracks;
    std::vector<float> scratchL, scratchR;
    int maxBlock = 512;
};

inline void zero(float* p, int n) noexcept { std::memset(p, 0, sizeof(float) * static_cast<std::size_t>(n)); }
}

struct MixerEngine::Impl
{
    explicit Impl(std::array<LiveTrack, kMaxTracks>& l) : live(l) {}

    std::array<LiveTrack, kMaxTracks>& live;
    std::array<TrackPersist, kMaxTracks> persist;

    double sr = 48000.0;
    int maxBlock = 512;

    std::atomic<Snapshot*> active { nullptr };
    std::atomic<bool> busy { false };
    std::vector<std::unique_ptr<Snapshot>> retired;
    std::atomic<int> latency { 0 };
    MixerCfg lastCfg;
    bool haveCfg = false;

    ~Impl()
    {
        delete active.exchange(nullptr);
    }

    // ------------------------------------------------------------------ build (message thread)
    std::unique_ptr<Snapshot> build(const MixerCfg& cfg, std::string* err) const
    {
        auto fail = [&](const char* m) { if (err) *err = m; return std::unique_ptr<Snapshot>(); };

        const int n = static_cast<int>(cfg.tracks.size());
        if (n < 1 || n > kMaxTracks)
            return fail("Invalid track count");

        for (int t = 0; t < n; ++t)
        {
            const auto& tr = cfg.tracks[static_cast<std::size_t>(t)];
            if (tr.slots.size() > static_cast<std::size_t>(kMaxSlots)) return fail("Too many slots");
            if (tr.sends.size() > static_cast<std::size_t>(kMaxSends)) return fail("Too many sends");
            if (tr.hostOut < -1 || tr.hostOut >= kMaxHostOut) return fail("Bad host output");
            for (const auto& s : tr.sends)
                if (s.dest < 1 || s.dest >= n || s.dest == t) return fail("Bad send destination");
            for (const auto& sl : tr.slots)
                if (sl.keySource < -1 || sl.keySource >= n || sl.keySource == t || (sl.keySource == 0 && sl.effect))
                    return fail("Bad sidechain source");
        }
        for (int b : cfg.hostInputTrack)
            if (b < -1) return fail("Bad host input mapping");   // >= n is simply unused

        std::vector<int> order;
        if (! topologicalOrder(n, collectEdges(cfg), order))
            return fail("That routing would create a feedback loop");

        auto s = std::make_unique<Snapshot>();
        s->cfg = cfg;
        s->order = order;
        s->maxBlock = maxBlock;

        s->slotLat.resize(static_cast<std::size_t>(n));
        for (int t = 0; t < n; ++t)
            for (const auto& sl : cfg.tracks[static_cast<std::size_t>(t)].slots)
                s->slotLat[static_cast<std::size_t>(t)].push_back(sl.effect ? std::max(0, sl.effect->latency()) : 0);

        s->plan = computePdc(cfg, s->slotLat, order);

        const auto mb = static_cast<std::size_t>(maxBlock);
        s->scratchL.assign(mb, 0.0f);
        s->scratchR.assign(mb, 0.0f);
        s->tracks.resize(static_cast<std::size_t>(n));

        for (int t = 0; t < n; ++t)
        {
            const auto ti = static_cast<std::size_t>(t);
            const auto& tc = cfg.tracks[ti];
            auto& rt = s->tracks[ti];
            rt.inL.assign(mb, 0.0f);   rt.inR.assign(mb, 0.0f);
            rt.postL.assign(mb, 0.0f); rt.postR.assign(mb, 0.0f);

            rt.slots.resize(tc.slots.size());
            for (std::size_t k = 0; k < tc.slots.size(); ++k)
            {
                auto& sr_ = rt.slots[k];
                sr_.latency = s->slotLat[ti][k];
                sr_.dryPad.setLength(sr_.latency);
                sr_.keyPad.setLength(s->plan.keyPad[ti][k]);
                sr_.dryL.assign(mb, 0.0f); sr_.dryR.assign(mb, 0.0f);
                sr_.keyL.assign(mb, 0.0f); sr_.keyR.assign(mb, 0.0f);
                sr_.mixCur = live[ti].slotMix[k].load();
            }
            rt.sends.resize(tc.sends.size());
            for (std::size_t k = 0; k < tc.sends.size(); ++k)
            {
                rt.sends[k].pad.setLength(s->plan.sendPad[ti][k]);
                rt.sends[k].gainCur = fader::positionToGain(live[ti].sendLevel[k].load());
            }
            rt.masterPad.setLength(s->plan.masterPad[ti]);
            rt.extPad.setLength(s->plan.extPad[ti]);
            rt.outPad.setLength(s->plan.outPad[ti]);
        }
        return s;
    }

    // ------------------------------------------------------------------ audio thread
    static void addScaled(const float* sl, const float* sr_, float g0, float g1, StereoDelay& pad,
                          float* dl, float* dr, float* tmpL, float* tmpR, int c) noexcept
    {
        const float dg = (g1 - g0) / static_cast<float>(c);
        if (pad.length() > 0)
        {
            float g = g0;
            for (int i = 0; i < c; ++i) { tmpL[i] = sl[i] * g; tmpR[i] = sr_[i] * g; g += dg; }
            pad.process(tmpL, tmpR, c);
            for (int i = 0; i < c; ++i) { dl[i] += tmpL[i]; dr[i] += tmpR[i]; }
        }
        else
        {
            float g = g0;
            for (int i = 0; i < c; ++i) { dl[i] += sl[i] * g; dr[i] += sr_[i] * g; g += dg; }
        }
    }

    void runEq(TrackPersist& P, const LiveTrack& lv, float* l, float* r, int c) noexcept
    {
        for (int b = 0; b < kEqBands; ++b)
        {
            const auto bi = static_cast<std::size_t>(b);
            const float g = lv.eqGainDb[bi].load(std::memory_order_relaxed);
            const float f = lv.eqFreq[bi].load(std::memory_order_relaxed);
            auto& q = P.eq[b];
            if (std::fabs(g) < 0.01f)
            {
                if (P.eqActive[b]) { q.z1[0] = q.z1[1] = q.z2[0] = q.z2[1] = 0.0; P.eqActive[b] = false; }
                continue;
            }
            if (! P.eqActive[b] || g != P.eqG[b] || f != P.eqF[b])
            {
                designBand(b, sr, f, g, q);
                P.eqG[b] = g; P.eqF[b] = f; P.eqActive[b] = true;
            }
            for (int i = 0; i < c; ++i)
            {
                const double xl = l[i], xr = r[i];
                const double yl = q.b0 * xl + q.z1[0];
                q.z1[0] = q.b1 * xl - q.a1 * yl + q.z2[0];
                q.z2[0] = q.b2 * xl - q.a2 * yl;
                const double yr = q.b0 * xr + q.z1[1];
                q.z1[1] = q.b1 * xr - q.a1 * yr + q.z2[1];
                q.z2[1] = q.b2 * xr - q.a2 * yr;
                l[i] = static_cast<float>(yl);
                r[i] = static_cast<float>(yr);
            }
        }
    }

    void processChunk(Snapshot& S, const IO& io, int off, int c) noexcept
    {
        const int n = static_cast<int>(S.cfg.tracks.size());
        float* scL = S.scratchL.data();
        float* scR = S.scratchR.data();

        bool anySolo = false;
        for (int t = 1; t < n; ++t)
            anySolo = anySolo || live[static_cast<std::size_t>(t)].solo.load(std::memory_order_relaxed);

        for (int t = 0; t < n; ++t)
        {
            zero(S.tracks[static_cast<std::size_t>(t)].inL.data(), c);
            zero(S.tracks[static_cast<std::size_t>(t)].inR.data(), c);
        }
        for (int k = 0; k < kMaxHostOut; ++k)
            if (io.auxOutL[k] && io.auxOutR[k])
            {
                zero(io.auxOutL[k] + off, c);
                zero(io.auxOutR[k] + off, c);
            }

        // External audio (host buses and direct feeds), delay-aligned to the track's arrival time.
        for (int t = 0; t < n; ++t)
        {
            bool has = false;
            auto feed = [&](const float* l, const float* r)
            {
                if (! l) return;
                if (! r) r = l;
                if (! has) { zero(scL, c); zero(scR, c); has = true; }
                for (int i = 0; i < c; ++i) { scL[i] += l[off + i]; scR[i] += r[off + i]; }
            };
            for (int b = 0; b < kMaxHostIn; ++b)
                if (S.cfg.hostInputTrack[static_cast<std::size_t>(b)] == t)
                    feed(io.inL[b], io.inR[b]);
            feed(io.trackL[t], io.trackR[t]);

            if (has)
            {
                auto& rt = S.tracks[static_cast<std::size_t>(t)];
                rt.extPad.process(scL, scR, c);
                for (int i = 0; i < c; ++i) { rt.inL[static_cast<std::size_t>(i)] += scL[i]; rt.inR[static_cast<std::size_t>(i)] += scR[i]; }
            }
        }

        for (int t : S.order)
        {
            const auto ti = static_cast<std::size_t>(t);
            auto& rt = S.tracks[ti];
            const auto& tc = S.cfg.tracks[ti];
            auto& lv = live[ti];
            auto& P = persist[ti];
            float* l = rt.inL.data();
            float* r = rt.inR.data();

            // ---- insert slots ----
            for (std::size_t s = 0; s < tc.slots.size(); ++s)
            {
                const auto& sc = tc.slots[s];
                if (! sc.effect)
                    continue;
                auto& sr_ = rt.slots[s];
                const bool hasLat = sr_.latency > 0;

                if (! lv.slotEnabled[s].load(std::memory_order_relaxed))
                {
                    if (hasLat) sr_.dryPad.process(l, r, c);      // bypassed but still time-aligned
                    sr_.mixCur = 1.0f;
                    continue;
                }

                const float mixTarget = std::min(1.0f, std::max(0.0f, lv.slotMix[s].load(std::memory_order_relaxed)));
                const bool needDry = hasLat || mixTarget < 0.9999f || sr_.mixCur < 0.9999f;
                if (needDry)
                {
                    std::memcpy(sr_.dryL.data(), l, sizeof(float) * static_cast<std::size_t>(c));
                    std::memcpy(sr_.dryR.data(), r, sizeof(float) * static_cast<std::size_t>(c));
                    if (hasLat) sr_.dryPad.process(sr_.dryL.data(), sr_.dryR.data(), c);
                }

                const float* kl = nullptr;
                const float* kr = nullptr;
                if (sc.keySource > 0 && sc.effect->wantsKey())
                {
                    auto& src = S.tracks[static_cast<std::size_t>(sc.keySource)];
                    kl = src.inL.data();
                    kr = src.inR.data();
                    if (sr_.keyPad.length() > 0)
                    {
                        std::memcpy(sr_.keyL.data(), kl, sizeof(float) * static_cast<std::size_t>(c));
                        std::memcpy(sr_.keyR.data(), kr, sizeof(float) * static_cast<std::size_t>(c));
                        sr_.keyPad.process(sr_.keyL.data(), sr_.keyR.data(), c);
                        kl = sr_.keyL.data();
                        kr = sr_.keyR.data();
                    }
                }

                sc.effect->process(l, r, kl, kr, c);

                if (needDry && (mixTarget < 0.9999f || sr_.mixCur < 0.9999f))
                {
                    float m = sr_.mixCur;
                    const float dm = (mixTarget - m) / static_cast<float>(c);
                    for (int i = 0; i < c; ++i)
                    {
                        const auto ii = static_cast<std::size_t>(i);
                        l[i] = sr_.dryL[ii] + (l[i] - sr_.dryL[ii]) * m;
                        r[i] = sr_.dryR[ii] + (r[i] - sr_.dryR[ii]) * m;
                        m += dm;
                    }
                }
                sr_.mixCur = mixTarget;
            }

            // ---- EQ ----
            runEq(P, lv, l, r, c);

            // ---- pre-fader sends ----
            for (std::size_t k = 0; k < tc.sends.size(); ++k)
            {
                if (! tc.sends[k].preFader) continue;
                auto& snd = rt.sends[k];
                auto& dst = S.tracks[static_cast<std::size_t>(tc.sends[k].dest)];
                const float g1 = fader::positionToGain(lv.sendLevel[k].load(std::memory_order_relaxed));
                addScaled(l, r, snd.gainCur, g1, snd.pad, dst.inL.data(), dst.inR.data(), scL, scR, c);
                snd.gainCur = g1;
            }

            // ---- fader / balance / width / polarity / swap ----
            const bool audible = ! lv.mute.load(std::memory_order_relaxed)
                                && (t == 0 || ! anySolo || lv.solo.load(std::memory_order_relaxed));
            const float vol = audible ? fader::positionToGain(lv.volume.load(std::memory_order_relaxed)) : 0.0f;
            float bl, br;
            fader::balance(lv.pan.load(std::memory_order_relaxed), bl, br);
            const float sign = lv.polarity.load(std::memory_order_relaxed) ? -1.0f : 1.0f;
            const float tgl = vol * bl * sign, tgr = vol * br * sign;
            if (! P.primed) { P.gl = tgl; P.gr = tgr; P.primed = true; }
            const float dgl = (tgl - P.gl) / static_cast<float>(c);
            const float dgr = (tgr - P.gr) / static_cast<float>(c);
            const bool doSwap = lv.swap.load(std::memory_order_relaxed);
            const float width = lv.width.load(std::memory_order_relaxed);
            const bool doWidth = std::fabs(width - 1.0f) > 0.001f;

            float* pl = rt.postL.data();
            float* pr = rt.postR.data();
            float gl = P.gl, gr = P.gr, peakL = 0.0f, peakR = 0.0f;
            for (int i = 0; i < c; ++i)
            {
                float a = l[i], b = r[i];
                if (doSwap) std::swap(a, b);
                if (doWidth)
                {
                    const float m = 0.5f * (a + b), sd = 0.5f * (a - b) * width;
                    a = m + sd; b = m - sd;
                }
                a *= gl; b *= gr;
                pl[i] = a; pr[i] = b;
                gl += dgl; gr += dgr;
                peakL = std::max(peakL, std::fabs(a));
                peakR = std::max(peakR, std::fabs(b));
            }
            P.gl = tgl; P.gr = tgr;
            if (peakL > lv.peakL.load(std::memory_order_relaxed)) lv.peakL.store(peakL, std::memory_order_relaxed);
            if (peakR > lv.peakR.load(std::memory_order_relaxed)) lv.peakR.store(peakR, std::memory_order_relaxed);

            // ---- routing of the post-fader signal ----
            if (t == 0)
            {
                if (io.outL) std::memcpy(io.outL + off, pl, sizeof(float) * static_cast<std::size_t>(c));
                if (io.outR) std::memcpy(io.outR + off, pr, sizeof(float) * static_cast<std::size_t>(c));
            }
            else if (tc.toMaster)
            {
                auto& m = S.tracks[0];
                addScaled(pl, pr, 1.0f, 1.0f, rt.masterPad, m.inL.data(), m.inR.data(), scL, scR, c);
            }

            for (std::size_t k = 0; k < tc.sends.size(); ++k)
            {
                if (tc.sends[k].preFader) continue;
                auto& snd = rt.sends[k];
                auto& dst = S.tracks[static_cast<std::size_t>(tc.sends[k].dest)];
                const float g1 = fader::positionToGain(lv.sendLevel[k].load(std::memory_order_relaxed));
                addScaled(pl, pr, snd.gainCur, g1, snd.pad, dst.inL.data(), dst.inR.data(), scL, scR, c);
                snd.gainCur = g1;
            }

            if (t > 0 && tc.hostOut >= 0 && io.auxOutL[tc.hostOut] && io.auxOutR[tc.hostOut])
                addScaled(pl, pr, 1.0f, 1.0f, rt.outPad, io.auxOutL[tc.hostOut] + off, io.auxOutR[tc.hostOut] + off, scL, scR, c);
        }
    }
};

// ====================================================================== public API
MixerEngine::MixerEngine() : impl_(std::make_unique<Impl>(live_)) {}
MixerEngine::~MixerEngine() = default;

void MixerEngine::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlock_ = std::max(16, maxBlockSize);
    impl_->sr = sampleRate_;
    impl_->maxBlock = maxBlock_;
    for (auto& p : impl_->persist)
    {
        p.primed = false;
        for (int b = 0; b < kEqBands; ++b) { p.eqActive[b] = false; p.eq[b] = Biquad {}; }
    }
    if (impl_->haveCfg)
        publish(impl_->lastCfg);
}

bool MixerEngine::publish(const MixerCfg& cfg, std::string* error)
{
    auto snap = impl_->build(cfg, error);
    if (! snap)
        return false;
    impl_->lastCfg = cfg;
    impl_->haveCfg = true;
    impl_->latency.store(snap->plan.total);
    if (auto* old = impl_->active.exchange(snap.release()))
        impl_->retired.emplace_back(old);
    collectGarbage();
    return true;
}

void MixerEngine::collectGarbage()
{
    // seq_cst on both sides: if we see busy == false *after* the swap, every later process()
    // call is guaranteed to load the new snapshot, so the retired ones are unreachable.
    if (! impl_->busy.load())
        impl_->retired.clear();
}

bool MixerEngine::latenciesChanged() const
{
    const auto* s = impl_->active.load();
    if (! s)
        return false;
    for (std::size_t t = 0; t < s->cfg.tracks.size(); ++t)
        for (std::size_t k = 0; k < s->cfg.tracks[t].slots.size(); ++k)
        {
            const auto& e = s->cfg.tracks[t].slots[k].effect;
            if (e && std::max(0, e->latency()) != s->slotLat[t][k])
                return true;
        }
    return false;
}

int MixerEngine::latency() const noexcept { return impl_->latency.load(); }

void MixerEngine::process(const IO& io, int numSamples) noexcept
{
    struct Guard
    {
        std::atomic<bool>& b;
        explicit Guard(std::atomic<bool>& x) : b(x) { b.store(true); }
        ~Guard() { b.store(false); }
    } guard(impl_->busy);

    Snapshot* snap = impl_->active.load();
    if (! snap)
    {
        if (io.outL) zero(io.outL, numSamples);
        if (io.outR) zero(io.outR, numSamples);
        for (int k = 0; k < kMaxHostOut; ++k)
        {
            if (io.auxOutL[k]) zero(io.auxOutL[k], numSamples);
            if (io.auxOutR[k]) zero(io.auxOutR[k], numSamples);
        }
        return;
    }
    for (int off = 0; off < numSamples; off += snap->maxBlock)
        impl_->processChunk(*snap, io, off, std::min(snap->maxBlock, numSamples - off));
}
}
