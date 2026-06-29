#include "monothread_Astar.hpp"
#include "trace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

static constexpr double EARTH_RADIUS_M = 6371000.0;
static constexpr double MAX_SPEED_MPS = 140.0 * 1000.0 / 3600.0;

static double deg2rad_astar(double deg) {
    return deg * M_PI / 180.0;
}

static float haversine_m_astar(
    double lat1,
    double lon1,
    double lat2,
    double lon2
) {
    double dlat = deg2rad_astar(lat2 - lat1);
    double dlon = deg2rad_astar(lon2 - lon1);

    lat1 = deg2rad_astar(lat1);
    lat2 = deg2rad_astar(lat2);

    double a =
        std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
        std::cos(lat1) * std::cos(lat2) *
        std::sin(dlon / 2.0) * std::sin(dlon / 2.0);

    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    return static_cast<float>(EARTH_RADIUS_M * c);
}

static float heuristic_seconds(
    const LoadedGraph& g,
    uint32_t from,
    uint32_t to
) {
    const GraphNode& a = g.nodes[from];
    const GraphNode& b = g.nodes[to];

    float meters = haversine_m_astar(
        a.lat,
        a.lon,
        b.lat,
        b.lon
    );

    return static_cast<float>(meters / MAX_SPEED_MPS);
}

struct AStarQueueItem {
    float priority;
    float dist;
    uint32_t node;

    bool operator>(const AStarQueueItem& other) const {
        return priority > other.priority;
    }
};

static std::vector<uint32_t> build_astar_path(
    uint32_t meeting,
    const std::vector<uint32_t>& parent_f,
    const std::vector<uint32_t>& parent_b
) {
    std::vector<uint32_t> path;

    uint32_t v = meeting;

    while (v != UINT32_MAX) {
        path.push_back(v);
        v = parent_f[v];
    }

    std::reverse(path.begin(), path.end());

    v = parent_b[meeting];

    while (v != UINT32_MAX) {
        path.push_back(v);
        v = parent_b[v];
    }

    return path;
}

static float calculate_astar_path_meters(
    const LoadedGraph& g,
    const std::vector<uint32_t>& path
) {
    float meters = 0.0f;

    for (size_t i = 1; i < path.size(); i++) {
        uint32_t from = path[i - 1];
        uint32_t to = path[i];

        for (uint64_t e = g.offsets[from]; e < g.offsets[from + 1]; e++) {
            if (g.edges[e].to == to) {
                meters += g.edges[e].meters;
                break;
            }
        }
    }

    return meters;
}

AStarResult bidirectional_astar(
    const LoadedGraph& g,
    uint32_t start,
    uint32_t target,
    std::vector<TraceEvent>* trace
) {
    constexpr float INF = std::numeric_limits<float>::infinity();

    AStarResult result;

    if (start >= g.nodes.size() || target >= g.nodes.size()) {
        return result;
    }

    if (start == target) {
        result.found = true;
        result.path = {start};
        if (trace) trace->push_back({start, 0});
        return result;
    }

    uint32_t n = static_cast<uint32_t>(g.nodes.size());

    std::vector<float> dist_f(n, INF);
    std::vector<float> dist_b(n, INF);

    std::vector<uint32_t> parent_f(n, UINT32_MAX);
    std::vector<uint32_t> parent_b(n, UINT32_MAX);

    std::vector<uint8_t> closed_f(n, 0);
    std::vector<uint8_t> closed_b(n, 0);

    std::priority_queue<
        AStarQueueItem,
        std::vector<AStarQueueItem>,
        std::greater<AStarQueueItem>
    > pq_f, pq_b;

    dist_f[start] = 0.0f;
    dist_b[target] = 0.0f;

    pq_f.push({heuristic_seconds(g, start, target), 0.0f, start});
    pq_b.push({heuristic_seconds(g, target, start), 0.0f, target});

    float best = INF;
    uint32_t meeting = UINT32_MAX;

    while (!pq_f.empty() && !pq_b.empty()) {
        float min_f = pq_f.top().priority;
        float min_b = pq_b.top().priority;

        if (min_f + min_b >= best) {
            break;
        }

        if (min_f <= min_b) {
            AStarQueueItem item = pq_f.top();
            pq_f.pop();

            uint32_t v = item.node;

            if (closed_f[v]) continue;
            if (item.dist != dist_f[v]) continue;

            closed_f[v] = 1;
            result.visited_forward++;
            if (trace) trace->push_back({v, 0});

            if (closed_b[v]) {
                float candidate = dist_f[v] + dist_b[v];

                if (candidate < best) {
                    best = candidate;
                    meeting = v;
                }
            }

            for (uint64_t i = g.offsets[v]; i < g.offsets[v + 1]; i++) {
                const GraphEdge& e = g.edges[i];

                float nd = dist_f[v] + e.seconds;

                if (nd < dist_f[e.to]) {
                    dist_f[e.to] = nd;
                    parent_f[e.to] = v;

                    float priority = nd + heuristic_seconds(g, e.to, target);

                    pq_f.push({priority, nd, e.to});

                    if (dist_b[e.to] < INF) {
                        float candidate = nd + dist_b[e.to];

                        if (candidate < best) {
                            best = candidate;
                            meeting = e.to;
                        }
                    }
                }
            }
        } else {
            AStarQueueItem item = pq_b.top();
            pq_b.pop();

            uint32_t v = item.node;

            if (closed_b[v]) continue;
            if (item.dist != dist_b[v]) continue;

            closed_b[v] = 1;
            result.visited_backward++;
            if (trace) trace->push_back({v, 1});

            if (closed_f[v]) {
                float candidate = dist_f[v] + dist_b[v];

                if (candidate < best) {
                    best = candidate;
                    meeting = v;
                }
            }

            for (uint64_t i = g.reverse_offsets[v]; i < g.reverse_offsets[v + 1]; i++) {
                const GraphEdge& e = g.reverse_edges[i];

                float nd = dist_b[v] + e.seconds;

                if (nd < dist_b[e.to]) {
                    dist_b[e.to] = nd;
                    parent_b[e.to] = v;

                    float priority = nd + heuristic_seconds(g, e.to, start);

                    pq_b.push({priority, nd, e.to});

                    if (dist_f[e.to] < INF) {
                        float candidate = nd + dist_f[e.to];

                        if (candidate < best) {
                            best = candidate;
                            meeting = e.to;
                        }
                    }
                }
            }
        }
    }

    if (meeting == UINT32_MAX) {
        return result;
    }

    result.found = true;
    result.seconds = best;
    result.path = build_astar_path(meeting, parent_f, parent_b);
    result.meters = calculate_astar_path_meters(g, result.path);

    return result;
}
