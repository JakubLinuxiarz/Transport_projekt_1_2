#include "event_graph.hpp"
#include "util.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <cmath>
#include <unordered_set>

static constexpr double MAX_WALK_METERS = 500.0;
static constexpr double WALK_SPEED_MPS = 5000.0 / 3600.0;

static std::string next_yyyymmdd(const std::string& input) {
    std::string d = normalize_gtfs_date(input);

    int y = std::stoi(d.substr(0, 4));
    int m = std::stoi(d.substr(4, 2));
    int day = std::stoi(d.substr(6, 2));

    static const int days_in_month[] = {
        0,
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    auto leap = [](int year) {
        return
            (year % 400 == 0) ||
            (year % 4 == 0 && year % 100 != 0);
    };

    int dim = days_in_month[m];

    if (m == 2 && leap(y)) {
        dim = 29;
    }

    day++;

    if (day > dim) {
        day = 1;
        m++;
    }

    if (m > 12) {
        m = 1;
        y++;
    }

    char buffer[16];

    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04d%02d%02d",
        y,
        m,
        day
    );

    return std::string(buffer);
}

static void add_events_for_service_date(
    EventGraph& graph,
    const GTFSData& data,
    const std::string& service_date,
    int time_offset
) {
    int total_stop_times = 0;
    int missing_trip = 0;
    int trip_not_running = 0;
    int added = 0;

    for (const StopTime& st : data.stop_times) {
        total_stop_times++;

        auto trip_it = data.trips.find(st.trip_id);

        if (trip_it == data.trips.end()) {
            missing_trip++;
            continue;
        }

        const Trip& trip = trip_it->second;

        if (!service_runs_on_date(data, trip.service_id, service_date)) {
            trip_not_running++;
            continue;
        }

        TransitEvent ev;

        ev.id = static_cast<int>(graph.events.size());
        ev.stop_id = st.stop_id;
        ev.trip_id = st.trip_id + "@" + service_date;
        ev.route_id = trip.route_id;
        ev.arrival_time = st.arrival + time_offset;
        ev.departure_time = st.departure + time_offset;
        ev.stop_sequence = st.sequence;
        ev.date = service_date;

        graph.events.push_back(ev);

        graph.events_by_stop[ev.stop_id].push_back(ev.id);
        graph.events_by_trip[ev.trip_id].push_back(ev.id);

        added++;
    }

    std::cout << "Debug service_date " << service_date << ":\n";
    std::cout << "  stop_times:       " << total_stop_times << "\n";
    std::cout << "  missing_trip:     " << missing_trip << "\n";
    std::cout << "  trip_not_running: " << trip_not_running << "\n";
    std::cout << "  added events:     " << added << "\n";
}

static void sort_indices_by_departure(
    const std::vector<TransitEvent>& events,
    std::vector<int>& ids
) {
    std::sort(
        ids.begin(),
        ids.end(),
        [&](int a, int b) {
            if (events[a].departure_time != events[b].departure_time) {
                return events[a].departure_time < events[b].departure_time;
            }

            return events[a].arrival_time < events[b].arrival_time;
        }
    );
}

static int walking_seconds_between(
    const GTFSData& data,
    const std::string& a,
    const std::string& b
) {
    auto ia = data.stops.find(a);
    auto ib = data.stops.find(b);

    if (ia == data.stops.end() || ib == data.stops.end()) {
        return -1;
    }

    double dist = haversine_meters(
        ia->second.lat,
        ia->second.lon,
        ib->second.lat,
        ib->second.lon
    );

    if (dist > MAX_WALK_METERS) {
        return -1;
    }

    return (int)std::ceil(dist / WALK_SPEED_MPS);
}

