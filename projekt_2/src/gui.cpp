#include "gui.hpp"
#include "util.hpp"

#include <sstream>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <codecvt>
#include <locale>


static sf::String utf8_to_sf(const std::string& s) {
    return sf::String::fromUtf8(s.begin(), s.end());
}

static std::string sf_to_utf8(const sf::String& s) {
    std::basic_string<sf::Uint8> bytes = s.toUtf8();
    return std::string(bytes.begin(), bytes.end());
}

GuiApp::GuiApp(
    const GTFSData& data_,
    const EventGraph& graph_
)
    : data(data_),
      graph(graph_),
      graph_date(""),
      graph_ready(false),
      window(sf::VideoMode(1200, 800), "Krakow MPK Router")
{
    window.setFramerateLimit(30);

    if (!font.loadFromFile("assets/DejaVuSans.ttf")) {
        std::cerr
            << "Brak fontu assets/DejaVuSans.ttf\n"
            << "cp /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf assets/\n";
    }

    date_text = "2026-06-30";
    time_text = "13:50";
    latest_arrival_text = "14:30";

    set_calendar_from_date();
    build_available_dates();
    build_stop_groups();
    update_matches();
}

void GuiApp::run() {
    while (window.isOpen()) {
        sf::Event event;

        while (window.pollEvent(event)) {
            handle_event(event);
        }

        draw();
    }
}


void GuiApp::build_available_dates() {
    available_dates.clear();

    for (const auto& ex : data.calendar_dates) {
        if (ex.exception_type == 1) {
            available_dates.insert(ex.date);
        }
    }
}

bool GuiApp::is_date_available(
    int year,
    int month,
    int day
) const {
    return available_dates.contains(
        normalize_gtfs_date(
            make_date_string(year, month, day)
        )
    );
}

void GuiApp::build_stop_groups() {
    stop_groups.clear();

    for (const auto& [id, stop] : data.stops) {
        stop_groups[normalize_name(stop.name)].push_back(id);
    }
}

void GuiApp::update_matches() {
    auto make_matches =
        [&](const std::string& query) {
            std::vector<StopGroupChoice> out;

            std::string q = normalize_name(query);

            if (q.empty()) {
                return out;
            }

            for (const auto& [name, ids] : stop_groups) {
                if (name.find(q) == std::string::npos) {
                    continue;
                }

                StopGroupChoice choice;
                choice.stop_ids = ids;
                choice.display_name =
                    data.stops.at(ids.front()).name;

                out.push_back(choice);

                if ((int)out.size() >= 6) {
                    break;
                }
            }

            return out;
        };

    start_matches = make_matches(start_text);
    target_matches = make_matches(target_text);
}

void GuiApp::handle_event(const sf::Event& event) {
    if (event.type == sf::Event::Closed) {
        window.close();
    } else if (event.type == sf::Event::TextEntered) {
        handle_text_entered(event.text.unicode);
    } else if (event.type == sf::Event::KeyPressed) {
        handle_key_pressed(event.key.code);
    } else if (event.type == sf::Event::MouseButtonPressed) {
        handle_mouse_pressed(
            event.mouseButton.x,
            event.mouseButton.y
        );
    }
}

void GuiApp::handle_text_entered(sf::Uint32 unicode) {
    if (calendar_open) {
        return;
    }

    sf::String* field = nullptr;

    if (active_field == 1) field = &start_text;
    if (active_field == 2) field = &target_text;

    if (!field) {
        return;
    }

    if (unicode == 8) {
        if (!field->isEmpty()) {
            field->erase(field->getSize() - 1, 1);
            update_matches();
        }

        return;
    }

    if (unicode == 13 || unicode == 9) {
        return;
    }

    if (unicode >= 32) {
        *field += unicode;
        update_matches();
    }
}

