#pragma once
// Draws a mod's radial wheel with Dear ImGui: a ring of up to 8 slices in the middle of the screen, the highlighted one
// lit, the title and the page in the centre, a hint below. Renderer-agnostic.
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "wheel_model.h"

namespace emf::core {

// "Mace (6 Wood Logs, 1 Stone)" -> {"Mace", "(6 Wood Logs, 1 Stone)"}; a label without " (" stays on one line.
std::pair<std::string, std::string> wheel_label_lines(const std::string& label);

// One line of a slice's label: the dim ones are a shared wheel's section ("Beasts:") and the detail ("(rests 1:20)").
struct WheelLabelLine {
    std::string text;
    bool dim = false;
};
// A slice's label as lines for a slice `width` wide: the name on one line when it fits; else a shared wheel's
// "Section: " prefix on a line of its own, then the rest, wrapped at a space when still too wide (wheel_title_lines);
// then the detail in brackets (wheel_label_lines). At most 4 lines; a line may still be too wide (the drawing cuts it).
std::vector<WheelLabelLine> wheel_label_layout(const std::string& label, float width,
                                               const std::function<float(const std::string&)>& measure);

// A title wider than `width` breaks at the last space that keeps the first line inside it; the second line may still
// be too wide (the drawing cuts it). A title that fits, or has no such space, stays on one line.
std::pair<std::string, std::string> wheel_title_lines(const std::string& title, float width,
                                                      const std::function<float(const std::string&)>& measure);

// The width in pixels a slice's label may use on a page of `slices` slices on a `display`-sized screen. The overlay's
// font is Consolas at height / 54 pixels (about 0.55 x that wide a character), so a page of 8 fits about 13 characters
// a line at any screen size.
float wheel_label_width(ImVec2 display, size_t slices);

void draw_wheel(const WheelModel& wheel, ImVec2 display);

}  // namespace emf::core
