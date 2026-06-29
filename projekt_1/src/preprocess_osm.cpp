#include "graph.hpp"

#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

static constexpr double EARTH_RADIUS_M = 6371000.0;

struct TempEdge {
    uint32_t from;
    uint32_t to;
    float seconds;
    float meters;
};

struct City {
    int64_t osm_id;
    std::string name;
    std::string place;
    double lat;
    double lon;
};

class DSU {
public:
    std::vector<uint32_t> parent;
    std::vector<uint32_t> size;

    explicit DSU(uint32_t n) {
        parent.resize(n);
        size.assign(n, 1);
        std::iota(parent.begin(), parent.end(), 0);
    }

    uint32_t find(uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }

    void unite(uint32_t a, uint32_t b) {
        uint32_t ra = find(a);
        uint32_t rb = find(b);

        if (ra == rb) return;

        if (size[ra] < size[rb]) {
            std::swap(ra, rb);
        }

        parent[rb] = ra;
        size[ra] += size[rb];
    }
};

static double deg2rad(double deg) {
    return deg * M_PI / 180.0;
}

static double haversine_m(double lat1, double lon1, double lat2, double lon2) {
    double dlat = deg2rad(lat2 - lat1);
    double dlon = deg2rad(lon2 - lon1);

    lat1 = deg2rad(lat1);
    lat2 = deg2rad(lat2);

    double a =
        std::sin(dlat / 2) * std::sin(dlat / 2) +
        std::cos(lat1) * std::cos(lat2) *
        std::sin(dlon / 2) * std::sin(dlon / 2);

    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return EARTH_RADIUS_M * c;
}

static bool is_allowed_highway(const char* value) {
    if (!value) return false;

    std::string h = value;

    return h == "motorway" ||
           h == "motorway_link" ||
           h == "trunk" ||
           h == "trunk_link" ||
           h == "primary" ||
           h == "primary_link" ||
           h == "secondary" ||
           h == "secondary_link" ||
           h == "tertiary" ||
           h == "tertiary_link" ||
           h == "unclassified" ||
           h == "residential" ||
           h == "living_street" ||
           h == "service" ||
           h == "road" ||
           h == "track";
}

static bool is_bad_service(const char* value) {
    return false;
}

static bool is_place_for_search(const char* value) {
    if (!value) return false;

    std::string p = value;

    return p == "city" ||
           p == "town" ||
           p == "village" ||
           p == "suburb" ||
           p == "neighbourhood";
}

static float default_speed_kmh(const std::string& h) {
    if (h == "motorway") return 140.0f;
    if (h == "motorway_link") return 60.0f;

    if (h == "trunk") return 120.0f;
    if (h == "trunk_link") return 60.0f;

    if (h == "primary") return 90.0f;
    if (h == "primary_link") return 50.0f;

    if (h == "secondary") return 70.0f;
    if (h == "secondary_link") return 50.0f;

    if (h == "tertiary") return 50.0f;
    if (h == "tertiary_link") return 40.0f;

    if (h == "unclassified") return 50.0f;
    if (h == "residential") return 50.0f;
    if (h == "living_street") return 20.0f;
    if (h == "service") return 20.0f;
    if (h == "road") return 50.0f;

    return 10.0f;
}

static float parse_maxspeed(const char* value, const std::string& highway) {
    if (!value) return default_speed_kmh(highway);

    std::string s = value;
    std::string number;

    for (char c : s) {
        if ((c >= '0' && c <= '9') || c == '.') {
            number += c;
        } else if (!number.empty()) {
            break;
        }
    }

    if (number.empty()) {
        return default_speed_kmh(highway);
    }

    try {
        float speed = std::stof(number);

        if (s.find("mph") != std::string::npos) {
            speed *= 1.60934f;
        }

        if (speed < 5.0f || speed > 160.0f) {
            return default_speed_kmh(highway);
        }

        return speed;
    } catch (...) {
        return default_speed_kmh(highway);
    }
}

static bool is_oneway_forward(const char* value) {
    if (!value) return false;

    std::string s = value;
    return s == "yes" || s == "true" || s == "1";
}

