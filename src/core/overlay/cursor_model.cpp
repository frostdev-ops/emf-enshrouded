#include "cursor_model.h"

#include <algorithm>

namespace emf::core {

void CursorModel::set_display(ImVec2 display) {
    display_ = display;
    if (!placed_) {
        placed_ = true;
        pos_ = ImVec2(display.x * 0.5f, display.y * 0.5f);
    }
    clamp();
}

void CursorModel::move(float dx, float dy, float speed) {
    pos_.x += dx * speed;
    pos_.y += dy * speed;
    clamp();
}

void CursorModel::place(ImVec2 p) {
    placed_ = true;
    pos_ = p;
    clamp();
}

void CursorModel::clamp() {
    pos_.x = std::clamp(pos_.x, 0.0f, (std::max)(display_.x - 1.0f, 0.0f));
    pos_.y = std::clamp(pos_.y, 0.0f, (std::max)(display_.y - 1.0f, 0.0f));
}

PointerSource pointer_source(bool windows_pointer_showing, bool game_in_front, bool absolute_mouse) {
    if (!game_in_front) return PointerSource::Own;
    return windows_pointer_showing || absolute_mouse ? PointerSource::Windows : PointerSource::Own;
}

std::optional<ImVec2> client_to_display(int x, int y, int client_w, int client_h, ImVec2 display) {
    if (client_w <= 0 || client_h <= 0) return std::nullopt;
    const float px = static_cast<float>(x) * display.x / static_cast<float>(client_w);
    const float py = static_cast<float>(y) * display.y / static_cast<float>(client_h);
    return ImVec2(std::clamp(px, 0.0f, (std::max)(display.x - 1.0f, 0.0f)),
                  std::clamp(py, 0.0f, (std::max)(display.y - 1.0f, 0.0f)));
}

}  // namespace emf::core
