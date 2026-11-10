#pragma once
// The vitals' bars and the status list (docs/engine_parts/re_ui_scopes.md): which bar a call of the player widget's bar
// function is, the offsets that stack the bars in the layout's order, and the offset that makes the status effects list
// grow upward from the food icons' bottom edge while it stays on screen. Pure functions; no Windows headers.
#include <array>
#include <cstdint>

#include "core/hud_editor/elements.h"
#include "core/hud_editor/scale.h"

namespace emf::core::hud {

// Build 1287369: the bars' data in the widget (health, mana, stamina), and the game's gap between two bars (units).
constexpr uintptr_t kBarDataFirst = 0x18, kBarDataStride = 0xd8;
constexpr float kBarGap = 6.0f;

// base = the first call's data - kBarDataFirst; 0..2 when data is that bar's, else -1.
int bar_index(uintptr_t data, uintptr_t base);

struct BarRect {
    float x = 0, y = 0, w = 0, h = 0;  // UI units, as the game laid the bar out (before EMF's offsets)
    bool seen = false;
};

// The y offsets that stack the seen bars that are not hidden in `order`, from the top of the seen bars, each next one
// its height plus the gap below (the gap measured between the first two seen bars in the game's order, kBarGap with
// fewer); 0 for a bar not seen or hidden. A hidden or absent bar leaves no hole.
std::array<float, kBarCount> order_dy(const std::array<BarRect, kBarCount>& game, const BarOrder& order,
                                      const std::array<bool, kBarCount>& hidden);

// The y offset (layer units) for the status list at list_y with height list_h beside a food column column_h high:
// column_h - list_h (its bottom on the column's bottom), then at most what keeps its bottom on screen and at least what
// keeps its top on screen (the top wins); top is the transform stack's top (screen = scale * p + t), ui_h the screen's
// height in UI units. 0 when an input is not usable.
float grow_up_dy(float list_y, float list_h, float column_h, const Xform& top, float ui_h);

}  // namespace emf::core::hud
