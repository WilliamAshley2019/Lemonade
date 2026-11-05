#include "Pdc.h"

#include <algorithm>

namespace lemon::core
{
PdcPlan computePdc(const MixerCfg& cfg, const std::vector<std::vector<int>>& slotLat, const std::vector<int>& order)
{
    const int n = static_cast<int>(cfg.tracks.size());
    const auto N = static_cast<std::size_t>(n);

    PdcPlan p;
    p.arrival.assign(N, 0);
    p.leaving.assign(N, 0);
    p.masterPad.assign(N, 0);
    p.extPad.assign(N, 0);
    p.outPad.assign(N, 0);
    p.tapPad.assign(N, 0);
    p.sendPad.assign(N, {});
    p.keyPad.assign(N, {});

    std::vector<std::vector<int>> incoming(N);
    for (int t = 1; t < n; ++t)
    {
        const auto& tr = cfg.tracks[static_cast<std::size_t>(t)];
        if (tr.toMaster)
            incoming[0].push_back(t);
        for (const auto& s : tr.sends)
            incoming[static_cast<std::size_t>(s.dest)].push_back(t);
    }

    std::vector<long long> arr(N, 0), lv(N, 0);
    for (int t : order)
    {
        const auto ti = static_cast<std::size_t>(t);
        const auto& tr = cfg.tracks[ti];
        long long a = 0;
        for (int src : incoming[ti])
            a = std::max(a, lv[static_cast<std::size_t>(src)]);

        long long pre = 0;
        for (std::size_t s = 0; s < tr.slots.size(); ++s)
        {
            const auto& sl = tr.slots[s];
            if (sl.effect && sl.keySource >= 0)
                a = std::max(a, lv[static_cast<std::size_t>(sl.keySource)] - pre);
            pre += slotLat[ti][s];
        }
        arr[ti] = a;
        lv[ti] = a + pre;
    }

    const long long total = lv[0];
    p.total = static_cast<int>(total);

    for (int t = 0; t < n; ++t)
    {
        const auto ti = static_cast<std::size_t>(t);
        const auto& tr = cfg.tracks[ti];
        p.arrival[ti] = static_cast<int>(arr[ti]);
        p.leaving[ti] = static_cast<int>(lv[ti]);
        p.extPad[ti] = static_cast<int>(arr[ti]);
        if (t > 0 && tr.toMaster)
            p.masterPad[ti] = static_cast<int>(std::max(0LL, arr[0] - lv[ti]));
        if (tr.hostOut >= 0)
            p.outPad[ti] = static_cast<int>(std::max(0LL, total - lv[ti]));
        if (tr.tap)
            p.tapPad[ti] = static_cast<int>(std::max(0LL, total - lv[ti]));

        p.sendPad[ti].assign(tr.sends.size(), 0);
        for (std::size_t k = 0; k < tr.sends.size(); ++k)
            p.sendPad[ti][k] = static_cast<int>(std::max(0LL, arr[static_cast<std::size_t>(tr.sends[k].dest)] - lv[ti]));

        p.keyPad[ti].assign(tr.slots.size(), 0);
        long long pre = 0;
        for (std::size_t s = 0; s < tr.slots.size(); ++s)
        {
            const auto& sl = tr.slots[s];
            if (sl.effect && sl.keySource >= 0)
                p.keyPad[ti][s] = static_cast<int>(std::max(0LL, arr[ti] + pre - lv[static_cast<std::size_t>(sl.keySource)]));
            pre += slotLat[ti][s];
        }
    }
    return p;
}
}
