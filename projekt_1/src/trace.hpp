#pragma once

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct TraceEvent {
    uint32_t node;
    uint8_t side; // 0 forward, 1 backward
};

struct TraceRun {
    std::string name;
    float seconds = 0.0f;
    float meters = 0.0f;
    std::vector<uint32_t> path;
    std::vector<TraceEvent> events;
};

struct TraceFile {
    std::string data_dir;
    std::string from_name;
    std::string to_name;
    uint32_t start_node = UINT32_MAX;
    uint32_t target_node = UINT32_MAX;
    std::vector<TraceRun> runs;
};

inline void write_string(std::ofstream& out, const std::string& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    out.write(s.data(), n);
}

inline std::string read_string(std::ifstream& in) {
    uint32_t n = 0;
    in.read(reinterpret_cast<char*>(&n), sizeof(n));
    std::string s(n, '\0');
    if (n > 0) in.read(s.data(), n);
    return s;
}

inline void save_trace_file(
    const std::string& path,
    const std::string& data_dir,
    const std::string& from_name,
    const std::string& to_name,
    uint32_t start_node,
    uint32_t target_node,
    const std::vector<TraceRun>& runs
) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Nie moge zapisac trace: " + path);

    uint32_t magic = 0x43415254; // TRAC
    uint32_t version = 2;
    uint32_t runs_count = static_cast<uint32_t>(runs.size());

    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));

    write_string(out, data_dir);
    write_string(out, from_name);
    write_string(out, to_name);

    out.write(reinterpret_cast<const char*>(&start_node), sizeof(start_node));
    out.write(reinterpret_cast<const char*>(&target_node), sizeof(target_node));
    out.write(reinterpret_cast<const char*>(&runs_count), sizeof(runs_count));

    for (const TraceRun& r : runs) {
        write_string(out, r.name);

        out.write(reinterpret_cast<const char*>(&r.seconds), sizeof(r.seconds));
        out.write(reinterpret_cast<const char*>(&r.meters), sizeof(r.meters));

        uint64_t path_size = r.path.size();
        uint64_t events_size = r.events.size();

        out.write(reinterpret_cast<const char*>(&path_size), sizeof(path_size));
        if (path_size > 0) {
            out.write(reinterpret_cast<const char*>(r.path.data()), sizeof(uint32_t) * path_size);
        }

        out.write(reinterpret_cast<const char*>(&events_size), sizeof(events_size));
        if (events_size > 0) {
            out.write(reinterpret_cast<const char*>(r.events.data()), sizeof(TraceEvent) * events_size);
        }
    }
}

inline TraceFile load_trace_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Nie moge otworzyc trace: " + path);

    TraceFile f;
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t runs_count = 0;

    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));

    if (magic != 0x43415254) throw std::runtime_error("Zly plik trace: " + path);

    f.data_dir = read_string(in);
    f.from_name = read_string(in);
    f.to_name = read_string(in);

    in.read(reinterpret_cast<char*>(&f.start_node), sizeof(f.start_node));
    in.read(reinterpret_cast<char*>(&f.target_node), sizeof(f.target_node));
    in.read(reinterpret_cast<char*>(&runs_count), sizeof(runs_count));

    f.runs.resize(runs_count);

    for (TraceRun& r : f.runs) {
        r.name = read_string(in);

        in.read(reinterpret_cast<char*>(&r.seconds), sizeof(r.seconds));
        in.read(reinterpret_cast<char*>(&r.meters), sizeof(r.meters));

        uint64_t path_size = 0;
        uint64_t events_size = 0;

        in.read(reinterpret_cast<char*>(&path_size), sizeof(path_size));
        r.path.resize(path_size);
        if (path_size > 0) {
            in.read(reinterpret_cast<char*>(r.path.data()), sizeof(uint32_t) * path_size);
        }

        in.read(reinterpret_cast<char*>(&events_size), sizeof(events_size));
        r.events.resize(events_size);
        if (events_size > 0) {
            in.read(reinterpret_cast<char*>(r.events.data()), sizeof(TraceEvent) * events_size);
        }
    }

    return f;
}
