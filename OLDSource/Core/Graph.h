#pragma once

#include <vector>

#include "Config.h"

namespace lemon::core
{
struct Edge { int from; int to; };

// Every audio / sidechain dependency in the config: track->master, sends, and key sources.
std::vector<Edge> collectEdges(const MixerCfg& cfg);

// Kahn's algorithm (lowest ready index first). Returns false if the graph has a cycle.
bool topologicalOrder(int numTracks, const std::vector<Edge>& edges, std::vector<int>& order);
}
