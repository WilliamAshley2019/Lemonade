#include "Graph.h"

#include <algorithm>

namespace lemon::core
{
std::vector<Edge> collectEdges(const MixerCfg& cfg)
{
    std::vector<Edge> edges;
    const int n = static_cast<int>(cfg.tracks.size());
    for (int t = 0; t < n; ++t)
    {
        const auto& tr = cfg.tracks[static_cast<std::size_t>(t)];
        if (t > 0 && tr.toMaster)
            edges.push_back({ t, 0 });
        for (const auto& s : tr.sends)
            edges.push_back({ t, s.dest });
        for (const auto& sl : tr.slots)
            if (sl.effect && sl.keySource >= 0)
                edges.push_back({ sl.keySource, t });
    }
    return edges;
}

bool topologicalOrder(int n, const std::vector<Edge>& edgesIn, std::vector<int>& order)
{
    std::vector<std::vector<int>> next(static_cast<std::size_t>(n));
    std::vector<int> indegree(static_cast<std::size_t>(n), 0);

    auto edges = edgesIn;
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b)
              { return a.from != b.from ? a.from < b.from : a.to < b.to; });
    edges.erase(std::unique(edges.begin(), edges.end(), [](const Edge& a, const Edge& b)
                            { return a.from == b.from && a.to == b.to; }), edges.end());

    for (const auto& e : edges)
    {
        if (e.from < 0 || e.from >= n || e.to < 0 || e.to >= n)
            return false;
        if (e.from == e.to)
            return false;
        next[static_cast<std::size_t>(e.from)].push_back(e.to);
        ++indegree[static_cast<std::size_t>(e.to)];
    }

    order.clear();
    std::vector<bool> done(static_cast<std::size_t>(n), false);
    for (int step = 0; step < n; ++step)
    {
        int pick = -1;
        for (int i = 0; i < n; ++i)
            if (!done[static_cast<std::size_t>(i)] && indegree[static_cast<std::size_t>(i)] == 0)
            {
                pick = i;
                break;
            }
        if (pick < 0)
            return false;                       // cycle
        done[static_cast<std::size_t>(pick)] = true;
        order.push_back(pick);
        for (int d : next[static_cast<std::size_t>(pick)])
            --indegree[static_cast<std::size_t>(d)];
    }
    return true;
}
}
