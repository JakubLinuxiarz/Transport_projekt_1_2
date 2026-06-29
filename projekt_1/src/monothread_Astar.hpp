#pragma once

#include "monothread_djikstra.hpp"

#include <cstdint>
#include <vector>

struct TraceEvent;

struct AStarResult {
    bool found = false;
    float seconds = 0.0f;
    float meters = 0.0f;
    std::vector<uint32_t> path;
    uint64_t visited_forward = 0;
    uint64_t visited_backward = 0;
};

AStarResult bidirectional_astar(
    const LoadedGraph& g,
    uint32_t start,
    uint32_t target,
    std::vector<TraceEvent>* trace = nullptr
);
