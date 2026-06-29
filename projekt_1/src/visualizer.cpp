#include "monothread_djikstra.hpp"
#include "trace.hpp"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

struct ViewTransform {
    double min_lat = 999.0;
    double max_lat = -999.0;
    double min_lon = 999.0;
    double max_lon = -999.0;
    float x0 = 30.0f;
    float y0 = 30.0f;
    float w = 1540.0f;
    float h = 850.0f;

    sf::Vector2f project(const GraphNode& n) const {
        double x = (n.lon - min_lon) / (max_lon - min_lon);
        double y = (max_lat - n.lat) / (max_lat - min_lat);

        return {
            x0 + static_cast<float>(x) * w,
            y0 + static_cast<float>(y) * h
        };
    }
};

static uint64_t grid_key(sf::Vector2f p, float cell) {
    int x = static_cast<int>(p.x / cell);
    int y = static_cast<int>(p.y / cell);

    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint32_t>(y);
}

static uint64_t edge_key(uint64_t a, uint64_t b) {
    if (a > b) std::swap(a, b);
    return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
}

static ViewTransform make_transform(const LoadedGraph& g, float W, float H) {
    ViewTransform t;

    for (const auto& n : g.nodes) {
        t.min_lat = std::min(t.min_lat, n.lat);
        t.max_lat = std::max(t.max_lat, n.lat);
        t.min_lon = std::min(t.min_lon, n.lon);
        t.max_lon = std::max(t.max_lon, n.lon);
    }

    t.x0 = 30.0f;
    t.y0 = 30.0f;
    t.w = W - 60.0f;
    t.h = H - 130.0f;

    return t;
}

static sf::VertexArray build_edge_layer(
    const LoadedGraph& g,
    const ViewTransform& tr,
    uint32_t edge_stride
) {
    sf::VertexArray edges(sf::Lines);

    const float merge_cell = 3.0f;
    const float min_edge_px = 3.0f;

    std::unordered_set<uint64_t> drawn_edges;
    drawn_edges.reserve(2'000'000);

    for (uint64_t v = 0; v < g.nodes.size(); v++) {
        sf::Vector2f a = tr.project(g.nodes[v]);
        uint64_t ka = grid_key(a, merge_cell);

        for (uint64_t i = g.offsets[v]; i < g.offsets[v + 1]; i += edge_stride) {
            uint32_t to = g.edges[i].to;
            sf::Vector2f b = tr.project(g.nodes[to]);
            uint64_t kb = grid_key(b, merge_cell);

            if (ka == kb) continue;

            float dx = a.x - b.x;
            float dy = a.y - b.y;

            if (dx * dx + dy * dy < min_edge_px * min_edge_px) continue;

            uint64_t ek = edge_key(ka, kb);

            if (!drawn_edges.insert(ek).second) continue;

            edges.append(sf::Vertex(a, sf::Color(85, 85, 85, 45)));
            edges.append(sf::Vertex(b, sf::Color(85, 85, 85, 45)));
        }
    }

    return edges;
}

static sf::VertexArray build_path_layer(
    const LoadedGraph& g,
    const ViewTransform& tr,
    const TraceRun& run
) {
    sf::VertexArray path(sf::LinesStrip);

    for (uint32_t v : run.path) {
        if (v < g.nodes.size()) {
            path.append(sf::Vertex(tr.project(g.nodes[v]), sf::Color(255, 35, 35, 255)));
        }
    }

    return path;
}

static void reset_run_visuals(
    const LoadedGraph& g,
    const ViewTransform& tr,
    const TraceRun& run,
    sf::VertexArray& visited_f,
    sf::VertexArray& visited_b,
    sf::VertexArray& path_layer,
    std::unordered_set<uint64_t>& drawn_forward,
    std::unordered_set<uint64_t>& drawn_backward,
    size_t& step
) {
    step = 0;
    visited_f.clear();
    visited_b.clear();
    drawn_forward.clear();
    drawn_backward.clear();
    path_layer = build_path_layer(g, tr, run);
}

