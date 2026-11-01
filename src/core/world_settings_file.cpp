#include "world_settings_file.h"

#include <cstdio>
#include <sstream>

#include "world_settings.h"

namespace emf::core::ws {

std::string overrides_text(const std::map<std::string, double>& values) {
    std::string out = "# EMF world settings kept for this world (world.set <key> <value> persist; world.reset all persist)\r\n";
    for (const auto& [key, value] : values) {  // std::map: sorted by key
        char b[48];
        std::snprintf(b, sizeof b, "%.9g", value);
        out += key + "=" + b + "\r\n";
    }
    return out;
}

std::map<std::string, double> parse_overrides(const std::string& text, std::vector<std::string>* skipped) {
    std::map<std::string, double> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#' || line[start] == ';') continue;
        line = line.substr(start);
        const size_t eq = line.find('=');
        const std::string key = line.substr(0, eq == std::string::npos ? line.size() : eq);
        const Field* f = find(key);
        if (!f || eq == std::string::npos) {
            if (skipped) skipped->push_back(key + ": not a world setting");
            continue;
        }
        const Parsed p = parse(*f, line.substr(eq + 1));
        if (!p.ok) {
            if (skipped) skipped->push_back(std::string(f->key) + ": " + p.error);
            continue;
        }
        out[f->key] = p.value;
    }
    return out;
}

}  // namespace emf::core::ws
