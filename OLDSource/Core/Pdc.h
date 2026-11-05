#pragma once
// Plugin delay compensation.
//
// A track "arrives" when the last of its inputs has arrived, and "leaves" `slotLatency` later.
// Every connection is padded so all signals meeting at a track are time-aligned:
//   audio edge   src -> dst : pad = arrival[dst] - leaving[src]
//   sidechain    src -> slot: pad = arrival[t] + (latency of slots before it) - leaving[src]
//   external feed (host / DAW) into track t : pad = arrival[t]       (external audio is "time 0")
//   host aux out : pad = total - leaving[t]  (so aux outputs line up with the main output)
// The plugin reports `total` (= leaving[master]) as its latency.

#include <vector>

#include "Config.h"

namespace lemon::core
{
struct PdcPlan
{
    std::vector<int> arrival, leaving;
    std::vector<int> masterPad;                 // [track] pad on the track->master edge
    std::vector<int> extPad;                    // [track] pad on host / DAW feed
    std::vector<int> outPad;                    // [track] pad on the host-aux-out tap
    std::vector<std::vector<int>> sendPad;      // [track][send]
    std::vector<std::vector<int>> keyPad;       // [track][slot]
    int total = 0;
};

// slotLat[t][s] = latency of slot s on track t (0 for empty slots). `order` must be topological.
PdcPlan computePdc(const MixerCfg& cfg, const std::vector<std::vector<int>>& slotLat, const std::vector<int>& order);
}
