#pragma once

#include <cstdint>

struct GraphNode {
    double lat;
    double lon;
};

struct GraphEdge {
    uint32_t to;
    float seconds;
    float meters;
};