static bool is_oneway_reverse(const char* value) {
    if (!value) return false;

    std::string s = value;
    return s == "-1";
}

class PreprocessHandler : public osmium::handler::Handler {
public:
    std::vector<GraphNode> nodes;
    std::vector<TempEdge> edges;
    std::vector<City> cities;

    std::unordered_map<int64_t, uint32_t> osm_node_to_internal;

    uint64_t nodes_seen = 0;
    uint64_t ways_seen = 0;
    uint64_t ways_used = 0;
    uint64_t edges_created = 0;
    uint64_t city_nodes = 0;

    uint32_t get_or_create_node(int64_t osm_id, const osmium::Location& loc) {
        auto it = osm_node_to_internal.find(osm_id);
        if (it != osm_node_to_internal.end()) {
            return it->second;
        }

        uint32_t internal_id = static_cast<uint32_t>(nodes.size());

        GraphNode node;
        node.lat = loc.lat_without_check();
        node.lon = loc.lon_without_check();

        nodes.push_back(node);
        osm_node_to_internal[osm_id] = internal_id;

        return internal_id;
    }

    void node(const osmium::Node& node) {
        nodes_seen++;

        const char* place = node.tags()["place"];
        const char* name = node.tags()["name"];

        if (!place || !name || !node.location().valid()) {
            return;
        }

        if (!is_place_for_search(place)) {
            return;
        }

        cities.push_back(City{
            static_cast<int64_t>(node.id()),
            name,
            place,
            node.location().lat_without_check(),
            node.location().lon_without_check()
        });

        city_nodes++;
    }

    void way(const osmium::Way& way) {
        ways_seen++;

        const char* highway_c = way.tags()["highway"];
        if (!is_allowed_highway(highway_c)) {
            return;
        }

        std::string highway = highway_c;

        if (highway == "service" && is_bad_service(way.tags()["service"])) {
            return;
        }

        const char* access = way.tags()["access"];
        const char* motor_vehicle = way.tags()["motor_vehicle"];
        const char* motorcar = way.tags()["motorcar"];

        if ((access && std::string(access) == "no") ||
            (motor_vehicle && std::string(motor_vehicle) == "no") ||
            (motorcar && std::string(motorcar) == "no")) {
            return;
        }

        const auto& refs = way.nodes();
        if (refs.size() < 2) {
            return;
        }

        float speed_kmh = parse_maxspeed(way.tags()["maxspeed"], highway);
        float speed_mps = speed_kmh * 1000.0f / 3600.0f;

        const char* oneway = way.tags()["oneway"];

        bool forward_only = is_oneway_forward(oneway);
        bool reverse_only = is_oneway_reverse(oneway);

        ways_used++;

        for (size_t i = 1; i < refs.size(); i++) {
            const auto& a = refs[i - 1];
            const auto& b = refs[i];

            if (!a.location().valid() || !b.location().valid()) {
                continue;
            }

            uint32_t from = get_or_create_node(static_cast<int64_t>(a.ref()), a.location());
            uint32_t to = get_or_create_node(static_cast<int64_t>(b.ref()), b.location());

            const GraphNode& na = nodes[from];
            const GraphNode& nb = nodes[to];

            double meters_double = haversine_m(na.lat, na.lon, nb.lat, nb.lon);

            if (meters_double <= 0.01) {
                continue;
            }

            float meters = static_cast<float>(meters_double);
            float seconds = meters / speed_mps;

            if (reverse_only) {
                edges.push_back(TempEdge{to, from, seconds, meters});
                edges_created++;
            } else if (forward_only) {
                edges.push_back(TempEdge{from, to, seconds, meters});
                edges_created++;
            } else {
                edges.push_back(TempEdge{from, to, seconds, meters});
                edges.push_back(TempEdge{to, from, seconds, meters});
                edges_created += 2;
            }
        }
    }
};

