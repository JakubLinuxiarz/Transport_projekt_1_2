#pragma once

#include "monothread_djikstra.hpp"

#include <cstdint>
#include <vector>

struct TraceEvent;

struct MultithreadDijkstraResult {
    bool found = false;
    float seconds = 0.0f;
    float meters = 0.0f;
    std::vector<uint32_t> path;
    uint64_t visited_forward = 0;
    uint64_t visited_backward = 0;
};

MultithreadDijkstraResult multithread_bidirectional_dijkstra(
    const LoadedGraph& g,
    uint32_t start,
    uint32_t target,
    uint32_t batch_size = 1000,
    std::vector<TraceEvent>* trace = nullptr
);
