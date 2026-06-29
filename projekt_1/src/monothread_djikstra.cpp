#include "monothread_djikstra.hpp"
#include "trace.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

template <typename T>
static std::vector<T> read_vector_binary(const std::string& path) {
    std::ifstream in(path, std::ios::binary);

    if (!in) {
        throw std::runtime_error("Nie mogę otworzyć pliku: " + path);
    }

    uint64_t size = 0;
    in.read(reinterpret_cast<char*>(&size), sizeof(size));

    std::vector<T> vec(size);

    if (size > 0) {
        in.read(reinterpret_cast<char*>(vec.data()), sizeof(T) * size);
    }

    return vec;
}

LoadedGraph load_graph_for_dijkstra(const std::string& data_dir) {
    LoadedGraph g;

    g.nodes = read_vector_binary<GraphNode>(data_dir + "/nodes.bin");
    g.offsets = read_vector_binary<uint64_t>(data_dir + "/offsets.bin");
    g.edges = read_vector_binary<GraphEdge>(data_dir + "/edges.bin");

    g.reverse_offsets.assign(g.nodes.size() + 1, 0);

    for (uint32_t from = 0; from < g.nodes.size(); from++) {
        for (uint64_t i = g.offsets[from]; i < g.offsets[from + 1]; i++) {
            uint32_t to = g.edges[i].to;
            g.reverse_offsets[to + 1]++;
        }
    }

    for (size_t i = 1; i < g.reverse_offsets.size(); i++) {
        g.reverse_offsets[i] += g.reverse_offsets[i - 1];
    }

    g.reverse_edges.resize(g.edges.size());

    std::vector<uint64_t> cursor = g.reverse_offsets;

    for (uint32_t from = 0; from < g.nodes.size(); from++) {
        for (uint64_t i = g.offsets[from]; i < g.offsets[from + 1]; i++) {
            const GraphEdge& e = g.edges[i];

            uint64_t pos = cursor[e.to]++;

            g.reverse_edges[pos] = GraphEdge{
                from,
                e.seconds,
                e.meters
            };
        }
    }

    return g;
}

struct QueueItem {
    float dist;
    uint32_t node;

    bool operator>(const QueueItem& other) const {
        return dist > other.dist;
    }
};

static std::vector<uint32_t> build_path(
    uint32_t meeting,
    const std::vector<uint32_t>& parent_f,
    const std::vector<uint32_t>& parent_b
) {
    std::vector<uint32_t> left;
    std::vector<uint32_t> right;

    uint32_t v = meeting;

    while (v != UINT32_MAX) {
        left.push_back(v);
        v = parent_f[v];
    }

    std::reverse(left.begin(), left.end());

    v = parent_b[meeting];

    while (v != UINT32_MAX) {
        right.push_back(v);
        v = parent_b[v];
    }

    left.insert(left.end(), right.begin(), right.end());

    return left;
}

static float path_meters(
    const LoadedGraph& g,
    const std::vector<uint32_t>& path
) {
    float sum = 0.0f;

    for (size_t i = 1; i < path.size(); i++) {
        uint32_t from = path[i - 1];
        uint32_t to = path[i];

        for (uint64_t e = g.offsets[from]; e < g.offsets[from + 1]; e++) {
            if (g.edges[e].to == to) {
                sum += g.edges[e].meters;
                break;
            }
        }
    }

    return sum;
}

DijkstraResult bidirectional_dijkstra(
    const LoadedGraph& g,
    uint32_t start,
    uint32_t target,
    std::vector<TraceEvent>* trace
) {
    constexpr float INF = std::numeric_limits<float>::infinity();

    DijkstraResult result;

    if (start >= g.nodes.size() || target >= g.nodes.size()) {
        return result;
    }

    if (start == target) {
        result.found = true;
        result.seconds = 0.0f;
        result.meters = 0.0f;
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

    std::priority_queue<
        QueueItem,
        std::vector<QueueItem>,
        std::greater<QueueItem>
    > pq_f;

    std::priority_queue<
        QueueItem,
        std::vector<QueueItem>,
        std::greater<QueueItem>
    > pq_b;

    dist_f[start] = 0.0f;
    dist_b[target] = 0.0f;

    pq_f.push({0.0f, start});
    pq_b.push({0.0f, target});

    float best = INF;
    uint32_t meeting = UINT32_MAX;

    while (!pq_f.empty() && !pq_b.empty()) {
        float min_f = pq_f.top().dist;
        float min_b = pq_b.top().dist;

        if (min_f + min_b >= best) {
            break;
        }

        if (min_f <= min_b) {
            QueueItem item = pq_f.top();
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
                uint32_t to = e.to;

                float nd = dist_f[v] + e.seconds;

                if (nd < dist_f[to]) {
                    dist_f[to] = nd;
                    parent_f[to] = v;
                    pq_f.push({nd, to});

                    if (dist_b[to] < INF) {
                        float candidate = nd + dist_b[to];

                        if (candidate < best) {
                            best = candidate;
                            meeting = to;
                        }
                    }
                }
            }
        } else {
            QueueItem item = pq_b.top();
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
                uint32_t to = e.to;

                float nd = dist_b[v] + e.seconds;

                if (nd < dist_b[to]) {
                    dist_b[to] = nd;
                    parent_b[to] = v;
                    pq_b.push({nd, to});

                    if (dist_f[to] < INF) {
                        float candidate = nd + dist_f[to];

                        if (candidate < best) {
                            best = candidate;
                            meeting = to;
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
    result.path = build_path(meeting, parent_f, parent_b);
    result.meters = path_meters(g, result.path);

    return result;
}
