#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct CSVTable {
    std::vector<std::string> header;
    std::unordered_map<std::string, int> column;
    std::vector<std::vector<std::string>> rows;
};

std::vector<std::string> split_csv_line(const std::string& line);
CSVTable read_csv(const std::string& path);
