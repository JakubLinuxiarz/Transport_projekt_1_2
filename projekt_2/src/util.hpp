#pragma once

#include <string>

int parse_time_to_seconds(const std::string& s);
std::string seconds_to_time(int seconds);

double deg_to_rad(double deg);
double haversine_meters(double lat1, double lon1, double lat2, double lon2);

std::string trim(const std::string& s);
std::string normalize_name(const std::string& s);
