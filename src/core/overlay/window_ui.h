#pragma once
// Draws a mod's window (API v24) with Dear ImGui in the console's look: title, text, headings, buttons, rows, text
// slots, bars, separators, side by side where asked; tooltips; the clicked widget's key back. API v26: pictures (an
// image widget, a slot's picture, a row's icon). Renderer-agnostic.
#include <functional>
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

// A picture's frame to draw (API v26): its texture, the frame's UVs, one frame's width / height.
struct ImageDraw {
    ImTextureID tex = 0;
    ImVec2 uv0{0, 0}, uv1{1, 1};
    float aspect = 1;
};
// The frame of picture `handle` at `seconds` for `fps`; nothing when it is unknown or not on the GPU yet.
using ImageLookup = std::function<std::optional<ImageDraw>(uint32_t handle, double seconds, uint32_t fps)>;

// One frame of the window. `input` false (the console is open over it): drawn dim, no clicks. Returns the click of this
// frame, if any. Places: centre, left or right of `display`; width in text heights. `images`: the pictures (null: none
// are drawn; the widgets keep their place).
std::optional<WindowClick> draw_window(const WindowRequest& w, WindowUiState& st, bool input, ImVec2 display,
                                       const TextLook& look, const ImageLookup* images = nullptr);

}  // namespace emf::core
