#include "router.hpp"
#include "util.hpp"

#include <queue>
#include <limits>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <tuple>

struct Label {
    int id = -1;
    int event_id = -1;

    int time = 0;
    int boardings = 0;
    int transfers = 0;

    std::string current_trip_id;

    int parent_label = -1;
    std::string edge_type;
};

struct QueueNode {
    int label_id;
    int time;
    int transfers;
    RouteMode mode;

    bool operator>(const QueueNode& other) const {
        if (mode == RouteMode::MinTransfers) {
            if (transfers != other.transfers) return transfers > other.transfers;
            return time > other.time;
        }

        if (time != other.time) return time > other.time;
        return transfers > other.transfers;
    }
};

static std::string state_key(int event_id, const std::string& current_trip_id) {
    return std::to_string(event_id) + "|" + current_trip_id;
}

static bool better(
    RouteMode mode,
    int new_time,
    int new_transfers,
    int old_time,
    int old_transfers
) {
    if (mode == RouteMode::MinTransfers) {
        if (new_transfers != old_transfers) return new_transfers < old_transfers;
        return new_time < old_time;
    }

    if (new_time != old_time) return new_time < old_time;
    return new_transfers < old_transfers;
}

RouteResult find_route(
    const GTFSData& data,
    const EventGraph& graph,
    const std::vector<std::string>& start_stop_ids,
    const std::vector<std::string>& target_stop_ids,
    int start_time_seconds,
    RouteMode mode
) {
    (void)data;

    RouteResult result;
    result.start_time = start_time_seconds;

    std::unordered_set<std::string> target_set(
        target_stop_ids.begin(),
        target_stop_ids.end()
    );

    std::vector<Label> labels;
    labels.reserve(300000);

    std::unordered_map<std::string, int> best_label;

    std::priority_queue<
        QueueNode,
        std::vector<QueueNode>,
        std::greater<QueueNode>
    > pq;

    auto add_label = [&](Label label) {
        label.id = (int)labels.size();

        std::string key = state_key(label.event_id, label.current_trip_id);

        auto it = best_label.find(key);

        if (it != best_label.end()) {
            const Label& old = labels[it->second];

            if (!better(
                    mode,
                    label.time,
                    label.transfers,
                    old.time,
                    old.transfers))
            {
                return -1;
            }
        }

        labels.push_back(label);
        best_label[key] = label.id;

        pq.push(QueueNode{
            label.id,
            label.time,
            label.transfers,
            mode
        });

        return label.id;
    };

    int start_labels = 0;

    for (const std::string& stop_id : start_stop_ids) {
        auto it = graph.events_by_stop.find(stop_id);

        if (it == graph.events_by_stop.end()) continue;

        for (int ev_id : it->second) {
            const TransitEvent& ev = graph.events[ev_id];

            if (ev.departure_time < start_time_seconds) continue;

            Label label;
            label.event_id = ev_id;
            label.time = ev.departure_time;
            label.boardings = 0;
            label.transfers = 0;
            label.current_trip_id = "";
            label.parent_label = -1;
            label.edge_type = "start";

            if (add_label(label) != -1) start_labels++;
        }
    }

    if (start_labels == 0) {
        std::cout << "Nie znaleziono odjazdow z grupy startowej.\n";
        return result;
    }

    int best_target_label = -1;

    while (!pq.empty()) {
        QueueNode node = pq.top();
        pq.pop();

        const Label& current_label = labels[node.label_id];

        if (node.time != current_label.time ||
            node.transfers != current_label.transfers)
        {
            continue;
        }

        const TransitEvent& current_event =
            graph.events[current_label.event_id];

        if (target_set.contains(current_event.stop_id)) {
            best_target_label = current_label.id;
            break;
        }

        for (const EventEdge& edge : graph.adjacency[current_label.event_id]) {
            int to_event_id = edge.to_event;

            const TransitEvent& from_event =
                graph.events[current_label.event_id];

            const TransitEvent& to_event =
                graph.events[to_event_id];

            Label next;
            next.event_id = to_event_id;
            next.parent_label = current_label.id;
            next.edge_type = edge.type;
            next.boardings = current_label.boardings;
            next.transfers = current_label.transfers;
            next.current_trip_id = current_label.current_trip_id;

            if (edge.type == "ride") {
                std::string ride_trip = to_event.trip_id;

                if (current_label.current_trip_id != ride_trip) {
                    next.boardings++;

                    if (next.boardings > 1) {
                        next.transfers = next.boardings - 1;
                    }

                    next.current_trip_id = ride_trip;
                }

                next.time = to_event.arrival_time;
            } else if (edge.type == "walk") {
                next.time = from_event.arrival_time + edge.walk_seconds;
                next.current_trip_id = "";
            } else {
                next.time = to_event.departure_time;
                next.current_trip_id = "";
            }

            add_label(next);
        }
    }

    if (best_target_label == -1) return result;

    const Label& final_label = labels[best_target_label];

    result.found = true;
    result.arrival_time = final_label.time;
    result.total_seconds = result.arrival_time - start_time_seconds;
    result.transfers = final_label.transfers;

    std::vector<int> label_path;

    for (int v = best_target_label; v != -1; v = labels[v].parent_label) {
        label_path.push_back(v);
    }

    std::reverse(label_path.begin(), label_path.end());

    for (int i = 0; i + 1 < (int)label_path.size(); i++) {
        const Label& a_label = labels[label_path[i]];
        const Label& b_label = labels[label_path[i + 1]];

        const TransitEvent& a = graph.events[a_label.event_id];
        const TransitEvent& b = graph.events[b_label.event_id];

        RouteStep step;
        step.type = b_label.edge_type;

        step.from_event = a.id;
        step.to_event = b.id;

        step.stop_from_id = a.stop_id;
        step.stop_to_id = b.stop_id;

        step.route_id = b.route_id;
        step.trip_id = b.trip_id;

        step.departure_time = a.departure_time;
        step.arrival_time = b_label.time;

        result.steps.push_back(step);
    }

    return result;
}




