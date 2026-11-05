#include "MixerController.h"

namespace lemon
{
using namespace core;

MixerController::MixerController(MixerEngine& e, juce::AudioPluginFormatManager& f, ForwardPlayHead& p)
    : engine(e), formats(f), playHead(p)
{
    model_.tracks.resize(9);
    for (int i = 0; i < 9; ++i)
        model_.tracks[(size_t) i].name = i == 0 ? "Master" : "Track " + juce::String(i);
    apply([](Model&) {});
    startTimer(200);
}

MixerController::~MixerController()
{
    *alive = false;
    stopTimer();
}

MixerCfg MixerController::buildCfg(const Model& m) const
{
    MixerCfg cfg;
    cfg.hostInputTrack = m.hostInputTrack;
    for (auto& tm : m.tracks)
    {
        TrackCfg tc;
        tc.toMaster = tm.toMaster;
        tc.hostOut = tm.hostOut;
        tc.sends = tm.sends;
        for (auto& sm : tm.slots)
        {
            SlotCfg sc;
            sc.effect = sm.slot;
            sc.keySource = sm.slot ? sm.keySource : -1;
            tc.slots.push_back(sc);
        }
        cfg.tracks.push_back(std::move(tc));
    }
    return cfg;
}

bool MixerController::apply(const std::function<void(Model&)>& mutate)
{
    Model next = model_;
    mutate(next);
    std::string err;
    if (! engine.publish(buildCfg(next), &err))
    {
        lastError = err;
        sendChangeMessage();
        return false;
    }
    lastError.clear();
    model_ = std::move(next);
    notifyLatency();
    sendChangeMessage();
    return true;
}

void MixerController::notifyLatency()
{
    const int l = engine.latency();
    if (l != lastLatency)
    {
        lastLatency = l;
        if (onLatencyChanged) onLatencyChanged();
    }
}

void MixerController::timerCallback()
{
    if (engine.latenciesChanged())      // a hosted plugin changed its reported latency
        apply([](Model&) {});
    engine.collectGarbage();
}

void MixerController::prepareAll(double sr, int block)
{
    for (auto& t : model_.tracks)
        for (auto& s : t.slots)
            if (s.slot) s.slot->prepare(sr, block);
    engine.prepare(sr, block);
    notifyLatency();
}

bool MixerController::addTrack()
{
    if (numTracks() >= kMaxTracks) return false;
    return apply([](Model& m)
    {
        TrackModel t;
        t.name = "Track " + juce::String((int) m.tracks.size());
        m.tracks.push_back(std::move(t));
    });
}

bool MixerController::removeLastTrack()
{
    if (numTracks() <= 2) return false;
    const int last = numTracks() - 1;
    for (auto& s : model_.tracks[(size_t) last].slots)
        if (s.slot && onSlotWillClose) onSlotWillClose(&s.slot->instance());
    return apply([last](Model& m)
    {
        m.tracks.pop_back();
        for (auto& t : m.tracks)
        {
            std::erase_if(t.sends, [last](const SendCfg& s) { return s.dest == last; });
            for (auto& s : t.slots) if (s.keySource == last) s.keySource = -1;
        }
        for (auto& h : m.hostInputTrack) if (h == last) h = -1;
    });
}

void MixerController::renameTrack(int t, const juce::String& n)
{
    model_.tracks[(size_t) t].name = n;
    sendChangeMessage();
}

bool MixerController::setToMaster(int t, bool on)
{
    return apply([=](Model& m) { m.tracks[(size_t) t].toMaster = on; });
}

bool MixerController::setHostOut(int t, int h)
{
    return apply([=](Model& m) { m.tracks[(size_t) t].hostOut = h; });
}

void MixerController::loadPlugin(int t, int s, const juce::PluginDescription& d)
{
    instantiate(t, s, d, {});
}

void MixerController::instantiate(int t, int s, const juce::PluginDescription& d, const juce::MemoryBlock& state)
{
    if (t < 0 || t >= numTracks()) return;
    auto& sm = model_.tracks[(size_t) t].slots[(size_t) s];
    if (sm.slot && onSlotWillClose) onSlotWillClose(&sm.slot->instance());
    sm.hasDesc = true; sm.desc = d; sm.pendingState = state; sm.loading = true; sm.missing = false;
    sendChangeMessage();

    const auto token = alive;
    formats.createPluginInstanceAsync(d, engine.sampleRate(), engine.maxBlockSize(),
        [this, token, t, s, d, state](std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
        {
            if (! *token) return;
            if (t >= numTracks()) return;
            juce::String problem = err;
            std::shared_ptr<HostedSlot> slot;
            if (inst)
            {
                slot = HostedSlot::create(std::move(inst), state.getSize() > 0 ? &state : nullptr, problem);
                if (slot)
                {
                    slot->setPlayHead(&playHead);
                    slot->prepare(engine.sampleRate(), engine.maxBlockSize());
                }
            }
            if (! slot)
            {
                auto& cur = model_.tracks[(size_t) t].slots[(size_t) s];
                cur.loading = false; cur.missing = true; cur.slot.reset();
                lastError = problem.isEmpty() ? juce::String("Could not load ") + d.name : problem;
                apply([](Model&) {});
                return;
            }
            auto old = model_.tracks[(size_t) t].slots[(size_t) s].slot;
            if (old && onSlotWillClose) onSlotWillClose(&old->instance());
            apply([&](Model& m)
            {
                auto& cur = m.tracks[(size_t) t].slots[(size_t) s];
                cur.slot = slot; cur.loading = false; cur.missing = false;
                cur.hasDesc = true; cur.desc = d; cur.pendingState = {};
            });
        });
}

bool MixerController::removePlugin(int t, int s)
{
    auto& cur = model_.tracks[(size_t) t].slots[(size_t) s];
    if (cur.slot && onSlotWillClose) onSlotWillClose(&cur.slot->instance());
    engine.live(t).slotEnabled[(size_t) s] = true;
    engine.live(t).slotMix[(size_t) s] = 1.0f;
    return apply([=](Model& m) { m.tracks[(size_t) t].slots[(size_t) s] = SlotModel {}; });
}

bool MixerController::setKeySource(int t, int s, int src)
{
    return apply([=](Model& m) { m.tracks[(size_t) t].slots[(size_t) s].keySource = src; });
}

bool MixerController::addSend(int t, int dest, bool pre)
{
    if (t <= 0 || (int) model_.tracks[(size_t) t].sends.size() >= kMaxSends) return false;
    const auto k = model_.tracks[(size_t) t].sends.size();
    const bool ok = apply([=](Model& m) { m.tracks[(size_t) t].sends.push_back({ dest, pre }); });
    if (ok) engine.live(t).sendLevel[k] = 0.8f;
    return ok;
}

bool MixerController::removeSend(int t, int k)
{
    auto& lv = engine.live(t);
    const int n = (int) model_.tracks[(size_t) t].sends.size();
    const bool ok = apply([=](Model& m) { m.tracks[(size_t) t].sends.erase(m.tracks[(size_t) t].sends.begin() + k); });
    if (ok)
        for (int i = k; i < n - 1; ++i)
            lv.sendLevel[(size_t) i] = lv.sendLevel[(size_t) i + 1].load();
    return ok;
}

bool MixerController::setSendPre(int t, int k, bool pre)
{
    return apply([=](Model& m) { m.tracks[(size_t) t].sends[(size_t) k].preFader = pre; });
}

bool MixerController::setSendDest(int t, int k, int dest)
{
    return apply([=](Model& m) { m.tracks[(size_t) t].sends[(size_t) k].dest = dest; });
}

juce::AudioPluginInstance* MixerController::instanceAt(int t, int s) const
{
    auto& sl = model_.tracks[(size_t) t].slots[(size_t) s].slot;
    return sl ? &sl->instance() : nullptr;
}

// ------------------------------------------------------------------ state
void MixerController::save(juce::ValueTree& root) const
{
    root.setProperty("hostIn", juce::String(model_.hostInputTrack[0]) + "," + juce::String(model_.hostInputTrack[1]) + ","
                     + juce::String(model_.hostInputTrack[2]) + "," + juce::String(model_.hostInputTrack[3]) + ","
                     + juce::String(model_.hostInputTrack[4]), nullptr);
    for (int t = 0; t < numTracks(); ++t)
    {
        auto& tm = model_.tracks[(size_t) t];
        auto& lv = engine.live(t);
        juce::ValueTree tv("Track");
        tv.setProperty("name", tm.name, nullptr);
        tv.setProperty("toMaster", tm.toMaster, nullptr);
        tv.setProperty("hostOut", tm.hostOut, nullptr);
        tv.setProperty("width", lv.width.load(), nullptr);
        tv.setProperty("polarity", lv.polarity.load(), nullptr);
        tv.setProperty("swap", lv.swap.load(), nullptr);
        for (int b = 0; b < kEqBands; ++b)
        {
            tv.setProperty("eqG" + juce::String(b), lv.eqGainDb[(size_t) b].load(), nullptr);
            tv.setProperty("eqF" + juce::String(b), lv.eqFreq[(size_t) b].load(), nullptr);
        }
        for (size_t k = 0; k < tm.sends.size(); ++k)
        {
            juce::ValueTree sv("Send");
            sv.setProperty("dest", tm.sends[k].dest, nullptr);
            sv.setProperty("pre", tm.sends[k].preFader, nullptr);
            sv.setProperty("level", lv.sendLevel[k].load(), nullptr);
            tv.addChild(sv, -1, nullptr);
        }
        for (int s = 0; s < kMaxSlots; ++s)
        {
            auto& sm = tm.slots[(size_t) s];
            if (! sm.hasDesc) continue;
            juce::ValueTree sv("Slot");
            sv.setProperty("index", s, nullptr);
            sv.setProperty("key", sm.keySource, nullptr);
            sv.setProperty("enabled", lv.slotEnabled[(size_t) s].load(), nullptr);
            sv.setProperty("mix", lv.slotMix[(size_t) s].load(), nullptr);
            if (auto xml = sm.desc.createXml())
                sv.setProperty("desc", xml->toString(), nullptr);
            juce::MemoryBlock state = sm.pendingState;        // keeps the state of a missing plugin
            if (sm.slot) { state.reset(); sm.slot->instance().getStateInformation(state); }
            sv.setProperty("state", state.toBase64Encoding(), nullptr);
            tv.addChild(sv, -1, nullptr);
        }
        root.addChild(tv, -1, nullptr);
    }
}

void MixerController::restore(const juce::ValueTree& root)
{
    // Close and drop everything that exists now.
    for (auto& t : model_.tracks)
        for (auto& s : t.slots)
            if (s.slot && onSlotWillClose) onSlotWillClose(&s.slot->instance());

    Model m;
    auto hi = juce::StringArray::fromTokens(root.getProperty("hostIn", "1,2,3,4,5").toString(), ",", "");
    for (int i = 0; i < kMaxHostIn && i < hi.size(); ++i) m.hostInputTrack[(size_t) i] = hi[i].getIntValue();

    struct Pending { int t, s; juce::PluginDescription d; juce::MemoryBlock state; };
    std::vector<Pending> pending;

    for (int t = 0; t < root.getNumChildren() && t < kMaxTracks; ++t)
    {
        auto tv = root.getChild(t);
        if (! tv.hasType("Track")) continue;
        TrackModel tm;
        tm.name = tv.getProperty("name", "Track").toString();
        tm.toMaster = (bool) tv.getProperty("toMaster", true);
        tm.hostOut = (int) tv.getProperty("hostOut", -1);
        const int ti = (int) m.tracks.size();
        auto& lv = engine.live(ti);
        lv.width = (float) (double) tv.getProperty("width", 1.0);
        lv.polarity = (bool) tv.getProperty("polarity", false);
        lv.swap = (bool) tv.getProperty("swap", false);
        for (int b = 0; b < kEqBands; ++b)
        {
            lv.eqGainDb[(size_t) b] = (float) (double) tv.getProperty("eqG" + juce::String(b), 0.0);
            lv.eqFreq[(size_t) b] = (float) (double) tv.getProperty("eqF" + juce::String(b), b == 0 ? 100.0 : b == 1 ? 1000.0 : 8000.0);
        }
        for (int c = 0; c < tv.getNumChildren(); ++c)
        {
            auto cv = tv.getChild(c);
            if (cv.hasType("Send") && tm.sends.size() < (size_t) kMaxSends)
            {
                lv.sendLevel[tm.sends.size()] = (float) (double) cv.getProperty("level", 0.8);
                tm.sends.push_back({ (int) cv.getProperty("dest", 1), (bool) cv.getProperty("pre", false) });
            }
            else if (cv.hasType("Slot"))
            {
                const int s = (int) cv.getProperty("index", 0);
                if (s < 0 || s >= kMaxSlots) continue;
                auto& sm = tm.slots[(size_t) s];
                sm.keySource = (int) cv.getProperty("key", -1);
                lv.slotEnabled[(size_t) s] = (bool) cv.getProperty("enabled", true);
                lv.slotMix[(size_t) s] = (float) (double) cv.getProperty("mix", 1.0);
                if (auto xml = juce::XmlDocument::parse(cv.getProperty("desc").toString()))
                {
                    sm.hasDesc = sm.desc.loadFromXml(*xml);
                    sm.missing = sm.hasDesc;      // until the instance arrives
                    sm.loading = sm.hasDesc;
                    juce::MemoryBlock st; st.fromBase64Encoding(cv.getProperty("state").toString());
                    sm.pendingState = st;
                    if (sm.hasDesc) pending.push_back({ ti, s, sm.desc, st });
                }
            }
        }
        m.tracks.push_back(std::move(tm));
    }
    if (m.tracks.size() < 2)
    {
        m.tracks.clear();
        m.tracks.resize(9);
        for (int i = 0; i < 9; ++i) m.tracks[(size_t) i].name = i == 0 ? "Master" : "Track " + juce::String(i);
    }
    // Drop references to tracks that do not exist (a corrupt or hand-edited state must not kill the engine).
    const int n = (int) m.tracks.size();
    for (auto& tr : m.tracks)
    {
        std::erase_if(tr.sends, [n](const SendCfg& s) { return s.dest < 1 || s.dest >= n; });
        for (auto& s : tr.slots) if (s.keySource >= n) s.keySource = -1;
    }

    if (! apply([&](Model& dst) { dst = m; }))
    {
        // Rejected (e.g. a loop in the saved state): fall back to a flat mixer, no routing between tracks.
        for (auto& tr : m.tracks) { tr.sends.clear(); for (auto& s : tr.slots) s.keySource = -1; }
        apply([&](Model& dst) { dst = m; });
    }
    for (auto& p : pending)
        instantiate(p.t, p.s, p.d, p.state);
}
}
