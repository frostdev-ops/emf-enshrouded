#include "core/hud_editor/bars.h"

#include <algorithm>
#include <cmath>

namespace emf::core::hud {

int bar_index(uintptr_t data, uintptr_t base) {
    if (data < base + kBarDataFirst) return -1;
    const uintptr_t rel = data - base - kBarDataFirst;
    if (rel % kBarDataStride != 0) return -1;
    const uintptr_t k = rel / kBarDataStride;
    return k < kBarCount ? static_cast<int>(k) : -1;
}

std::array<float, kBarCount> order_dy(const std::array<BarRect, kBarCount>& game, const BarOrder& order,
                                      const std::array<bool, kBarCount>& hidden) {
    std::array<float, kBarCount> dy{};
    if (!valid_order(order)) return dy;
    int first = -1, second = -1;
    float top = 0;
    for (size_t b = 0; b < kBarCount; ++b) {
        if (!game[b].seen) continue;
        if (first < 0) {
            first = static_cast<int>(b);
            top = game[b].y;
        } else if (second < 0) {
            second = static_cast<int>(b);
        }
        top = (std::min)(top, game[b].y);
    }
    if (first < 0) return dy;
    float gap = kBarGap;
    if (second >= 0) {
        const BarRect &f = game[static_cast<size_t>(first)], &s = game[static_cast<size_t>(second)];
        const float g = s.y - (f.y + f.h);
        if (std::isfinite(g) && g >= 0) gap = g;
    }
    float y = top;
    for (uint8_t b : order) {
        if (!game[b].seen || hidden[b]) continue;
        dy[b] = y - game[b].y;
        y += game[b].h + gap;
    }
    return dy;
}

float grow_up_dy(float list_y, float list_h, float column_h, const Xform& top, float ui_h) {
    if (!(top.scale > 0) || !std::isfinite(top.scale) || !std::isfinite(top.ty) || !std::isfinite(list_y) ||
        !(list_h >= 0) || !std::isfinite(list_h) || !std::isfinite(column_h) || !(ui_h > 0) || !std::isfinite(ui_h))
        return 0;
    const float desired = column_h - list_h;
    const float upper = (ui_h - top.ty) / top.scale - list_y - list_h;  // the bottom stays on screen
    const float lower = -top.ty / top.scale - list_y;                    // the top stays on screen
    return (std::max)((std::min)(desired, upper), lower);
}

}  // namespace emf::core::hud
