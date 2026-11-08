#pragma once
// The HUD layout files in <game>\emf\hud: one per layout, named by the layout name's slug, written whole.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/hud_editor/layout.h"

namespace emf::core::hud {

constexpr const char* kDefaultLayoutName = "Default";
constexpr const char* kBeforeResetName = "Before reset";

struct LayoutFile {
    std::string slug;  // the file's name without .json
    std::optional<HudLayout> layout;
    std::string error;  // why the file cannot be used
    std::vector<std::string> notes;
};

// Every file of `dir` whose name ends in .json (case ignored), sorted by slug. A missing folder gives an empty list. When
// the name inside is empty or its slug is not the file's, the layout is shown under the file's slug.
std::vector<LayoutFile> read_layout_dir(const std::wstring& dir);
// Creates `dir` (one level) and writes <dir>\<profile_slug(l.name)>.json whole.
bool write_layout_file(const std::wstring& dir, const HudLayout& l, uint32_t build, std::string* error);
// Removes <dir>\<slug>.json; a file that is already gone counts as success. A slug that is not a file name of ours is refused.
bool delete_layout_file(const std::wstring& dir, const std::string& slug, std::string* error);

}  // namespace emf::core::hud
