#include "csv.hpp"
#include "util.hpp"

#include <fstream>
#include <stdexcept>

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> result;
    std::string cell;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];

        if (c == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                cell += '"';
                i++;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (c == ',' && !in_quotes) {
            result.push_back(trim(cell));
            cell.clear();
        } else {
            cell += c;
        }
    }

    result.push_back(trim(cell));
    return result;
}

CSVTable read_csv(const std::string& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error("Nie mozna otworzyc pliku CSV: " + path);
    }

    CSVTable table;
    std::string line;

    if (!std::getline(file, line)) {
        throw std::runtime_error("Pusty plik CSV: " + path);
    }

    table.header = split_csv_line(line);

    for (int i = 0; i < (int)table.header.size(); i++) {
        table.header[i] = trim(table.header[i]);
        table.column[table.header[i]] = i;
    }

    while (std::getline(file, line)) {
        if (trim(line).empty()) continue;
        table.rows.push_back(split_csv_line(line));
    }

    return table;
}
