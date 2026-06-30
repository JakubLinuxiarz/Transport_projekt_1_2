#include "util.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>

int parse_time_to_seconds(const std::string& s) {
    if (s.size() < 5) return 0;

    int h = std::stoi(s.substr(0, 2));
    int m = std::stoi(s.substr(3, 2));
    int sec = 0;

    if (s.size() >= 8) {
        sec = std::stoi(s.substr(6, 2));
    }

    return h * 3600 + m * 60 + sec;
}

std::string seconds_to_time(int seconds) {
    if (seconds < 0) seconds = 0;

    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;

    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << h
        << ":"
        << std::setfill('0') << std::setw(2) << m;

    return out.str();
}

double deg_to_rad(double deg) {
    return deg * M_PI / 180.0;
}

double haversine_meters(double lat1, double lon1, double lat2, double lon2) {
    constexpr double R = 6371000.0;

    double dlat = deg_to_rad(lat2 - lat1);
    double dlon = deg_to_rad(lon2 - lon1);

    lat1 = deg_to_rad(lat1);
    lat2 = deg_to_rad(lat2);

    double a =
        std::sin(dlat / 2) * std::sin(dlat / 2) +
        std::cos(lat1) * std::cos(lat2) *
        std::sin(dlon / 2) * std::sin(dlon / 2);

    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));

    return R * c;
}

std::string trim(const std::string& s) {
    size_t start = 0;

    while (start < s.size() &&
           std::isspace((unsigned char)s[start])) {
        start++;
    }

    size_t end = s.size();

    while (end > start &&
           std::isspace((unsigned char)s[end - 1])) {
        end--;
    }

    return s.substr(start, end - start);
}

std::string normalize_name(const std::string& s) {
    std::string result = trim(s);

    std::string out;
    out.reserve(result.size());

    for (size_t i = 0; i < result.size();) {
        unsigned char c = (unsigned char)result[i];

        if (c < 128) {
            out.push_back((char)std::tolower(c));
            i++;
            continue;
        }

        if (i + 1 < result.size()) {
            unsigned char c2 = (unsigned char)result[i + 1];

            if (c == 0xC4 && c2 == 0x85) { out += "a"; i += 2; continue; } // ą
            if (c == 0xC4 && c2 == 0x84) { out += "a"; i += 2; continue; } // Ą

            if (c == 0xC4 && c2 == 0x87) { out += "c"; i += 2; continue; } // ć
            if (c == 0xC4 && c2 == 0x86) { out += "c"; i += 2; continue; } // Ć

            if (c == 0xC4 && c2 == 0x99) { out += "e"; i += 2; continue; } // ę
            if (c == 0xC4 && c2 == 0x98) { out += "e"; i += 2; continue; } // Ę

            if (c == 0xC5 && c2 == 0x82) { out += "l"; i += 2; continue; } // ł
            if (c == 0xC5 && c2 == 0x81) { out += "l"; i += 2; continue; } // Ł

            if (c == 0xC5 && c2 == 0x84) { out += "n"; i += 2; continue; } // ń
            if (c == 0xC5 && c2 == 0x83) { out += "n"; i += 2; continue; } // Ń

            if (c == 0xC3 && c2 == 0xB3) { out += "o"; i += 2; continue; } // ó
            if (c == 0xC3 && c2 == 0x93) { out += "o"; i += 2; continue; } // Ó

            if (c == 0xC5 && c2 == 0x9B) { out += "s"; i += 2; continue; } // ś
            if (c == 0xC5 && c2 == 0x9A) { out += "s"; i += 2; continue; } // Ś

            if (c == 0xC5 && c2 == 0xBA) { out += "z"; i += 2; continue; } // ź
            if (c == 0xC5 && c2 == 0xB9) { out += "z"; i += 2; continue; } // Ź

            if (c == 0xC5 && c2 == 0xBC) { out += "z"; i += 2; continue; } // ż
            if (c == 0xC5 && c2 == 0xBB) { out += "z"; i += 2; continue; } // Ż
        }

        i++;
    }

    return out;
}
