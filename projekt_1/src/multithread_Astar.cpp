#include "multithread_Astar.hpp"
#include "trace.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <thread>
#include <vector>

static constexpr double EARTH_RADIUS_M = 6371000.0;
static constexpr double MAX_SPEED_MPS = 140.0 * 1000.0 / 3600.0;

static double deg2rad(double deg) {
    return deg * M_PI / 180.0;
}

static float haversine_m(const GraphNode& a, const GraphNode& b) {
    double dlat = deg2rad(b.lat - a.lat);
    double dlon = deg2rad(b.lon - a.lon);

    double lat1 = deg2rad(a.lat);
    double lat2 = deg2rad(b.lat);

    double x =
        std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
        std::cos(lat1) * std::cos(lat2) *
        std::sin(dlon / 2.0) * std::sin(dlon / 2.0);

    double c = 2.0 * std::atan2(std::sqrt(x), std::sqrt(1.0 - x));

    return static_cast<float>(EARTH_RADIUS_M * c);
}

static float heuristic_seconds(const LoadedGraph& g, uint32_t a, uint32_t b) {
    return haversine_m(g.nodes[a], g.nodes[b]) / static_cast<float>(MAX_SPEED_MPS);
}

struct QueueItem {
    float priority;
    float dist;
    uint32_t node;

    bool operator>(const QueueItem& other) const {
        return priority > other.priority;
    }
};