void GuiApp::handle_key_pressed(sf::Keyboard::Key key) {
    if (key == sf::Keyboard::Tab) {
        active_field++;
        if (active_field > 4) active_field = 1;
    }

    if (key == sf::Keyboard::Enter) {
        calculate_route();
    }

    if (active_field == 4 && key == sf::Keyboard::Up) {
        adjust_time(time_text, 5);
    }

    if (active_field == 4 && key == sf::Keyboard::Down) {
        adjust_time(time_text, -5);
    }
}

bool GuiApp::point_in_rect(
    int px,
    int py,
    float x,
    float y,
    float w,
    float h
) const {
    return
        px >= x &&
        px <= x + w &&
        py >= y &&
        py <= y + h;
}

void GuiApp::handle_mouse_pressed(int x, int y) {
    if (calendar_open) {
        float cx = 30;
        float cy = 225;

        if (point_in_rect(x, y, cx, cy, 280, 260)) {
            if (point_in_rect(x, y, cx + 10, cy + 10, 35, 28)) {
                calendar_month--;
                if (calendar_month < 1) {
                    calendar_month = 12;
                    calendar_year--;
                }
                return;
            }

            if (point_in_rect(x, y, cx + 235, cy + 10, 35, 28)) {
                calendar_month++;
                if (calendar_month > 12) {
                    calendar_month = 1;
                    calendar_year++;
                }
                return;
            }

            int first_dow = day_of_week(calendar_year, calendar_month, 1);
            int dim = days_in_month(calendar_year, calendar_month);

            float grid_x = cx + 15;
            float grid_y = cy + 75;
            float cell = 35;

            for (int d = 1; d <= dim; d++) {
                int idx = first_dow + d - 1;
                int row = idx / 7;
                int col = idx % 7;

                float bx = grid_x + col * cell;
                float by = grid_y + row * cell;

                if (point_in_rect(x, y, bx, by, 30, 28)) {
                    if (!is_date_available(calendar_year, calendar_month, d)) {
                        return;
                    }

                    date_text = utf8_to_sf(
                        make_date_string(calendar_year, calendar_month, d)
                    );

                    calendar_open = false;
                    active_field = 0;
                    return;
                }
            }

            return;
        } else {
            calendar_open = false;
        }
    }

    if (point_in_rect(x, y, 30, 40, 360, 38)) {
        active_field = 1;
        return;
    }

    if (point_in_rect(x, y, 30, 110, 360, 38)) {
        active_field = 2;
        return;
    }

    if (point_in_rect(x, y, 30, 180, 170, 38)) {
        active_field = 3;
        calendar_open = true;
        set_calendar_from_date();
        return;
    }

    if (point_in_rect(x, y, 220, 180, 28, 20)) {
        adjust_time(time_text, 5);
        return;
    }

    if (point_in_rect(x, y, 220, 225, 28, 20)) {
        adjust_time(time_text, -5);
        return;
    }

    if (point_in_rect(x, y, 255, 195, 130, 38)) {
        active_field = 4;
        return;
    }

    if (mode == 4) {
        if (point_in_rect(x, y, 30, 385, 28, 20)) {
            adjust_time(latest_arrival_text, 5);
            return;
        }

        if (point_in_rect(x, y, 30, 430, 28, 20)) {
            adjust_time(latest_arrival_text, -5);
            return;
        }

        if (point_in_rect(x, y, 65, 400, 130, 38)) {
            active_field = 5;
            return;
        }
    }

    for (int i = 0; i < (int)start_matches.size(); i++) {
        if (point_in_rect(x, y, 420, 40 + i * 32, 330, 28)) {
            selected_start = start_matches[i].stop_ids;
            start_text = sf::String::fromUtf8(start_matches[i].display_name.begin(), start_matches[i].display_name.end());
            update_matches();
            return;
        }
    }

    for (int i = 0; i < (int)target_matches.size(); i++) {
        if (point_in_rect(x, y, 420, 260 + i * 32, 330, 28)) {
            selected_target = target_matches[i].stop_ids;
            target_text = sf::String::fromUtf8(target_matches[i].display_name.begin(), target_matches[i].display_name.end());
            update_matches();
            return;
        }
    }

    if (point_in_rect(x, y, 30, 310, 260, 36)) {
        mode++;
        if (mode > 4) mode = 1;
        return;
    }

    float search_y = mode == 4 ? 480 : 370;

    if (point_in_rect(x, y, 30, search_y, 260, 42)) {
        calculate_route();
        return;
    }
}

