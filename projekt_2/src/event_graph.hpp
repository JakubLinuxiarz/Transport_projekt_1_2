#pragma once

#include "gtfs.hpp"

#include <string>
#include <vector>
#include <unordered_map>

struct TransitEvent {
    int id = -1;

    std::string stop_id;
    std::string trip_id;
    std::string route_id;

    int arrival_time = 0;
    int departure_time = 0;

    int stop_sequence = 0;

    std::string date;
};

struct EventEdge {
    int from_event = -1;
    int to_event = -1;

    int cost_seconds = 0;
    int walk_seconds = 0;

    // ride, wait, walk
    std::string type;
};

struct EventGraph {
    std::vector<TransitEvent> events;
    std::vector<std::vector<EventEdge>> adjacency;

    std::unordered_map<std::string, std::vector<int>> events_by_stop;
    std::unordered_map<std::string, std::vector<int>> events_by_trip;
};

EventGraph build_event_graph_for_date(
    const GTFSData& data,
    const std::string& date
);