static uint32_t find_largest_component_root(
    uint32_t nodes_count,
    const std::vector<TempEdge>& edges
) {
    DSU dsu(nodes_count);

    std::cout << "Liczenie składowych Union-Find...\n";

    for (size_t i = 0; i < edges.size(); i++) {
        dsu.unite(edges[i].from, edges[i].to);

        if (i > 0 && i % 5000000 == 0) {
            std::cout << "  przetworzono krawędzie: " << i << " / " << edges.size() << "\n";
        }
    }

    std::unordered_map<uint32_t, uint32_t> comp_size;
    comp_size.reserve(nodes_count / 2);

    uint32_t best_root = 0;
    uint32_t best_size = 0;

    for (uint32_t v = 0; v < nodes_count; v++) {
        uint32_t r = dsu.find(v);
        uint32_t s = ++comp_size[r];

        if (s > best_size) {
            best_size = s;
            best_root = r;
        }
    }

    std::cout << "Liczba składowych: " << comp_size.size() << "\n";
    std::cout << "Największa składowa: root=" << best_root
              << " size=" << best_size
              << " / " << nodes_count
              << " (" << (100.0 * best_size / nodes_count) << "%)\n";

    return best_root;
}

static void filter_largest_component(
    std::vector<GraphNode>& nodes,
    std::vector<TempEdge>& edges,
    uint32_t largest_root
) {
    const uint32_t old_n = static_cast<uint32_t>(nodes.size());

    DSU dsu(old_n);

    for (const TempEdge& e : edges) {
        dsu.unite(e.from, e.to);
    }

    std::vector<uint32_t> old_to_new(old_n, UINT32_MAX);
    std::vector<GraphNode> new_nodes;
    new_nodes.reserve(old_n);

    for (uint32_t old_id = 0; old_id < old_n; old_id++) {
        if (dsu.find(old_id) == largest_root) {
            uint32_t new_id = static_cast<uint32_t>(new_nodes.size());
            old_to_new[old_id] = new_id;
            new_nodes.push_back(nodes[old_id]);
        }
    }

    std::vector<TempEdge> new_edges;
    new_edges.reserve(edges.size());

    for (const TempEdge& e : edges) {
        uint32_t nf = old_to_new[e.from];
        uint32_t nt = old_to_new[e.to];

        if (nf != UINT32_MAX && nt != UINT32_MAX) {
            new_edges.push_back(TempEdge{nf, nt, e.seconds, e.meters});
        }
    }

    std::cout << "Po odrzuceniu małych składowych:\n";
    std::cout << "  nodes: " << nodes.size() << " -> " << new_nodes.size() << "\n";
    std::cout << "  edges: " << edges.size() << " -> " << new_edges.size() << "\n";

    nodes.swap(new_nodes);
    edges.swap(new_edges);
}

template <typename T>
static void write_vector_binary(const fs::path& path, const std::vector<T>& vec) {
    std::ofstream out(path, std::ios::binary);

    if (!out) {
        throw std::runtime_error("Nie mogę zapisać pliku: " + path.string());
    }

    uint64_t size = vec.size();

    out.write(reinterpret_cast<const char*>(&size), sizeof(size));

    if (!vec.empty()) {
        out.write(reinterpret_cast<const char*>(vec.data()), sizeof(T) * vec.size());
    }
}

static void write_cities_csv(const fs::path& path, const std::vector<City>& cities) {
    std::ofstream out(path);

    if (!out) {
        throw std::runtime_error("Nie mogę zapisać pliku: " + path.string());
    }

    out << "osm_id,name,place,lat,lon\n";

    for (const City& c : cities) {
        out << c.osm_id << ",\"";

        for (char ch : c.name) {
            if (ch == '"') {
                out << "\"\"";
            } else {
                out << ch;
            }
        }

        out << "\"," << c.place << "," << c.lat << "," << c.lon << "\n";
    }
}