void GuiApp::adjust_time(sf::String& time_value, int delta_minutes) {
    int t = parse_time_to_seconds(time_value.toAnsiString());
    t += delta_minutes * 60;

    if (t < 0) t += 24 * 3600;
    if (t >= 24 * 3600) t -= 24 * 3600;

    std::string out = seconds_to_time(t);
    time_value = sf::String::fromUtf8(out.begin(), out.end());
}

int GuiApp::days_in_month(int year, int month) const {
    static const int dim[] = {
        0,
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    bool leap =
        (year % 400 == 0) ||
        (year % 4 == 0 && year % 100 != 0);

    if (month == 2 && leap) return 29;
    return dim[month];
}

int GuiApp::day_of_week(int year, int month, int day) const {
    if (month < 3) {
        month += 12;
        year--;
    }

    int K = year % 100;
    int J = year / 100;

    int h =
        (day +
         (13 * (month + 1)) / 5 +
         K +
         K / 4 +
         J / 4 +
         5 * J) % 7;

    int dow_sun0 = (h + 6) % 7;

    int dow_mon0 = dow_sun0 - 1;
    if (dow_mon0 < 0) dow_mon0 = 6;

    return dow_mon0;
}

std::string GuiApp::make_date_string(int year, int month, int day) const {
    std::ostringstream out;

    out
        << std::setfill('0')
        << std::setw(4)
        << year
        << "-"
        << std::setw(2)
        << month
        << "-"
        << std::setw(2)
        << day;

    return out.str();
}

void GuiApp::set_calendar_from_date() {
    std::string d = normalize_gtfs_date(date_text.toAnsiString());

    if (d.size() != 8) {
        return;
    }

    calendar_year = std::stoi(d.substr(0, 4));
    calendar_month = std::stoi(d.substr(4, 2));

    if (calendar_month < 1 || calendar_month > 12) {
        calendar_month = 6;
    }
}

void GuiApp::calculate_route() {
    if (selected_start.empty() && !start_matches.empty()) {
        selected_start = start_matches.front().stop_ids;
        start_text = utf8_to_sf(start_matches.front().display_name);
    }

    if (selected_target.empty() && !target_matches.empty()) {
        selected_target = target_matches.front().stop_ids;
        target_text = utf8_to_sf(target_matches.front().display_name);
    }

    if (selected_start.empty() || selected_target.empty()) {
        result_text = "Wybierz przystanek startowy i koncowy.";
        return;
    }

    std::string normalized_date =
        normalize_gtfs_date(sf_to_utf8(date_text));

    if (normalized_date.size() != 8) {
        result_text = "Zly format daty.";
        return;
    }

    int year =
        std::stoi(normalized_date.substr(0, 4));

    int month =
        std::stoi(normalized_date.substr(4, 2));

    int day =
        std::stoi(normalized_date.substr(6, 2));

    if (month < 1 || month > 12 ||
        day < 1 || day > days_in_month(year, month))
    {
        result_text = "Niepoprawna data.";
        return;
    }

    if (!gtfs_has_any_service_on_date(data, normalized_date)) {
        result_text =
            "Brak rozkladu dla daty " +
            normalized_date +
            ". Dostepny zakres: " +
            gtfs_first_available_date(data) +
            " - " +
            gtfs_last_available_date(data) +
            ".";

        return;
    }

    if (!graph_ready || graph_date != normalized_date) {
        result_text =
            "Budowanie grafu dla daty " +
            normalized_date +
            "...";

        draw();

        graph =
            build_event_graph_for_date(
                data,
                normalized_date
            );

        graph_date = normalized_date;
        graph_ready = true;

        if (graph.events.empty()) {
            result_text =
                "Graf dla daty " +
                normalized_date +
                " jest pusty. Dostepny zakres: " +
                gtfs_first_available_date(data) +
                " - " +
                gtfs_last_available_date(data) +
                ".";

            return;
        }
    }

    int start_time =
        parse_time_to_seconds(sf_to_utf8(time_text));

    RouteMode route_mode =
        RouteMode::EarliestArrival;

    if (mode == 2) {
        route_mode = RouteMode::Balanced;
    }

    if (mode == 3) {
        route_mode = RouteMode::MinTransfers;
    }

    RouteResult result;

    if (mode == 4) {
        int latest =
            parse_time_to_seconds(
                sf_to_utf8(latest_arrival_text)
            );

        result =
            find_latest_departure(
                data,
                graph,
                selected_start,
                selected_target,
                latest,
                RouteMode::Balanced
            );
    } else {
        result =
            find_route(
                data,
                graph,
                selected_start,
                selected_target,
                start_time,
                route_mode
            );
    }

    result_text =
        route_result_to_string(result);
}

std::string GuiApp::route_result_to_string(
    const RouteResult& result
) const {
    if (!result.found) {
        return "Nie znaleziono trasy.";
    }

    auto stop_name =
        [&](const std::string& stop_id) {
            auto it = data.stops.find(stop_id);
            if (it == data.stops.end()) return stop_id;
            return it->second.name;
        };

    auto route_name =
        [&](const std::string& route_id) {
            auto it = data.routes.find(route_id);
            if (it == data.routes.end()) return route_id;
            return it->second.short_name;
        };

    std::ostringstream out;

    out << "Start: " << seconds_to_time(result.start_time) << "\n";
    out << "Koniec: " << seconds_to_time(result.arrival_time) << "\n";
    out << "Czas: " << result.total_seconds / 60 << " min\n";
    out << "Wejscia: " << result.boardings << "\n";
    out << "Przesiadki: " << result.transfers << "\n";
    out << "Pieszo: " << result.walk_seconds / 60 << " min\n\n";

    std::string current_trip;
    std::string current_route;
    int segment_start = -1;
    int segment_end = -1;

    auto flush_ride =
        [&]() {
            if (segment_start == -1 ||
                segment_end == -1)
            {
                return;
            }

            const TransitEvent& a =
                graph.events[segment_start];

            const TransitEvent& b =
                graph.events[segment_end];

            out
                << seconds_to_time(a.departure_time)
                << " wsiadz linia "
                << route_name(current_route)
                << " z "
                << stop_name(a.stop_id)
                << "\n";

            out
                << seconds_to_time(b.arrival_time)
                << " wysiadz "
                << stop_name(b.stop_id)
                << "\n\n";
        };

    for (const RouteStep& step : result.steps) {
        if (step.type == "ride") {
            if (current_trip.empty()) {
                current_trip = step.trip_id;
                current_route = step.route_id;
                segment_start = step.from_event;
                segment_end = step.to_event;
            } else if (current_trip == step.trip_id) {
                segment_end = step.to_event;
            } else {
                flush_ride();

                current_trip = step.trip_id;
                current_route = step.route_id;
                segment_start = step.from_event;
                segment_end = step.to_event;
            }
        } else {
            flush_ride();

            current_trip.clear();
            current_route.clear();
            segment_start = -1;
            segment_end = -1;

            const TransitEvent& a =
                graph.events[step.from_event];

            const TransitEvent& b =
                graph.events[step.to_event];

            if (step.type == "walk") {
                out
                    << seconds_to_time(a.arrival_time)
                    << " przejdz pieszo z "
                    << stop_name(a.stop_id)
                    << " na "
                    << stop_name(b.stop_id)
                    << "\n";

                out
                    << seconds_to_time(step.arrival_time)
                    << " jestes na "
                    << stop_name(b.stop_id)
                    << "\n\n";
            } else {
                out
                    << seconds_to_time(a.arrival_time)
                    << " czekaj na "
                    << stop_name(a.stop_id)
                    << " do "
                    << seconds_to_time(b.departure_time)
                    << "\n";
            }
        }
    }

    flush_ride();

    return out.str();
}

void GuiApp::draw_textbox(
    float x,
    float y,
    float w,
    float h,
    const std::string& label,
    const sf::String& value,
    bool active
) {
    sf::Text label_text(utf8_to_sf(label), font, 15);
    label_text.setPosition(x, y - 22);
    label_text.setFillColor(sf::Color::Black);
    window.draw(label_text);

    sf::RectangleShape box({w, h});
    box.setPosition(x, y);
    box.setFillColor(sf::Color::White);
    box.setOutlineColor(
        active ? sf::Color::Blue : sf::Color(120, 120, 120)
    );
    box.setOutlineThickness(2);
    window.draw(box);

    sf::Text text(value, font, 17);
    text.setPosition(x + 8, y + 7);
    text.setFillColor(sf::Color::Black);
    window.draw(text);
}

void GuiApp::draw_button(
    float x,
    float y,
    float w,
    float h,
    const std::string& label
) {
    sf::RectangleShape box({w, h});
    box.setPosition(x, y);
    box.setFillColor(sf::Color(220, 220, 230));
    box.setOutlineColor(sf::Color(80, 80, 80));
    box.setOutlineThickness(2);
    window.draw(box);

    sf::Text text(utf8_to_sf(label), font, 17);
    text.setPosition(x + 10, y + 8);
    text.setFillColor(sf::Color::Black);
    window.draw(text);
}

void GuiApp::draw_time_spinner(
    float x,
    float y,
    const std::string& label,
    const sf::String& value,
    bool active
) {
    sf::Text label_text(utf8_to_sf(label), font, 15);
    label_text.setPosition(x, y - 22);
    label_text.setFillColor(sf::Color::Black);
    window.draw(label_text);

    draw_button(x, y, 28, 20, "^");
    draw_button(x, y + 45, 28, 20, "v");

    draw_textbox(
        x + 35,
        y + 15,
        130,
        38,
        "",
        value,
        active
    );
}

void GuiApp::draw_calendar() {
    if (!calendar_open) {
        return;
    }

    float x = 30;
    float y = 225;

    sf::RectangleShape box({280, 260});
    box.setPosition(x, y);
    box.setFillColor(sf::Color(255, 255, 255));
    box.setOutlineColor(sf::Color::Black);
    box.setOutlineThickness(2);
    window.draw(box);

    sf::RectangleShape left({35, 28});
    left.setPosition(x + 10, y + 10);
    left.setFillColor(sf::Color(220, 220, 230));
    left.setOutlineColor(sf::Color::Black);
    left.setOutlineThickness(1);
    window.draw(left);

    sf::Text lt(utf8_to_sf("<"), font, 18);
    lt.setPosition(x + 21, y + 12);
    lt.setFillColor(sf::Color::Black);
    window.draw(lt);

    sf::RectangleShape right({35, 28});
    right.setPosition(x + 235, y + 10);
    right.setFillColor(sf::Color(220, 220, 230));
    right.setOutlineColor(sf::Color::Black);
    right.setOutlineThickness(1);
    window.draw(right);

    sf::Text rt(utf8_to_sf(">"), font, 18);
    rt.setPosition(x + 247, y + 12);
    rt.setFillColor(sf::Color::Black);
    window.draw(rt);

    std::ostringstream title;
    title
        << std::setfill('0')
        << std::setw(4)
        << calendar_year
        << "-"
        << std::setw(2)
        << calendar_month;

    sf::Text title_text(utf8_to_sf(title.str()), font, 18);
    title_text.setPosition(x + 95, y + 15);
    title_text.setFillColor(sf::Color::Black);
    window.draw(title_text);

    const char* days[] = {
        "Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd"
    };

    for (int i = 0; i < 7; i++) {
        sf::Text t(utf8_to_sf(days[i]), font, 13);
        t.setPosition(x + 20 + i * 35, y + 52);
        t.setFillColor(sf::Color::Black);
        window.draw(t);
    }

    int first_dow =
        day_of_week(calendar_year, calendar_month, 1);

    int dim =
        days_in_month(calendar_year, calendar_month);

    std::string current_date =
        normalize_gtfs_date(sf_to_utf8(date_text));

    for (int d = 1; d <= dim; d++) {
        int idx = first_dow + d - 1;
        int row = idx / 7;
        int col = idx % 7;

        float bx = x + 15 + col * 35;
        float by = y + 75 + row * 35;

        std::string this_date =
            normalize_gtfs_date(
                make_date_string(calendar_year, calendar_month, d)
            );

        bool selected =
            this_date == current_date;

        bool available =
            available_dates.contains(this_date);

        sf::RectangleShape cell({30, 28});
        cell.setPosition(bx, by);

        if (selected) {
            cell.setFillColor(sf::Color(120, 180, 255));
        } else if (available) {
            cell.setFillColor(sf::Color(235, 235, 245));
        } else {
            cell.setFillColor(sf::Color(215, 215, 215));
        }

        cell.setOutlineColor(sf::Color(160, 160, 160));
        cell.setOutlineThickness(1);
        window.draw(cell);

        sf::Text txt(utf8_to_sf(std::to_string(d)), font, 14);
        txt.setPosition(bx + 7, by + 5);

        if (available) {
            txt.setFillColor(sf::Color::Black);
        } else {
            txt.setFillColor(sf::Color(140, 140, 140));
        }

        window.draw(txt);
    }
}

void GuiApp::draw() {
    window.clear(sf::Color(245, 245, 245));

    draw_textbox(
        30, 40, 360, 38,
        "Przystanek startowy",
        start_text,
        active_field == 1
    );

    draw_textbox(
        30, 110, 360, 38,
        "Przystanek końcowy",
        target_text,
        active_field == 2
    );

    draw_textbox(
        30, 180, 170, 38,
        "Data",
        date_text,
        active_field == 3
    );

    draw_time_spinner(
        220,
        180,
        "Godzina startu",
        time_text,
        active_field == 4
    );

    std::string mode_label = "Tryb: ";

    if (mode == 1) mode_label += "najwczesniej";
    if (mode == 2) mode_label += "zbalansowany";
    if (mode == 3) mode_label += "malo przesiadek";
    if (mode == 4) mode_label += "najpozniej wyjechac";

    draw_button(30, 310, 260, 36, mode_label);

    float search_y = 370;

    if (mode == 4) {
        draw_time_spinner(
            30,
            385,
            "Godzina dojazdu",
            latest_arrival_text,
            active_field == 5
        );

        search_y = 480;
    }

    draw_button(30, search_y, 260, 42, "Szukaj trasy");

    sf::Text st(utf8_to_sf("Wyniki startu:"), font, 16);
    st.setPosition(420, 15);
    st.setFillColor(sf::Color::Black);
    window.draw(st);

    for (int i = 0; i < (int)start_matches.size(); i++) {
        draw_button(
            420,
            40 + i * 32,
            330,
            28,
            start_matches[i].display_name
        );
    }

    sf::Text tt(utf8_to_sf("Wyniki celu:"), font, 16);
    tt.setPosition(420, 235);
    tt.setFillColor(sf::Color::Black);
    window.draw(tt);

    for (int i = 0; i < (int)target_matches.size(); i++) {
        draw_button(
            420,
            260 + i * 32,
            330,
            28,
            target_matches[i].display_name
        );
    }

    sf::RectangleShape result_box({390, 720});
    result_box.setPosition(780, 40);
    result_box.setFillColor(sf::Color::White);
    result_box.setOutlineColor(sf::Color(120, 120, 120));
    result_box.setOutlineThickness(2);
    window.draw(result_box);

    sf::Text result(utf8_to_sf(result_text), font, 15);
    result.setPosition(795, 55);
    result.setFillColor(sf::Color::Black);
    window.draw(result);

    draw_calendar();

    window.display();
}
