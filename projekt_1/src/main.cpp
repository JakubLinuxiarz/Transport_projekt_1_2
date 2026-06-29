#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "monothread_djikstra.hpp"
#include "monothread_Astar.hpp"
#include "multithread_djikstra.hpp"
#include "multithread_Astar.hpp"
#include "trace.hpp"

namespace fs = std::filesystem;

struct City {
    std::string name;
    std::string place;
    double lat{};
    double lon{};
};

static double distance2(double lat1, double lon1, double lat2, double lon2) {
    double a = lat1 - lat2;
    double b = lon1 - lon2;
    return a * a + b * b;
}

static uint32_t nearest_graph_node(const LoadedGraph& g, const City& city) {
    double best_score = std::numeric_limits<double>::infinity();
    uint32_t best_id = UINT32_MAX;

    for (uint32_t i = 0; i < g.nodes.size(); i++) {
        uint64_t out_deg = g.offsets[i + 1] - g.offsets[i];
        uint64_t in_deg = g.reverse_offsets[i + 1] - g.reverse_offsets[i];

        if (out_deg == 0 || in_deg == 0) continue;

        double d = distance2(city.lat, city.lon, g.nodes[i].lat, g.nodes[i].lon);
        double degree_bonus = static_cast<double>(std::min<uint64_t>(out_deg + in_deg, 20));
        double score = d / (1.0 + 0.03 * degree_bonus);

        if (score < best_score) {
            best_score = score;
            best_id = i;
        }
    }

    return best_id;
}

enum class Method {
    Exit = 0,
    BidirDijkstra = 1,
    BidirAstar = 2,
    MultithreadDijkstra = 3,
    MultithreadAstar = 4,
    All = 5
};

static std::string stem_name(const fs::path& p) {
    std::string name = p.filename().string();

    if (name.size() >= 8 && name.substr(name.size() - 8) == ".osm.pbf") {
        name.resize(name.size() - 8);
    }

    return name;
}

static bool ask_yes_no(const std::string& q) {
    std::string a;
    std::cout << q << " [t/n]: ";
    std::cin >> a;

    return a == "t" || a == "T" || a == "tak" || a == "TAK";
}

static std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> cols;
    std::string current;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];

        if (c == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                current += '"';
                i++;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (c == ',' && !in_quotes) {
            cols.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }

    cols.push_back(current);
    return cols;
}

static std::vector<City> load_cities(const fs::path& csv) {
    std::vector<City> cities;
    std::ifstream in(csv);

    if (!in) {
        std::cerr << "Nie moge otworzyc pliku: " << csv << "\n";
        return cities;
    }

    std::string line;
    std::getline(in, line);

    while (std::getline(in, line)) {
        auto cols = parse_csv_line(line);

        if (cols.size() < 5) continue;

        try {
            City c;
            c.name = cols[1];
            c.place = cols[2];
            c.lat = std::stod(cols[3]);
            c.lon = std::stod(cols[4]);
            cities.push_back(c);
        } catch (...) {
        }
    }

    return cities;
}

static City choose_city(const std::vector<City>& cities, const std::string& label) {
    while (true) {
        std::cout << "\nPodaj " << label << " miasto / wieś / fragment nazwy: ";

        std::string query;
        std::cin >> std::ws;
        std::getline(std::cin, query);
        query = lower(query);

        std::vector<size_t> matches;

        for (size_t i = 0; i < cities.size(); i++) {
            std::string city_name = lower(cities[i].name);

            if (city_name.rfind(query, 0) == 0) {
                matches.push_back(i);
            }

            if (matches.size() >= 20) break;
        }

        if (matches.empty()) {
            std::cout << "Brak dopasowan.\n";
            continue;
        }

        std::cout << "\nDopasowania:\n";
        std::cout << "0. Szukaj ponownie\n";

        for (size_t i = 0; i < matches.size(); i++) {
            const City& c = cities[matches[i]];

            std::cout << (i + 1)
                      << ". " << c.name
                      << " ; " << c.place
                      << " ; " << c.lat
                      << ", " << c.lon
                      << "\n";
        }

        std::cout << "\nWybierz numer: ";

        int choice;
        std::cin >> choice;

        if (choice == 0) continue;

        if (choice >= 1 && choice <= static_cast<int>(matches.size())) {
            return cities[matches[choice - 1]];
        }

        std::cout << "Nieprawidlowy numer.\n";
    }
}

static Method choose_method() {
    std::cout << "\nWybierz metodę obliczania trasy:\n\n";

    std::cout << "0. Wyjście\n";
    std::cout << "1. Dwukierunkowa Dijkstra\n";
    std::cout << "2. Dwukierunkowe A*\n";
    std::cout << "3. Wielowątkowa Dijkstra\n";
    std::cout << "4. Wielowątkowe A*\n";
    std::cout << "5. Wszystkie metody\n";

    std::cout << "\nPodaj nr: ";

    int choice;
    std::cin >> choice;

    if (choice < 0 || choice > 5) return Method::Exit;

    return static_cast<Method>(choice);
}

