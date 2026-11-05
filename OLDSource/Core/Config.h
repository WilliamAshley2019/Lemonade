#pragma once
// Immutable-by-convention description of a mixer's topology. Built on the message thread and
// handed to MixerEngine::publish(). Everything that changes continuously (faders, pans, mutes,
// send levels, slot mix/bypass, EQ) is NOT here: it lives in LiveTrack atomics so automation
// never needs a rebuild.

#include <array>
#include <memory>
#include <vector>

#include "Common.h"
#include "Effect.h"

namespace lemon::core
{
struct SlotCfg
{
    std::shared_ptr<IEffect> effect;   // null = empty slot
    int keySource = -1;                // track whose pre-fader signal feeds the effect's sidechain; -1 = none
};

struct SendCfg
{
    int  dest = 1;                     // destination track (1..N-1)
    bool preFader = false;
};

struct TrackCfg
{
    bool toMaster = true;              // route post-fader output to the master (ignored on track 0)
    int  hostOut  = -1;                // -1 = none, 0..kMaxHostOut-1 = also copy to that host aux output
    std::vector<SlotCfg> slots;
    std::vector<SendCfg> sends;
};

struct MixerCfg
{
    std::vector<TrackCfg> tracks;      // [0] = master; size 1..kMaxTracks
    std::array<int, kMaxHostIn> hostInputTrack { 1, 2, 3, 4, 5 };   // host input bus -> track (-1 = unused)
};
}
