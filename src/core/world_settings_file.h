#pragma once
// World settings kept for a world: `key=value` lines (keys as in the struct, durations in minutes, enums as numbers),
// one file per world slot, read at the world's load and written when a kept setting changes.
#include <map>
#include <string>
#include <vector>

namespace emf::core::ws {

std::string overrides_text(const std::map<std::string, double>& values);
// Unknown keys and values a setting does not allow are left out, each named in `skipped`.
std::map<std::string, double> parse_overrides(const std::string& text, std::vector<std::string>* skipped);

}  // namespace emf::core::ws
