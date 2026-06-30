#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct Stop {
    std::string id;
    std::string name;
    double lat = 0.0;
    double lon = 0.0;
};

struct Route {
    std::string id;
    std::string short_name;
    std::string long_name;
    std::string type;
};

struct Trip {
    std::string id;
    std::string route_id;
    std::string service_id;
    std::string headsign;
};

struct StopTime {
    std::string trip_id;
    std::string stop_id;
    int arrival = 0;
    int departure = 0;
    int sequence = 0;
};

struct CalendarService {
    std::string service_id;

    bool monday = false;
    bool tuesday = false;
    bool wednesday = false;
    bool thursday = false;
    bool friday = false;
    bool saturday = false;
    bool sunday = false;

    std::string start_date;
    std::string end_date;
};

struct CalendarDateException {
    std::string service_id;
    std::string date;
    int exception_type = 0;
};

struct GTFSData {
    std::unordered_map<std::string, Stop> stops;
    std::unordered_map<std::string, Route> routes;
    std::unordered_map<std::string, Trip> trips;
    std::vector<StopTime> stop_times;

    std::unordered_map<std::string, CalendarService> calendar;
    std::vector<CalendarDateException> calendar_dates;
};

GTFSData load_gtfs_folder(const std::string& folder, const std::string& vehicle_type);
GTFSData merge_gtfs(const GTFSData& a, const GTFSData& b);

std::string normalize_gtfs_date(const std::string& date);
int day_of_week_from_yyyymmdd(const std::string& yyyymmdd);

bool service_runs_on_date(
    const GTFSData& data,
    const std::string& service_id,
    const std::string& date
);

bool gtfs_has_any_service_on_date(
    const GTFSData& data,
    const std::string& date
);

std::string gtfs_first_available_date(
    const GTFSData& data
);

std::string gtfs_last_available_date(
    const GTFSData& data
);
