#pragma once
// The EML content mods this game's data was built with, for the mod check on join (mods_check.h): the usable EML mods
// in <game>\mods\ when the loader's content status at this start is "built" or "current"; none when the content could
// not be applied ("failed", "off") or EMF did not build it (no status). The status is read once at start
// (read_content_status clears its variable): pass the one the core kept.
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "loader/content_status_env.h"
#include "loader/eml_mods.h"

namespace emf::core {

inline std::vector<std::pair<std::string, std::string>> applied_content_mods(
    const std::optional<emf::loader::ContentStatus>& status, const std::wstring& game) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!status || (status->kind != "built" && status->kind != "current")) return out;
    for (const auto& m : emf::loader::scan_eml_mods(game + L"\\mods").usable) out.emplace_back(m.id, m.version);
    return out;
}

}  // namespace emf::core
