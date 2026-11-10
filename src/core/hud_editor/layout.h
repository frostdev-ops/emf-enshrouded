#pragma once
// A HUD layout: for each of the HUD parts an offset (percent of the UI space), a scale and a hidden flag, and its JSON
// file format (docs/engine_parts/re_ui_scopes.md describes what the offsets act on).
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/hud_editor/colours.h"
#include "core/hud_editor/elements.h"

namespace emf::core::hud {

constexpr float kOffsetMin = -100.0f, kOffsetMax = 100.0f, kScaleMin = 0.5f, kScaleMax = 2.0f;
constexpr int kLayoutFormat = 1;
constexpr size_t kLayoutFileMax = 64 * 1024;

struct ElementLayout {
    float x = 0, y = 0, scale = 1;  // x and y: percent of the UI space; +x is right, +y is down
    bool hidden = false;
    bool operator==(const ElementLayout&) const = default;
    bool is_default() const { return *this == ElementLayout{}; }
};

struct HudLayout {
    std::string name;
    std::array<ElementLayout, kHudElementCount> elements{};
    BarOrder order = kDefaultOrder;  // the vitals' bars, top first
    bool buffs_up = false;           // the status effects list grows upward from the food icons' bottom edge
    std::array<BarColours, kBarCount> colours{};  // the vitals' bar colours, by bar (they follow the bar, not its place)
    Colour panel{};                               // the dark panel behind the bars (health's frame colour)
    bool is_default() const {
        if (order != kDefaultOrder || buffs_up || panel.set) return false;
        for (const auto& c : colours)
            if (c.any()) return false;
        for (const auto& e : elements)
            if (!e.is_default()) return false;
        return true;
    }
};

// In range and rounded to 0.01; NaN or infinity gives 0 (offset) or 1 (scale).
float clamp_offset(double percent);
float clamp_scale(double s);
int percent_to_pixels(float percent, float extent);
float pixels_to_percent(float pixels, float extent);  // an extent of 0 or less gives 0
float percent_to_units(float percent, float extent);  // not rounded
// "%.2f" without trailing zeros; "-0" becomes "0".
std::string number_text(float v);

std::string layout_to_json(const HudLayout& l, uint32_t build);

struct LayoutRead {
    std::optional<HudLayout> layout;  // empty when the file is refused
    std::string error;
    std::vector<std::string> notes;  // what was skipped or clamped
};
LayoutRead layout_from_json(std::string_view text);

}  // namespace emf::core::hud
