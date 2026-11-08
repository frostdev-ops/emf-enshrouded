#include "hud_pick.h"

namespace emf::core {

using namespace hud;

ScreenRect to_screen(const float rect_units[4], UiSpace ui, ImVec2 display) {
    const float sx = ui.w > 0 ? display.x / ui.w : 1.0f;
    const float sy = ui.h > 0 ? display.y / ui.h : 1.0f;
    return ScreenRect{rect_units[0] * sx, rect_units[1] * sy, rect_units[2] * sx, rect_units[3] * sy};
}

int pick_part(const std::array<std::optional<ScreenRect>, kHudElementCount>& rects, ImVec2 mouse) {
    int best = -1;
    float best_area = 0;
    for (size_t i = 0; i < kHudElementCount; ++i) {
        if (!rects[i]) continue;
        const ScreenRect& r = *rects[i];
        if (mouse.x < r.x || mouse.x > r.x + r.w || mouse.y < r.y || mouse.y > r.y + r.h) continue;
        const float area = r.w * r.h;
        if (best < 0 || area < best_area) {
            best = static_cast<int>(i);
            best_area = area;
        }
    }
    return best;
}

ElementLayout dragged(ElementLayout start, ImVec2 delta_px, ImVec2 display) {
    start.x = clamp_offset(static_cast<double>(start.x) + pixels_to_percent(delta_px.x, display.x));
    start.y = clamp_offset(static_cast<double>(start.y) + pixels_to_percent(delta_px.y, display.y));
    return start;
}

}  // namespace emf::core
