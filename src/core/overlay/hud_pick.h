#pragma once
// Picking a HUD part with the mouse and dragging it (the HUD page): where a part is on the display, which part is under the
// pointer, and what a drag adds in percent. Pure functions; hud_page.cpp feeds them the mouse.
#include <array>
#include <optional>

#include "core/hud_editor/hook.h"
#include "core/hud_editor/layout.h"
#include "imgui.h"

namespace emf::core {

struct ScreenRect {
    float x, y, w, h;
};

// A part's rectangle in the UI space's units ({x, y, w, h}) on a display of `display` pixels.
ScreenRect to_screen(const float rect_units[4], hud::UiSpace ui, ImVec2 display);
// The smallest rectangle that holds the point (a part inside another wins over its parent), or -1.
int pick_part(const std::array<std::optional<ScreenRect>, hud::kHudElementCount>& rects, ImVec2 mouse);
// The layout after a drag of delta_px from `start`: the pixels as percent of the display, clamped.
hud::ElementLayout dragged(hud::ElementLayout start, ImVec2 delta_px, ImVec2 display);

}  // namespace emf::core