RouteResult find_latest_departure(
    const GTFSData& data,
    const EventGraph& graph,
    const std::vector<std::string>& start_stop_ids,
    const std::vector<std::string>& target_stop_ids,
    int latest_arrival_time_seconds,
    RouteMode inner_mode
) {
    (void)data;
    (void)inner_mode;

    RouteResult result;

    const int NEG_INF = std::numeric_limits<int>::min() / 4;

    std::unordered_set<std::string> start_set(
        start_stop_ids.begin(),
        start_stop_ids.end()
    );

    std::unordered_set<std::string> target_set(
        target_stop_ids.begin(),
        target_stop_ids.end()
    );

    std::vector<std::vector<EventEdge>> reverse_adjacency(
        graph.events.size()
    );

    for (int from = 0; from < (int)graph.adjacency.size(); from++) {
        for (const EventEdge& edge : graph.adjacency[from]) {
            reverse_adjacency[edge.to_event].push_back(edge);
        }
    }

    std::vector<int> latest(graph.events.size(), NEG_INF);
    std::vector<int> next_event(graph.events.size(), -1);
    std::vector<std::string> next_edge_type(graph.events.size());
    std::vector<int> next_edge_walk_seconds(graph.events.size(), 0);

    struct ReverseNode {
        int event_id;
        int latest_time;

        bool operator<(const ReverseNode& other) const {
            return latest_time < other.latest_time;
        }
    };

    std::priority_queue<ReverseNode> pq;

    int target_seed_count = 0;

    for (int event_id = 0; event_id < (int)graph.events.size(); event_id++) {
        const TransitEvent& ev = graph.events[event_id];

        if (!target_set.contains(ev.stop_id)) {
            continue;
        }

        if (ev.arrival_time > latest_arrival_time_seconds) {
            continue;
        }

        latest[event_id] = latest_arrival_time_seconds;
        pq.push(ReverseNode{event_id, latest_arrival_time_seconds});
        target_seed_count++;
    }

    if (target_seed_count == 0) {
        return result;
    }

    while (!pq.empty()) {
        ReverseNode cur = pq.top();
        pq.pop();

        if (cur.latest_time != latest[cur.event_id]) {
            continue;
        }

        for (const EventEdge& edge : reverse_adjacency[cur.event_id]) {
            int from = edge.from_event;
            int to = edge.to_event;

            const TransitEvent& from_event = graph.events[from];
            const TransitEvent& to_event = graph.events[to];

            int candidate = NEG_INF;

            if (edge.type == "ride") {
                if (latest[to] >= to_event.arrival_time) {
                    candidate = from_event.departure_time;
                }
            } else if (edge.type == "wait") {
                if (latest[to] >= to_event.departure_time) {
                    candidate = from_event.arrival_time;
                }
            } else if (edge.type == "walk") {
                candidate =
                    std::min(
                        from_event.arrival_time,
                        latest[to] - edge.walk_seconds
                    );
            }

            if (candidate <= NEG_INF / 2) {
                continue;
            }

            if (candidate > latest[from]) {
                latest[from] = candidate;
                next_event[from] = to;
                next_edge_type[from] = edge.type;
                next_edge_walk_seconds[from] = edge.walk_seconds;

                pq.push(ReverseNode{from, candidate});
            }
        }
    }

    int best_start_event = -1;
    int best_departure_time = NEG_INF;

    for (const std::string& stop_id : start_stop_ids) {
        auto it = graph.events_by_stop.find(stop_id);

        if (it == graph.events_by_stop.end()) {
            continue;
        }

        for (int event_id : it->second) {
            const TransitEvent& ev = graph.events[event_id];

            if (latest[event_id] == NEG_INF) {
                continue;
            }

            if (ev.departure_time > latest[event_id]) {
                continue;
            }

            if (ev.departure_time > best_departure_time) {
                best_departure_time = ev.departure_time;
                best_start_event = event_id;
            }
        }
    }

    if (best_start_event == -1) {
        return result;
    }

    std::vector<int> path;

    int current = best_start_event;

    while (current != -1) {
        path.push_back(current);

        if (target_set.contains(graph.events[current].stop_id)) {
            break;
        }

        current = next_event[current];
    }

    if (path.empty()) {
        return result;
    }

    int final_event = path.back();

    result.found = true;
    result.start_time = best_departure_time;
    result.arrival_time =
        std::min(latest_arrival_time_seconds, graph.events[final_event].arrival_time);

    if (target_set.contains(graph.events[final_event].stop_id)) {
        result.arrival_time =
            std::min(latest_arrival_time_seconds, latest[final_event]);
    }

    int boardings = 0;
    int transfers = 0;
    int walk_seconds = 0;
    std::string current_trip;

    for (int i = 0; i + 1 < (int)path.size(); i++) {
        int from = path[i];
        int to = path[i + 1];

        const TransitEvent& a = graph.events[from];
        const TransitEvent& b = graph.events[to];

        std::string edge_type = next_edge_type[from];

        RouteStep step;
        step.type = edge_type;
        step.from_event = from;
        step.to_event = to;
        step.stop_from_id = a.stop_id;
        step.stop_to_id = b.stop_id;
        step.route_id = b.route_id;
        step.trip_id = b.trip_id;
        step.departure_time = a.departure_time;

        if (edge_type == "ride") {
            if (current_trip != b.trip_id) {
                boardings++;

                if (boardings > 1) {
                    transfers = boardings - 1;
                }

                current_trip = b.trip_id;
            }

            step.arrival_time = b.arrival_time;
        } else if (edge_type == "walk") {
            step.arrival_time = a.arrival_time + next_edge_walk_seconds[from];
            walk_seconds += next_edge_walk_seconds[from];
            current_trip.clear();
        } else {
            step.arrival_time = b.departure_time;
            current_trip.clear();
        }

        result.steps.push_back(step);
    }

    if (!result.steps.empty()) {
        result.arrival_time = result.steps.back().arrival_time;
    }

    result.total_seconds = result.arrival_time - result.start_time;
    result.boardings = boardings;
    result.transfers = transfers;
    result.walk_seconds = walk_seconds;

    return result;
}


