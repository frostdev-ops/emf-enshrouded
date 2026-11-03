#pragma once
// Draws a mod's window (API v24) with Dear ImGui in the console's look: title, text, headings, buttons, rows, text
// slots, bars, separators, side by side where asked; tooltips; the clicked widget's key back. Renderer-agnostic.
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/window_request.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "look_layout.h"

namespace emf::core {

struct WindowClick {
    std::string key;
    bool right = false;
};

struct WindowUiState {
    std::string id;              // the window drawn last; another id starts fresh
    bool close_clicked = false;  // its X was clicked (the overlay closes it: CLOSED, BY_PLAYER)
    std::vector<std::pair<std::string, ImRect>> hit;  // the clickable widgets of the last frame, by key
};

// ImGui's window name for `w` (its title, then a hidden part from its id, so a new title keeps the window).
std::string window_imgui_name(const WindowRequest& w);

// One frame of the window. `input` false (the console is open over it): drawn dim, no clicks. Returns the click of this
// frame, if any. Places: centre, left or right of `display`; width in text heights.
std::optional<WindowClick> draw_window(const WindowRequest& w, WindowUiState& st, bool input, ImVec2 display,
                                       const TextLook& look);

}  // namespace emf::core