static void draw_hud(
    sf::RenderWindow& window,
    sf::Font& font,
    const TraceFile& trace,
    const TraceRun& run,
    size_t current_run,
    size_t step,
    uint32_t speed,
    bool playing,
    float H
) {
    sf::Text text;
    text.setFont(font);
    text.setCharacterSize(18);
    text.setFillColor(sf::Color::White);
    text.setPosition(20.0f, H - 95.0f);

    std::ostringstream ss;
    ss << "Algorytm: " << run.name
       << " (" << (current_run + 1) << "/" << trace.runs.size() << ")"
       << " | krok: " << step << "/" << run.events.size()
       << " | speed: " << speed
       << " | " << (playing ? "PLAY" : "PAUSE") << "\n"
       << "Trasa: " << trace.from_name << " -> " << trace.to_name
       << " | czas trasy: " << run.seconds / 60.0f << " min"
       << " | dlugosc: " << run.meters / 1000.0f << " km\n"
       << "Sterowanie: LPM drag, scroll/Z/X zoom, WASD ruch, SPACE pauza, TAB algorytm, R reset, strzalki speed";

    text.setString(ss.str());
    window.draw(text);
}

int main(int argc, char** argv) {
    std::string trace_path = "data/poland-latest/last_trace.bin";

    if (argc >= 2) {
        trace_path = argv[1];
    }

    std::cout << "Wczytuje trace: " << trace_path << "\n";
    TraceFile trace = load_trace_file(trace_path);

    if (trace.runs.empty()) {
        std::cerr << "Trace nie zawiera zadnych algorytmow.\n";
        return 1;
    }

    std::cout << "Wczytuje graf: " << trace.data_dir << "\n";
    LoadedGraph g = load_graph_for_dijkstra(trace.data_dir);

    const float W = 1600.0f;
    const float H = 1000.0f;

    sf::RenderWindow window(
        sf::VideoMode(static_cast<unsigned>(W), static_cast<unsigned>(H)),
        "Wizualizacja algorytmow trasowania"
    );

    window.setVerticalSyncEnabled(true);

    sf::View view(sf::FloatRect(0.0f, 0.0f, W, H));
    window.setView(view);

    ViewTransform tr = make_transform(g, W, H);

    uint32_t edge_stride = 8;

    std::cout << "Buduje warstwe krawedzi...\n";
    sf::VertexArray edge_layer = build_edge_layer(g, tr, edge_stride);

    size_t current_run = 0;
    size_t step = 0;
    bool playing = true;
    uint32_t speed = 300;

    bool dragging = false;
    sf::Vector2i last_mouse;

    sf::VertexArray visited_f(sf::Points);
    sf::VertexArray visited_b(sf::Points);
    sf::VertexArray path_layer = build_path_layer(g, tr, trace.runs[current_run]);

    std::unordered_set<uint64_t> drawn_forward;
    std::unordered_set<uint64_t> drawn_backward;

    drawn_forward.reserve(1'000'000);
    drawn_backward.reserve(1'000'000);

    const float visited_merge_cell = 2.5f;

    sf::Font font;
    font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");

    while (window.isOpen()) {
        sf::Event ev;

        while (window.pollEvent(ev)) {
            if (ev.type == sf::Event::Closed) {
                window.close();
            }

            if (ev.type == sf::Event::MouseButtonPressed &&
                ev.mouseButton.button == sf::Mouse::Left) {
                dragging = true;
                last_mouse = sf::Mouse::getPosition(window);
            }

            if (ev.type == sf::Event::MouseButtonReleased &&
                ev.mouseButton.button == sf::Mouse::Left) {
                dragging = false;
            }

            if (ev.type == sf::Event::MouseMoved && dragging) {
                sf::Vector2i now = sf::Mouse::getPosition(window);
                sf::Vector2f before = window.mapPixelToCoords(last_mouse, view);
                sf::Vector2f after = window.mapPixelToCoords(now, view);
                view.move(before - after);
                window.setView(view);
                last_mouse = now;
            }

            if (ev.type == sf::Event::MouseWheelScrolled) {
                sf::Vector2i pixel = sf::Mouse::getPosition(window);
                sf::Vector2f before = window.mapPixelToCoords(pixel, view);

                if (ev.mouseWheelScroll.delta > 0) view.zoom(0.85f);
                else view.zoom(1.15f);

                window.setView(view);

                sf::Vector2f after = window.mapPixelToCoords(pixel, view);
                view.move(before - after);
                window.setView(view);
            }

            if (ev.type == sf::Event::KeyPressed) {
                if (ev.key.code == sf::Keyboard::Space) playing = !playing;

                if (ev.key.code == sf::Keyboard::Tab) {
                    current_run = (current_run + 1) % trace.runs.size();

                    reset_run_visuals(
                        g,
                        tr,
                        trace.runs[current_run],
                        visited_f,
                        visited_b,
                        path_layer,
                        drawn_forward,
                        drawn_backward,
                        step
                    );
                }

                if (ev.key.code == sf::Keyboard::Right) speed *= 2;
                if (ev.key.code == sf::Keyboard::Left && speed > 10) speed /= 2;

                if (ev.key.code == sf::Keyboard::R) {
                    reset_run_visuals(
                        g,
                        tr,
                        trace.runs[current_run],
                        visited_f,
                        visited_b,
                        path_layer,
                        drawn_forward,
                        drawn_backward,
                        step
                    );
                }

                if (ev.key.code == sf::Keyboard::Z) {
                    view.zoom(0.85f);
                    window.setView(view);
                }

                if (ev.key.code == sf::Keyboard::X) {
                    view.zoom(1.15f);
                    window.setView(view);
                }
            }
        }

        float move = 20.0f * view.getSize().x / W;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::W)) {
            view.move(0.0f, -move);
            window.setView(view);
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::S)) {
            view.move(0.0f, move);
            window.setView(view);
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::A)) {
            view.move(-move, 0.0f);
            window.setView(view);
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::D)) {
            view.move(move, 0.0f);
            window.setView(view);
        }

        const TraceRun& run = trace.runs[current_run];

        if (playing) {
            for (uint32_t i = 0; i < speed && step < run.events.size(); i++, step++) {
                const TraceEvent& e = run.events[step];

                if (e.node >= g.nodes.size()) continue;

                sf::Vector2f p = tr.project(g.nodes[e.node]);
                uint64_t k = grid_key(p, visited_merge_cell);

                if (e.side == 0) {
                    if (drawn_forward.insert(k).second) {
                        visited_f.append(sf::Vertex(p, sf::Color(60, 140, 255, 180)));
                    }
                } else {
                    if (drawn_backward.insert(k).second) {
                        visited_b.append(sf::Vertex(p, sf::Color(255, 210, 40, 180)));
                    }
                }
            }
        }

        window.clear(sf::Color(12, 16, 18));

        window.setView(view);
        window.draw(edge_layer);
        window.draw(visited_f);
        window.draw(visited_b);

        if (step >= run.events.size()) {
            window.draw(path_layer);
        }

        sf::CircleShape start_circle(7.0f);
        start_circle.setOrigin(7.0f, 7.0f);
        start_circle.setFillColor(sf::Color(80, 255, 80));
        if (trace.start_node < g.nodes.size()) {
            start_circle.setPosition(tr.project(g.nodes[trace.start_node]));
            window.draw(start_circle);
        }

        sf::CircleShape target_circle(7.0f);
        target_circle.setOrigin(7.0f, 7.0f);
        target_circle.setFillColor(sf::Color(255, 70, 70));
        if (trace.target_node < g.nodes.size()) {
            target_circle.setPosition(tr.project(g.nodes[trace.target_node]));
            window.draw(target_circle);
        }

        window.setView(window.getDefaultView());

        draw_hud(
            window,
            font,
            trace,
            run,
            current_run,
            step,
            speed,
            playing,
            H
        );

        window.display();
    }

    return 0;
}