static void add_trace_run(
    std::vector<TraceRun>& trace_runs,
    const std::string& name,
    float seconds,
    float meters,
    const std::vector<uint32_t>& path,
    std::vector<TraceEvent>&& events
) {
    TraceRun tr;
    tr.name = name;
    tr.seconds = seconds;
    tr.meters = meters;
    tr.path = path;
    tr.events = std::move(events);
    trace_runs.push_back(std::move(tr));
}

int main() {
    std::vector<fs::path> maps;

    if (!fs::exists("assets")) {
        std::cerr << "Brak katalogu assets\n";
        return 1;
    }

    for (const auto& entry : fs::directory_iterator("assets")) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();

        if (filename.size() >= 8 &&
            filename.substr(filename.size() - 8) == ".osm.pbf") {
            maps.push_back(entry.path());
        }
    }

    if (maps.empty()) {
        std::cout << "Brak map .osm.pbf w assets/\n";
        return 0;
    }

    std::cout << "Dostepne mapy:\n\n";
    std::cout << "0. Exit\n";

    for (size_t i = 0; i < maps.size(); i++) {
        std::cout << (i + 1) << ". " << maps[i].filename().string() << "\n";
    }

    std::cout << "\nPodaj nr: ";

    int map_choice;
    std::cin >> map_choice;

    if (map_choice == 0) return 0;

    if (map_choice < 1 || map_choice > static_cast<int>(maps.size())) {
        std::cout << "Nieprawidlowy numer.\n";
        return 1;
    }

    fs::path map_path = maps[map_choice - 1];
    std::string map_name = stem_name(map_path);
    fs::path data_dir = fs::path("data") / map_name;

    bool has_preprocessed =
        fs::exists(data_dir / "nodes.bin") &&
        fs::exists(data_dir / "edges.bin") &&
        fs::exists(data_dir / "offsets.bin") &&
        fs::exists(data_dir / "cities.csv");

    if (!has_preprocessed) {
        std::cout << "\nBrak przetworzonych danych.\n";
        fs::create_directories(data_dir);

        std::string cmd =
            "./bin/preprocess \"" +
            map_path.string() +
            "\" \"" +
            data_dir.string() +
            "\"";

        if (std::system(cmd.c_str()) != 0) {
            std::cout << "Preprocessing nie powiodl sie.\n";
            return 1;
        }
    } else {
        if (!ask_yes_no("Znaleziono przetworzone dane. Czy skorzystac z nich?")) {
            std::string cmd =
                "./bin/preprocess \"" +
                map_path.string() +
                "\" \"" +
                data_dir.string() +
                "\"";

            if (std::system(cmd.c_str()) != 0) {
                std::cout << "Preprocessing nie powiodl sie.\n";
                return 1;
            }
        }
    }

    auto cities = load_cities(data_dir / "cities.csv");

    std::cout << "\nWczytano miejscowosci: " << cities.size() << "\n";

    City from = choose_city(cities, "źródłowe");
    City to = choose_city(cities, "docelowe");

    Method method = choose_method();

    if (method == Method::Exit) return 0;

    std::cout << "\nWczytuję graf...\n";
    LoadedGraph graph = load_graph_for_dijkstra(data_dir.string());

    std::cout << "Szukam najbliższych wierzchołków grafu...\n";
    uint32_t start_node = nearest_graph_node(graph, from);
    uint32_t target_node = nearest_graph_node(graph, to);

    if (start_node == UINT32_MAX || target_node == UINT32_MAX) {
        std::cout << "Nie znaleziono poprawnego node'a startowego albo końcowego.\n";
        return 1;
    }

    std::cout << "Start node: " << start_node
              << " out=" << (graph.offsets[start_node + 1] - graph.offsets[start_node])
              << " in=" << (graph.reverse_offsets[start_node + 1] - graph.reverse_offsets[start_node])
              << " lat=" << graph.nodes[start_node].lat
              << " lon=" << graph.nodes[start_node].lon
              << "\n";

    std::cout << "Target node: " << target_node
              << " out=" << (graph.offsets[target_node + 1] - graph.offsets[target_node])
              << " in=" << (graph.reverse_offsets[target_node + 1] - graph.reverse_offsets[target_node])
              << " lat=" << graph.nodes[target_node].lat
              << " lon=" << graph.nodes[target_node].lon
              << "\n";

    std::vector<TraceRun> trace_runs;

    auto run_dijkstra = [&]() {
        std::cout << "Uruchamiam Dwukierunkowa Dijkstra...\n";

        auto t1 = std::chrono::high_resolution_clock::now();
        std::vector<TraceEvent> events;
        DijkstraResult r = bidirectional_dijkstra(graph, start_node, target_node, &events);
        auto t2 = std::chrono::high_resolution_clock::now();

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

        if (!r.found) {
            std::cout << "Nie znaleziono trasy.\n";
            return;
        }

        std::cout << "\n[Dijkstra]\n";
        std::cout << "Czas obliczeń: " << ms << " ms\n";
        std::cout << "Czas trasy: " << r.seconds / 60.0f << " min\n";
        std::cout << "Długość trasy: " << r.meters / 1000.0f << " km\n";
        std::cout << "Liczba wierzchołków trasy: " << r.path.size() << "\n";
        std::cout << "Odwiedzone przód: " << r.visited_forward << "\n";
        std::cout << "Odwiedzone tył: " << r.visited_backward << "\n";

        add_trace_run(trace_runs, "Dijkstra", r.seconds, r.meters, r.path, std::move(events));
    };

    auto run_astar = [&]() {
        std::cout << "Uruchamiam Dwukierunkowe A*...\n";

        auto t1 = std::chrono::high_resolution_clock::now();
        std::vector<TraceEvent> events;
        AStarResult r = bidirectional_astar(graph, start_node, target_node, &events);
        auto t2 = std::chrono::high_resolution_clock::now();

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

        if (!r.found) {
            std::cout << "Nie znaleziono trasy.\n";
            return;
        }

        std::cout << "\n[A*]\n";
        std::cout << "Czas obliczeń: " << ms << " ms\n";
        std::cout << "Czas trasy: " << r.seconds / 60.0f << " min\n";
        std::cout << "Długość trasy: " << r.meters / 1000.0f << " km\n";
        std::cout << "Liczba wierzchołków trasy: " << r.path.size() << "\n";
        std::cout << "Odwiedzone przód: " << r.visited_forward << "\n";
        std::cout << "Odwiedzone tył: " << r.visited_backward << "\n";

        add_trace_run(trace_runs, "A*", r.seconds, r.meters, r.path, std::move(events));
    };

    auto run_multithread_dijkstra = [&]() {
        std::cout << "Uruchamiam Wielowątkowa Dwukierunkowa Dijkstra...\n";

        auto t1 = std::chrono::high_resolution_clock::now();

        std::vector<TraceEvent> events;
        MultithreadDijkstraResult r =
            multithread_bidirectional_dijkstra(graph, start_node, target_node, 1000, &events);

        auto t2 = std::chrono::high_resolution_clock::now();

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

        if (!r.found) {
            std::cout << "Nie znaleziono trasy.\n";
            return;
        }

        std::cout << "\n[Wielowątkowa Dijkstra]\n";
        std::cout << "Czas obliczeń: " << ms << " ms\n";
        std::cout << "Czas trasy: " << r.seconds / 60.0f << " min\n";
        std::cout << "Długość trasy: " << r.meters / 1000.0f << " km\n";
        std::cout << "Liczba wierzchołków trasy: " << r.path.size() << "\n";
        std::cout << "Odwiedzone przód: " << r.visited_forward << "\n";
        std::cout << "Odwiedzone tył: " << r.visited_backward << "\n";

        add_trace_run(trace_runs, "MT Dijkstra", r.seconds, r.meters, r.path, std::move(events));
    };

    auto run_multithread_astar = [&]() {
        std::cout << "Uruchamiam Wielowątkowe A*...\n";

        auto t1 = std::chrono::high_resolution_clock::now();

        std::vector<TraceEvent> events;
        MultithreadAStarResult r =
            multithread_bidirectional_astar(graph, start_node, target_node, &events, 1000);

        auto t2 = std::chrono::high_resolution_clock::now();

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

        if (!r.found) {
            std::cout << "Nie znaleziono trasy.\n";
            return;
        }

        std::cout << "\n[Wielowątkowe A*]\n";
        std::cout << "Czas obliczeń: " << ms << " ms\n";
        std::cout << "Czas trasy: " << r.seconds / 60.0f << " min\n";
        std::cout << "Długość trasy: " << r.meters / 1000.0f << " km\n";
        std::cout << "Liczba wierzchołków trasy: " << r.path.size() << "\n";
        std::cout << "Odwiedzone przód: " << r.visited_forward << "\n";
        std::cout << "Odwiedzone tył: " << r.visited_backward << "\n";

        add_trace_run(trace_runs, "MT A*", r.seconds, r.meters, r.path, std::move(events));
    };

    switch (method) {
        case Method::Exit:
            return 0;

        case Method::BidirDijkstra:
            run_dijkstra();
            break;

        case Method::BidirAstar:
            run_astar();
            break;

        case Method::MultithreadDijkstra:
            run_multithread_dijkstra();
            break;

        case Method::MultithreadAstar:
            run_multithread_astar();
            break;

        case Method::All:
            run_dijkstra();
            std::cout << "\n-----------------------------\n";
            run_astar();
            std::cout << "\n-----------------------------\n";
            run_multithread_dijkstra();
            std::cout << "\n-----------------------------\n";
            run_multithread_astar();
            break;
    }

    if (!trace_runs.empty()) {
        fs::path trace_path = data_dir / "last_trace.bin";

        save_trace_file(
            trace_path.string(),
            data_dir.string(),
            from.name,
            to.name,
            start_node,
            target_node,
            trace_runs
        );

        std::cout << "\nZapisano trace: " << trace_path << "\n";
    }

    std::cout << "\nTrasa:\n"
              << from.name
              << " -> "
              << to.name
              << "\n";

    return 0;
}
