#include "gtfs.hpp"
#include "csv.hpp"
#include "util.hpp"

#include <iostream>

static std::string get_cell(
    const CSVTable& table,
    const std::vector<std::string>& row,
    const std::string& column_name
) {
    auto it = table.column.find(column_name);

    if (it == table.column.end()) {
        return "";
    }

    int idx = it->second;

    if (idx < 0 || idx >= (int)row.size()) {
        return "";
    }

    return row[idx];
}

GTFSData load_gtfs_folder(
    const std::string& folder,
    const std::string& vehicle_type
) {
    GTFSData data;

    std::cout << "Wczytywanie GTFS z: " << folder << "\n";

    {
        CSVTable table = read_csv(folder + "/stops.txt");

        for (const auto& row : table.rows) {
            Stop stop;

            std::string raw_id = get_cell(table, row, "stop_id");

            if (raw_id.empty()) continue;

            stop.id = vehicle_type + ":" + raw_id;
            stop.name = get_cell(table, row, "stop_name");

            std::string lat = get_cell(table, row, "stop_lat");
            std::string lon = get_cell(table, row, "stop_lon");

            if (lat.empty() || lon.empty()) continue;

            stop.lat = std::stod(lat);
            stop.lon = std::stod(lon);

            data.stops[stop.id] = stop;
        }
    }

    {
        CSVTable table = read_csv(folder + "/routes.txt");

        for (const auto& row : table.rows) {
            std::string raw_id = get_cell(table, row, "route_id");

            if (raw_id.empty()) continue;

            Route route;
            route.id = vehicle_type + ":" + raw_id;
            route.short_name = get_cell(table, row, "route_short_name");
            route.long_name = get_cell(table, row, "route_long_name");
            route.type = vehicle_type;

            data.routes[route.id] = route;
        }
    }

    {
        CSVTable table = read_csv(folder + "/trips.txt");

        for (const auto& row : table.rows) {
            std::string raw_trip_id = get_cell(table, row, "trip_id");
            std::string raw_route_id = get_cell(table, row, "route_id");
            std::string raw_service_id = get_cell(table, row, "service_id");

            if (raw_trip_id.empty()) continue;

            Trip trip;
            trip.id = vehicle_type + ":" + raw_trip_id;
            trip.route_id = vehicle_type + ":" + raw_route_id;
            trip.service_id = vehicle_type + ":" + raw_service_id;
            trip.headsign = get_cell(table, row, "trip_headsign");

            data.trips[trip.id] = trip;
        }
    }

    {
        CSVTable table = read_csv(folder + "/calendar.txt");

        for (const auto& row : table.rows) {
            std::string raw_service_id = get_cell(table, row, "service_id");

            if (raw_service_id.empty()) continue;

            CalendarService s;
            s.service_id = vehicle_type + ":" + raw_service_id;

            s.monday = get_cell(table, row, "monday") == "1";
            s.tuesday = get_cell(table, row, "tuesday") == "1";
            s.wednesday = get_cell(table, row, "wednesday") == "1";
            s.thursday = get_cell(table, row, "thursday") == "1";
            s.friday = get_cell(table, row, "friday") == "1";
            s.saturday = get_cell(table, row, "saturday") == "1";
            s.sunday = get_cell(table, row, "sunday") == "1";

            s.start_date = get_cell(table, row, "start_date");
            s.end_date = get_cell(table, row, "end_date");

            data.calendar[s.service_id] = s;
        }
    }

    {
        CSVTable table = read_csv(folder + "/calendar_dates.txt");

        for (const auto& row : table.rows) {
            std::string raw_service_id = get_cell(table, row, "service_id");

            if (raw_service_id.empty()) continue;

            CalendarDateException ex;
            ex.service_id = vehicle_type + ":" + raw_service_id;
            ex.date = get_cell(table, row, "date");

            std::string type = get_cell(table, row, "exception_type");
            ex.exception_type = type.empty() ? 0 : std::stoi(type);

            if (!ex.date.empty()) {
                data.calendar_dates.push_back(ex);
            }
        }
    }

    {
        CSVTable table = read_csv(folder + "/stop_times.txt");

        for (const auto& row : table.rows) {
            std::string raw_trip_id = get_cell(table, row, "trip_id");
            std::string raw_stop_id = get_cell(table, row, "stop_id");

            if (raw_trip_id.empty() || raw_stop_id.empty()) continue;

            std::string arr = get_cell(table, row, "arrival_time");
            std::string dep = get_cell(table, row, "departure_time");
            std::string seq = get_cell(table, row, "stop_sequence");

            if (arr.empty() || dep.empty()) continue;

            StopTime st;
            st.trip_id = vehicle_type + ":" + raw_trip_id;
            st.stop_id = vehicle_type + ":" + raw_stop_id;
            st.arrival = parse_time_to_seconds(arr);
            st.departure = parse_time_to_seconds(dep);
            st.sequence = seq.empty() ? 0 : std::stoi(seq);

            data.stop_times.push_back(st);
        }
    }

    std::cout << "Wczytano:\n";
    std::cout << "  przystanki: " << data.stops.size() << "\n";
    std::cout << "  linie: " << data.routes.size() << "\n";
    std::cout << "  kursy: " << data.trips.size() << "\n";
    std::cout << "  stop_times: " << data.stop_times.size() << "\n";
    std::cout << "  calendar: " << data.calendar.size() << "\n";
    std::cout << "  calendar_dates: " << data.calendar_dates.size() << "\n";

    return data;
}

