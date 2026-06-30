#pragma once

#include "gtfs.hpp"
#include "event_graph.hpp"
#include "router.hpp"

#include <SFML/Graphics.hpp>
#include <string>
#include <SFML/System/String.hpp>
#include <vector>
#include <map>
#include <unordered_set>
#include <locale>

struct StopGroupChoice {
    std::string display_name;
    std::vector<std::string> stop_ids;
};

class GuiApp {
public:
    GuiApp(
        const GTFSData& data,
        const EventGraph& graph
    );

    void run();

private:
    const GTFSData& data;
    EventGraph graph;
    std::string graph_date;
    bool graph_ready = false;

    sf::RenderWindow window;
    sf::Font font;

    std::map<std::string, std::vector<std::string>> stop_groups;
    std::vector<StopGroupChoice> start_matches;
    std::vector<StopGroupChoice> target_matches;

    std::vector<std::string> selected_start;
    std::vector<std::string> selected_target;

    sf::String start_text;
    sf::String target_text;
    sf::String date_text;
    sf::String time_text;
    sf::String latest_arrival_text;

    int active_field = 0;
    int mode = 1;

    bool calendar_open = false;
    std::unordered_set<std::string> available_dates;

    int calendar_year = 2026;
    int calendar_month = 6;

    std::string result_text;

    void build_stop_groups();
    void build_available_dates();
    void update_matches();

    void handle_event(const sf::Event& event);
    void handle_text_entered(sf::Uint32 unicode);
    void handle_key_pressed(sf::Keyboard::Key key);
    void handle_mouse_pressed(int x, int y);

    void calculate_route();

    void draw();
    void draw_textbox(
        float x,
        float y,
        float w,
        float h,
        const std::string& label,
        const sf::String& value,
        bool active
    );

    void draw_button(
        float x,
        float y,
        float w,
        float h,
        const std::string& label
    );

    void draw_time_spinner(
        float x,
        float y,
        const std::string& label,
        const sf::String& value,
        bool active
    );

    void draw_calendar();

    bool point_in_rect(
        int px,
        int py,
        float x,
        float y,
        float w,
        float h
    ) const;

    void adjust_time(sf::String& time_value, int delta_minutes);
    void set_calendar_from_date();
    std::string make_date_string(int year, int month, int day) const;
    int days_in_month(int year, int month) const;
    int day_of_week(int year, int month, int day) const;
    bool is_date_available(int year, int month, int day) const;

    std::string route_result_to_string(
        const RouteResult& result
    ) const;
};