static std::vector<uint32_t> build_path(
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

static float calculate_path_meters(
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

MultithreadAStarResult multithread_bidirectional_astar(
    const LoadedGraph& g,
    uint32_t start,
    uint32_t target,
    std::vector<TraceEvent>* trace,
    uint32_t batch_size
) {
    constexpr float INF = std::numeric_limits<float>::infinity();

    MultithreadAStarResult result;

    if (start >= g.nodes.size() || target >= g.nodes.size()) {
        return result;
    }

    if (start == target) {
        result.found = true;
        result.path = {start};
        if (trace) trace->push_back({start, 0});
        return result;
    }

    const uint32_t n = static_cast<uint32_t>(g.nodes.size());

    std::vector<float> dist_f(n, INF);
    std::vector<float> dist_b(n, INF);

    std::vector<uint32_t> parent_f(n, UINT32_MAX);
    std::vector<uint32_t> parent_b(n, UINT32_MAX);

    std::vector<uint8_t> closed_f(n, 0);
    std::vector<uint8_t> closed_b(n, 0);

    std::vector<uint32_t> settled_f;
    std::vector<uint32_t> settled_b;

    settled_f.reserve(1'000'000);
    settled_b.reserve(1'000'000);

    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> pq_f;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> pq_b;

    dist_f[start] = 0.0f;
    dist_b[target] = 0.0f;

    pq_f.push({heuristic_seconds(g, start, target), 0.0f, start});
    pq_b.push({heuristic_seconds(g, target, start), 0.0f, target});

    std::atomic<uint32_t> epoch(0);
    std::atomic<uint32_t> done_count(0);
    std::atomic<bool> stop(false);

    std::atomic<float> top_f(INF);
    std::atomic<float> top_b(INF);

    std::atomic<uint64_t> visited_f(0);
    std::atomic<uint64_t> visited_b(0);

    auto forward_worker = [&]() {
        uint32_t local_epoch = 0;

        while (!stop.load(std::memory_order_relaxed)) {
            while (epoch.load(std::memory_order_acquire) == local_epoch &&
                   !stop.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            if (stop.load(std::memory_order_relaxed)) break;

            local_epoch = epoch.load(std::memory_order_relaxed);

            uint32_t processed = 0;

            while (processed < batch_size && !pq_f.empty()) {
                QueueItem item = pq_f.top();
                pq_f.pop();

                uint32_t v = item.node;

                if (closed_f[v]) continue;
                if (item.dist != dist_f[v]) continue;

                closed_f[v] = 1;
                settled_f.push_back(v);
                visited_f.fetch_add(1, std::memory_order_relaxed);
                processed++;

                for (uint64_t i = g.offsets[v]; i < g.offsets[v + 1]; i++) {
                    const GraphEdge& e = g.edges[i];

                    float nd = dist_f[v] + e.seconds;

                    if (nd < dist_f[e.to]) {
                        dist_f[e.to] = nd;
                        parent_f[e.to] = v;

                        pq_f.push({
                            nd + heuristic_seconds(g, e.to, target),
                            nd,
                            e.to
                        });
                    }
                }
            }

            if (!pq_f.empty()) top_f.store(pq_f.top().priority, std::memory_order_relaxed);
            else top_f.store(INF, std::memory_order_relaxed);

            done_count.fetch_add(1, std::memory_order_release);
        }
    };

    auto backward_worker = [&]() {
        uint32_t local_epoch = 0;

        while (!stop.load(std::memory_order_relaxed)) {
            while (epoch.load(std::memory_order_acquire) == local_epoch &&
                   !stop.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            if (stop.load(std::memory_order_relaxed)) break;

            local_epoch = epoch.load(std::memory_order_relaxed);

            uint32_t processed = 0;

            while (processed < batch_size && !pq_b.empty()) {
                QueueItem item = pq_b.top();
                pq_b.pop();

                uint32_t v = item.node;

                if (closed_b[v]) continue;
                if (item.dist != dist_b[v]) continue;

                closed_b[v] = 1;
                settled_b.push_back(v);
                visited_b.fetch_add(1, std::memory_order_relaxed);
                processed++;

                for (uint64_t i = g.reverse_offsets[v]; i < g.reverse_offsets[v + 1]; i++) {
                    const GraphEdge& e = g.reverse_edges[i];

                    float nd = dist_b[v] + e.seconds;

                    if (nd < dist_b[e.to]) {
                        dist_b[e.to] = nd;
                        parent_b[e.to] = v;

                        pq_b.push({
                            nd + heuristic_seconds(g, e.to, start),
                            nd,
                            e.to
                        });
                    }
                }
            }

            if (!pq_b.empty()) top_b.store(pq_b.top().priority, std::memory_order_relaxed);
            else top_b.store(INF, std::memory_order_relaxed);

            done_count.fetch_add(1, std::memory_order_release);
        }
    };

    std::thread tf(forward_worker);
    std::thread tb(backward_worker);

    float best = INF;
    uint32_t meeting = UINT32_MAX;

    size_t checked_f = 0;
    size_t checked_b = 0;

    while (true) {
        done_count.store(0, std::memory_order_relaxed);
        epoch.fetch_add(1, std::memory_order_release);

        while (done_count.load(std::memory_order_acquire) < 2) {
            std::this_thread::yield();
        }

        for (; checked_f < settled_f.size(); checked_f++) {
            uint32_t v = settled_f[checked_f];
            if (trace) trace->push_back({v, 0});

            if (closed_b[v]) {
                float candidate = dist_f[v] + dist_b[v];

                if (candidate < best) {
                    best = candidate;
                    meeting = v;
                }
            }
        }

        for (; checked_b < settled_b.size(); checked_b++) {
            uint32_t v = settled_b[checked_b];
            if (trace) trace->push_back({v, 1});

            if (closed_f[v]) {
                float candidate = dist_f[v] + dist_b[v];

                if (candidate < best) {
                    best = candidate;
                    meeting = v;
                }
            }
        }

        float mf = top_f.load(std::memory_order_relaxed);
        float mb = top_b.load(std::memory_order_relaxed);

        bool forward_empty = (mf == INF);
        bool backward_empty = (mb == INF);

        if (meeting != UINT32_MAX && mf + mb >= best) break;
        if (forward_empty || backward_empty) break;
    }

    stop.store(true, std::memory_order_release);
    epoch.fetch_add(1, std::memory_order_release);

    tf.join();
    tb.join();

    if (meeting == UINT32_MAX) {
        return result;
    }

    result.found = true;
    result.seconds = best;
    result.path = build_path(meeting, parent_f, parent_b);
    result.meters = calculate_path_meters(g, result.path);
    result.visited_forward = visited_f.load(std::memory_order_relaxed);
    result.visited_backward = visited_b.load(std::memory_order_relaxed);

    return result;
}