static std::string stop_name(const GTFSData& data, const std::string& stop_id) {
    auto it = data.stops.find(stop_id);
    if (it == data.stops.end()) return stop_id;
    return it->second.name;
}

static std::string route_name(const GTFSData& data, const std::string& route_id) {
    auto it = data.routes.find(route_id);
    if (it == data.routes.end()) return route_id;
    return it->second.short_name;
}

void print_route_result(
    const GTFSData& data,
    const EventGraph& graph,
    const RouteResult& result
) {
    if (!result.found) {
        std::cout << "Nie znaleziono trasy.\n";
        return;
    }

    std::cout << "\nZnaleziono trase.\n";
    std::cout << "Start:      " << seconds_to_time(result.start_time) << "\n";
    std::cout << "Koniec:     " << seconds_to_time(result.arrival_time) << "\n";
    std::cout << "Czas:       " << result.total_seconds / 60 << " min\n";
    std::cout << "Przesiadki: " << result.transfers << "\n\n";

    std::string current_trip;
    std::string current_route;
    int segment_start_event = -1;
    int segment_end_event = -1;

    auto flush_ride = [&]() {
        if (segment_start_event == -1 || segment_end_event == -1) return;

        const TransitEvent& a = graph.events[segment_start_event];
        const TransitEvent& b = graph.events[segment_end_event];

        std::cout
            << seconds_to_time(a.departure_time)
            << "  wsiadz: linia "
            << route_name(data, current_route)
            << " z przystanku "
            << stop_name(data, a.stop_id)
            << "\n";

        std::cout
            << seconds_to_time(b.arrival_time)
            << "  wysiadz: "
            << stop_name(data, b.stop_id)
            << "\n\n";
    };

    for (const RouteStep& step : result.steps) {
        if (step.type == "ride") {
            if (current_trip.empty()) {
                current_trip = step.trip_id;
                current_route = step.route_id;
                segment_start_event = step.from_event;
                segment_end_event = step.to_event;
            } else if (step.trip_id == current_trip) {
                segment_end_event = step.to_event;
            } else {
                flush_ride();

                current_trip = step.trip_id;
                current_route = step.route_id;
                segment_start_event = step.from_event;
                segment_end_event = step.to_event;
            }
        } else {
            flush_ride();

            current_trip.clear();
            current_route.clear();
            segment_start_event = -1;
            segment_end_event = -1;

            const TransitEvent& a = graph.events[step.from_event];
            const TransitEvent& b = graph.events[step.to_event];

            if (step.type == "walk") {
                std::cout
                    << seconds_to_time(a.arrival_time)
                    << "  przejdz pieszo z "
                    << stop_name(data, a.stop_id)
                    << " na "
                    << stop_name(data, b.stop_id)
                    << "\n";

                std::cout
                    << seconds_to_time(step.arrival_time)
                    << "  jestes na "
                    << stop_name(data, b.stop_id)
                    << "\n\n";
            } else {
                std::cout
                    << seconds_to_time(a.arrival_time)
                    << "  czekaj na przystanku "
                    << stop_name(data, a.stop_id)
                    << " do "
                    << seconds_to_time(b.departure_time)
                    << "\n";
            }
        }
    }

    flush_ride();
}
