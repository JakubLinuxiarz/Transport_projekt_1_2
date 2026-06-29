#pragma once

#include "monothread_djikstra.hpp"

#include <cstdint>
#include <vector>

struct TraceEvent;

struct MultithreadAStarResult {
    bool found = false;
    float seconds = 0.0f;
    float meters = 0.0f;
    std::vector<uint32_t> path;
    uint64_t visited_forward = 0;
    uint64_t visited_backward = 0;
};

MultithreadAStarResult multithread_bidirectional_astar(
    const LoadedGraph& g,
    uint32_t start,
    uint32_t target,
    std::vector<TraceEvent>* trace = nullptr,
    uint32_t batch_size = 1000
);