static void save_graph_csr(
    const fs::path& output_dir,
    const std::vector<GraphNode>& nodes,
    const std::vector<TempEdge>& temp_edges,
    const std::vector<City>& cities,
    uint64_t nodes_seen,
    uint64_t ways_seen,
    uint64_t ways_used,
    uint64_t edges_created,
    uint32_t largest_component_root
) {
    fs::create_directories(output_dir);

    std::vector<uint64_t> offsets(nodes.size() + 1, 0);

    for (const TempEdge& e : temp_edges) {
        offsets[e.from + 1]++;
    }

    for (size_t i = 1; i < offsets.size(); i++) {
        offsets[i] += offsets[i - 1];
    }

    std::vector<GraphEdge> edges(temp_edges.size());
    std::vector<uint64_t> cursor = offsets;

    for (const TempEdge& e : temp_edges) {
        uint64_t pos = cursor[e.from]++;
        edges[pos] = GraphEdge{
            e.to,
            e.seconds,
            e.meters
        };
    }

    write_vector_binary(output_dir / "nodes.bin", nodes);
    write_vector_binary(output_dir / "offsets.bin", offsets);
    write_vector_binary(output_dir / "edges.bin", edges);
    write_cities_csv(output_dir / "cities.csv", cities);

    std::ofstream meta(output_dir / "graph_meta.txt");

    meta << "nodes_seen=" << nodes_seen << "\n";
    meta << "ways_seen=" << ways_seen << "\n";
    meta << "ways_used=" << ways_used << "\n";
    meta << "graph_nodes=" << nodes.size() << "\n";
    meta << "graph_edges=" << edges.size() << "\n";
    meta << "cities=" << cities.size() << "\n";
    meta << "edges_created_before_filter=" << edges_created << "\n";
    meta << "largest_component_root_before_remap=" << largest_component_root << "\n";
    meta << "only_largest_component=true\n";
}

int main(int argc, char** argv) {
    std::string input = "assets/poland-latest.osm.pbf";
    std::string output = "data";

    if (argc >= 2) {
        input = argv[1];
    }

    if (argc >= 3) {
        output = argv[2];
    }

    std::cout << "Input:  " << input << "\n";
    std::cout << "Output: " << output << "\n\n";

    using index_type =
        osmium::index::map::SparseMemArray<
            osmium::unsigned_object_id_type,
            osmium::Location
        >;

    using location_handler_type =
        osmium::handler::NodeLocationsForWays<index_type>;

    index_type index;
    location_handler_type location_handler(index);
    location_handler.ignore_errors();

    PreprocessHandler handler;

    osmium::io::Reader reader(
        input,
        osmium::osm_entity_bits::node | osmium::osm_entity_bits::way
    );

    std::cout << "Przetwarzam OSM PBF...\n";

    osmium::apply(reader, location_handler, handler);

    reader.close();

    std::cout << "\nStatystyki przed filtrowaniem składowych:\n";
    std::cout << "Node'y przeczytane:       " << handler.nodes_seen << "\n";
    std::cout << "Way'e przeczytane:        " << handler.ways_seen << "\n";
    std::cout << "Way'e użyte jako drogi:   " << handler.ways_used << "\n";
    std::cout << "Wierzchołki grafu:        " << handler.nodes.size() << "\n";
    std::cout << "Krawędzie grafu:          " << handler.edges.size() << "\n";
    std::cout << "Miejscowości do szukania: " << handler.cities.size() << "\n\n";

    uint32_t largest_root = find_largest_component_root(
        static_cast<uint32_t>(handler.nodes.size()),
        handler.edges
    );

    filter_largest_component(
        handler.nodes,
        handler.edges,
        largest_root
    );

    std::cout << "\nZapisuję graf CSR...\n";

    save_graph_csr(
        output,
        handler.nodes,
        handler.edges,
        handler.cities,
        handler.nodes_seen,
        handler.ways_seen,
        handler.ways_used,
        handler.edges_created,
        largest_root
    );

    std::cout << "\nGotowe.\n";
    std::cout << "Zapisano:\n";
    std::cout << "  " << output << "/nodes.bin\n";
    std::cout << "  " << output << "/offsets.bin\n";
    std::cout << "  " << output << "/edges.bin\n";
    std::cout << "  " << output << "/cities.csv\n";
    std::cout << "  " << output << "/graph_meta.txt\n";

    return 0;
}