static void add_walk_edges(
    EventGraph& graph,
    const GTFSData& data
) {
    std::cout << "Dodawanie przejsc pieszych do 500 m...\n";
    std::cout << "  tryb: najblizszy event dla kazdej linii\n";

    std::vector<std::string> stop_ids;
    stop_ids.reserve(graph.events_by_stop.size());

    for (const auto& [stop_id, ids] : graph.events_by_stop) {
        (void)ids;
        stop_ids.push_back(stop_id);
    }

    size_t walk_edges = 0;
    size_t nearby_pairs = 0;

    for (const std::string& from_stop : stop_ids) {
        auto from_it = graph.events_by_stop.find(from_stop);

        if (from_it == graph.events_by_stop.end()) {
            continue;
        }

        const std::vector<int>& from_events = from_it->second;

        for (const std::string& to_stop : stop_ids) {
            if (from_stop == to_stop) {
                continue;
            }

            int walk_sec = walking_seconds_between(data, from_stop, to_stop);

            if (walk_sec < 0) {
                continue;
            }

            nearby_pairs++;

            auto to_it = graph.events_by_stop.find(to_stop);

            if (to_it == graph.events_by_stop.end()) {
                continue;
            }

            const std::vector<int>& to_events = to_it->second;

            for (int from_event : from_events) {
                const TransitEvent& a = graph.events[from_event];

                int ready_time = a.arrival_time + walk_sec;

                auto first = std::lower_bound(
                    to_events.begin(),
                    to_events.end(),
                    ready_time,
                    [&](int event_id, int value) {
                        return graph.events[event_id].departure_time < value;
                    }
                );

                std::unordered_set<std::string> seen_routes;

                for (auto it = first; it != to_events.end(); ++it) {
                    int to_event = *it;

                    const TransitEvent& b = graph.events[to_event];

                    if (seen_routes.contains(b.route_id)) {
                        continue;
                    }

                    seen_routes.insert(b.route_id);

                    int cost =
                        b.departure_time -
                        a.arrival_time;

                    if (cost < walk_sec) {
                        continue;
                    }

                    graph.adjacency[from_event].push_back(EventEdge{
                        from_event,
                        to_event,
                        cost,
                        walk_sec,
                        "walk"
                    });

                    walk_edges++;
                }
            }
        }
    }

    std::cout << "  pary przystankow blisko: " << nearby_pairs << "\n";
    std::cout << "  krawedzie piesze:        " << walk_edges << "\n";
}

EventGraph build_event_graph_for_date(
    const GTFSData& data,
    const std::string& date
) {
    EventGraph graph;

    std::string today = normalize_gtfs_date(date);
    std::string tomorrow = next_yyyymmdd(today);

    std::cout << "Budowanie event graph dla dat:\n";
    std::cout << "  dzis:   " << today << "\n";
    std::cout << "  jutro:  " << tomorrow << "\n";

    graph.events.reserve(data.stop_times.size() / 2);

    add_events_for_service_date(graph, data, today, 0);
    add_events_for_service_date(graph, data, tomorrow, 24 * 3600);

    graph.adjacency.resize(graph.events.size());

    for (auto& [trip_id, ids] : graph.events_by_trip) {
        std::sort(
            ids.begin(),
            ids.end(),
            [&](int a, int b) {
                return graph.events[a].stop_sequence < graph.events[b].stop_sequence;
            }
        );

        for (int i = 0; i + 1 < (int)ids.size(); i++) {
            int from = ids[i];
            int to = ids[i + 1];

            int cost =
                graph.events[to].arrival_time -
                graph.events[from].departure_time;

            if (cost < 0) continue;

            graph.adjacency[from].push_back(EventEdge{
                from,
                to,
                cost,
                0,
                "ride"
            });
        }
    }

    for (auto& [stop_id, ids] : graph.events_by_stop) {
        (void)stop_id;
        sort_indices_by_departure(graph.events, ids);
    }

    for (auto& [stop_id, ids] : graph.events_by_stop) {
        (void)stop_id;

        for (int i = 0; i + 1 < (int)ids.size(); i++) {
            int from = ids[i];
            int to = ids[i + 1];

            int cost =
                graph.events[to].departure_time -
                graph.events[from].arrival_time;

            if (cost < 0) continue;

            graph.adjacency[from].push_back(EventEdge{
                from,
                to,
                cost,
                0,
                "wait"
            });
        }
    }

    add_walk_edges(graph, data);

    std::cout << "Eventy: " << graph.events.size() << "\n";

    size_t edge_count = 0;

    for (const auto& edges : graph.adjacency) {
        edge_count += edges.size();
    }

    std::cout << "Krawedzie: " << edge_count << "\n";

    return graph;
}
