#pragma once

#include "graph.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct TraceEvent;

struct LoadedGraph {
    std::vector<GraphNode> nodes;
    std::vector<uint64_t> offsets;
    std::vector<GraphEdge> edges;

    std::vector<uint64_t> reverse_offsets;
    std::vector<GraphEdge> reverse_edges;
};

struct DijkstraResult {
    bool found = false;
    float seconds = 0.0f;
    float meters = 0.0f;
    std::vector<uint32_t> path;
    uint64_t visited_forward = 0;
    uint64_t visited_backward = 0;
};

LoadedGraph load_graph_for_dijkstra(const std::string& data_dir);

DijkstraResult bidirectional_dijkstra(
    const LoadedGraph& g,
    uint32_t start,
    uint32_t target,
    std::vector<TraceEvent>* trace = nullptr
);