GTFSData merge_gtfs(
    const GTFSData& a,
    const GTFSData& b
) {
    GTFSData r = a;

    r.stops.insert(b.stops.begin(), b.stops.end());
    r.routes.insert(b.routes.begin(), b.routes.end());
    r.trips.insert(b.trips.begin(), b.trips.end());
    r.calendar.insert(b.calendar.begin(), b.calendar.end());

    r.calendar_dates.insert(
        r.calendar_dates.end(),
        b.calendar_dates.begin(),
        b.calendar_dates.end()
    );

    r.stop_times.insert(
        r.stop_times.end(),
        b.stop_times.begin(),
        b.stop_times.end()
    );

    return r;
}

std::string normalize_gtfs_date(const std::string& date) {
    if (date.size() == 8) {
        return date;
    }

    if (date.size() == 10) {
        return date.substr(0, 4) + date.substr(5, 2) + date.substr(8, 2);
    }

    return date;
}

int day_of_week_from_yyyymmdd(const std::string& s) {
    int y = std::stoi(s.substr(0, 4));
    int m = std::stoi(s.substr(4, 2));
    int d = std::stoi(s.substr(6, 2));

    if (m < 3) {
        m += 12;
        y--;
    }

    int K = y % 100;
    int J = y / 100;

    int h =
        (d +
         (13 * (m + 1)) / 5 +
         K +
         K / 4 +
         J / 4 +
         5 * J) % 7;

    return (h + 6) % 7;
}

bool service_runs_on_date(
    const GTFSData& data,
    const std::string& service_id,
    const std::string& date
) {
    std::string d = normalize_gtfs_date(date);

    for (const auto& ex : data.calendar_dates) {
        if (ex.service_id != service_id) continue;
        if (ex.date != d) continue;

        if (ex.exception_type == 1) return true;
        if (ex.exception_type == 2) return false;
    }

    auto it = data.calendar.find(service_id);

    if (it == data.calendar.end()) {
        return false;
    }

    const CalendarService& s = it->second;

    if (s.start_date.empty() || s.end_date.empty()) {
        return false;
    }

    if (d < s.start_date || d > s.end_date) {
        return false;
    }

    int dow = day_of_week_from_yyyymmdd(d);

    switch (dow) {
        case 0: return s.sunday;
        case 1: return s.monday;
        case 2: return s.tuesday;
        case 3: return s.wednesday;
        case 4: return s.thursday;
        case 5: return s.friday;
        case 6: return s.saturday;
    }

    return false;
}


bool gtfs_has_any_service_on_date(
    const GTFSData& data,
    const std::string& date
) {
    std::string d = normalize_gtfs_date(date);

    for (const auto& ex : data.calendar_dates) {
        if (ex.date == d && ex.exception_type == 1) {
            return true;
        }
    }

    for (const auto& [service_id, service] : data.calendar) {
        (void)service;

        if (service_runs_on_date(data, service_id, d)) {
            return true;
        }
    }

    return false;
}

std::string gtfs_first_available_date(
    const GTFSData& data
) {
    std::string best;

    for (const auto& ex : data.calendar_dates) {
        if (ex.exception_type != 1) {
            continue;
        }

        if (best.empty() || ex.date < best) {
            best = ex.date;
        }
    }

    for (const auto& [id, service] : data.calendar) {
        (void)id;

        if (!service.start_date.empty() &&
            (best.empty() || service.start_date < best))
        {
            best = service.start_date;
        }
    }

    return best;
}

std::string gtfs_last_available_date(
    const GTFSData& data
) {
    std::string best;

    for (const auto& ex : data.calendar_dates) {
        if (ex.exception_type != 1) {
            continue;
        }

        if (best.empty() || ex.date > best) {
            best = ex.date;
        }
    }

    for (const auto& [id, service] : data.calendar) {
        (void)id;

        if (!service.end_date.empty() &&
            (best.empty() || service.end_date > best))
        {
            best = service.end_date;
        }
    }

    return best;
}
