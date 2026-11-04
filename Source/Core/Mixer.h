#pragma once
// MixerEngine — the DAW-linkable mixer. No JUCE.
//
//  * Message thread: prepare(), publish(), collectGarbage(), live(t).* writes.
//  * Audio thread  : process() only. Never allocates, never locks.
//
// Topology changes (add plugin, add send, change sidechain...) go through publish(): a complete,
// pre-allocated Snapshot is built off-thread and swapped in atomically. Continuous controls
// (fader, pan, mute, solo, send level, slot mix/bypass, EQ, width, polarity) are plain atomics in
// LiveTrack, so automation never triggers a rebuild.
//
// Inputs, per block (all optional):
//   IO::inL/inR[b]       host input bus b      -> track cfg.hostInputTrack[b]
//   IO::trackL/trackR[t] direct feed for track t (what a DAW's channel rack would use)
// Outputs:
//   IO::outL/outR        master
//   IO::auxOutL/R[k]     tracks with cfg.hostOut == k (summed), delay-aligned with the master

#include <array>
#include <atomic>
#include <memory>
#include <string>

#include "Config.h"

namespace lemon::core
{
struct LiveTrack
{
    std::atomic<float> volume { 0.8f };       // fader position 0..1 (0.8 = 0 dB), see Fader.h
    std::atomic<float> pan    { 0.0f };       // -1..1
    std::atomic<float> width  { 1.0f };       // 0 = mono .. 2 = extra wide
    std::atomic<bool>  mute { false }, solo { false }, polarity { false }, swap { false };

    std::array<std::atomic<float>, kEqBands> eqGainDb {};   // dB, +-18
    std::array<std::atomic<float>, kEqBands> eqFreq {};     // Hz

    std::array<std::atomic<float>, kMaxSends> sendLevel {};   // fader position 0..1
    std::array<std::atomic<float>, kMaxSlots> slotMix {};     // 0 = dry .. 1 = wet
    std::array<std::atomic<bool>,  kMaxSlots> slotEnabled {};

    // Peak since last read: the UI does peakL.exchange(0).
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

    LiveTrack()
    {
        const float f[kEqBands] = { 100.0f, 1000.0f, 8000.0f };
        for (int i = 0; i < kEqBands; ++i) { eqGainDb[static_cast<std::size_t>(i)] = 0.0f; eqFreq[static_cast<std::size_t>(i)] = f[i]; }
        for (auto& s : sendLevel) s = 0.8f;
        for (auto& m : slotMix) m = 1.0f;
        for (auto& e : slotEnabled) e = true;
    }
};

struct IO
{
    const float* inL[kMaxHostIn] {};
    const float* inR[kMaxHostIn] {};
    const float* trackL[kMaxTracks] {};
    const float* trackR[kMaxTracks] {};
    float* outL = nullptr;
    float* outR = nullptr;
    float* auxOutL[kMaxHostOut] {};
    float* auxOutR[kMaxHostOut] {};
};

class MixerEngine
{
public:
    MixerEngine();
    ~MixerEngine();
    MixerEngine(const MixerEngine&) = delete;
    MixerEngine& operator=(const MixerEngine&) = delete;

    // ---- message thread ----
    void prepare(double sampleRate, int maxBlockSize);                 // rebuilds the last config if any
    bool publish(const MixerCfg& cfg, std::string* error = nullptr);   // false = rejected (e.g. routing loop); old snapshot stays live
    void collectGarbage();                                             // free retired snapshots when the audio thread is not inside one
    bool latenciesChanged() const;                                     // any effect now reports a different latency than the snapshot assumed
    int  latency() const noexcept;                                     // total PDC latency of the live snapshot
    double sampleRate() const noexcept { return sampleRate_; }
    int    maxBlockSize() const noexcept { return maxBlock_; }

    LiveTrack& live(int track) noexcept { return live_[static_cast<std::size_t>(track)]; }
    const LiveTrack& live(int track) const noexcept { return live_[static_cast<std::size_t>(track)]; }

    // ---- audio thread ----
    void process(const IO& io, int numSamples) noexcept;

private:
    struct Impl;
    std::array<LiveTrack, kMaxTracks> live_;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    std::unique_ptr<Impl> impl_;
};
}
