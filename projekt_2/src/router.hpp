#pragma once

#include "gtfs.hpp"
#include "event_graph.hpp"

#include <string>
#include <vector>

enum class RouteMode {
    EarliestArrival,
    Balanced,
    MinTransfers
};

struct RouteStep {
    std::string type;

    int from_event = -1;
    int to_event = -1;

    std::string stop_from_id;
    std::string stop_to_id;

    std::string route_id;
    std::string trip_id;

    int departure_time = 0;
    int arrival_time = 0;
};

struct RouteResult {
    bool found = false;

    int start_time = 0;
    int arrival_time = 0;
    int total_seconds = 0;

    int transfers = 0;
    int boardings = 0;
    int walk_seconds = 0;

    std::vector<RouteStep> steps;
};

RouteResult find_route(
    const GTFSData& data,
    const EventGraph& graph,
    const std::vector<std::string>& start_stop_ids,
    const std::vector<std::string>& target_stop_ids,
    int start_time_seconds,
    RouteMode mode
);

RouteResult find_latest_departure(
    const GTFSData& data,
    const EventGraph& graph,
    const std::vector<std::string>& start_stop_ids,
    const std::vector<std::string>& target_stop_ids,
    int latest_arrival_time_seconds,
    RouteMode inner_mode
);

void print_route_result(
    const GTFSData& data,
    const EventGraph& graph,
    const RouteResult& result
);
