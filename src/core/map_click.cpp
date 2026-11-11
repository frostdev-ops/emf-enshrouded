// Markers step 2 (map_click.h).
#include "core/map_click.h"

#include <algorithm>
#include <cmath>

namespace emf::core {

namespace {
constexpr double kWorld = 10240.0;  // the main scene, metres (marker_ui.h kMapWorldSize)
}

bool map_world_at(const MapView& v, float ui_x, float ui_y, double* x, double* z) {
    if (!(v.rect[2] > 0) || !(v.rect[3] > 0) || !x || !z) return false;
    if (v.clip[2] > 0 && v.clip[3] > 0 &&
        !(ui_x >= v.clip[0] && ui_x <= v.clip[0] + v.clip[2] && ui_y >= v.clip[1] && ui_y <= v.clip[1] + v.clip[3]))
        return false;
    const double wx = (ui_x - v.rect[0]) / v.rect[2] * kWorld;
    const double wz = (1.0 - (ui_y - v.rect[1]) / v.rect[3]) * kWorld;
    if (!std::isfinite(wx) || !std::isfinite(wz) || wx < 0 || wx > kWorld || wz < 0 || wz > kWorld) return false;
    *x = wx;
    *z = wz;
    return true;
}

MapHoldOut MapHold::down(bool shift, bool on_map, double x, double z, uint64_t now_ms) {
    MapHoldOut o;
    if (holding_) {
        o.swallow = true;
        return o;
    }
    rewrite_next_ = false;
    if (enabled_ && shift && on_map) {
        holding_ = button_ours_ = true;
        start_ms_ = now_ms;
        x_ = x;
        z_ = z;
        o.swallow = o.started = true;
        return o;
    }
    button_ours_ = false;
    return o;
}

MapHoldOut MapHold::dblclk(bool shift, bool on_map, double x, double z, uint64_t now_ms) {
    MapHoldOut o;
    if (holding_) {
        o.swallow = true;
        return o;
    }
    if (enabled_ && shift && on_map) return down(shift, on_map, x, z, now_ms);
    if (rewrite_next_) {  // the game never saw the click before this one: for it, this is a first press
        rewrite_next_ = false;
        button_ours_ = false;
        o.rewrite_down = true;
    }
    return o;
}

MapHoldOut MapHold::up(uint64_t now_ms) {
    MapHoldOut o;
    if (holding_) {
        holding_ = button_ours_ = false;
        rewrite_next_ = true;
        o.swallow = true;
        if (now_ms >= start_ms_ && now_ms - start_ms_ >= hold_ms_) {
            o.done = true;
            o.x = x_;
            o.z = z_;
        } else {
            o.cancelled = true;
        }
        return o;
    }
    if (button_ours_) {
        button_ours_ = false;
        rewrite_next_ = true;
        o.swallow = true;
    }
    return o;
}

MapHoldOut MapHold::cancel(bool swallow) {
    MapHoldOut o;
    holding_ = false;  // button_ours_ stays: the release is still the hold's
    o.swallow = swallow;
    o.cancelled = true;
    return o;
}

MapHoldOut MapHold::right() { return holding_ ? cancel(true) : MapHoldOut{}; }
MapHoldOut MapHold::esc() { return holding_ ? cancel(true) : MapHoldOut{}; }
MapHoldOut MapHold::closed() { return holding_ ? cancel(false) : MapHoldOut{}; }

MapHoldOut MapHold::wheel() {
    MapHoldOut o;
    o.swallow = holding_;
    return o;
}

MapHoldOut MapHold::tick(uint64_t now_ms) {
    MapHoldOut o;
    if (holding_ && now_ms >= start_ms_ && now_ms - start_ms_ >= hold_ms_) {
        holding_ = false;  // done while the button is still down: its release stays ours
        o.done = true;
        o.x = x_;
        o.z = z_;
    }
    return o;
}

float MapHold::progress(uint64_t now_ms) const {
    if (!holding_ || !hold_ms_ || now_ms < start_ms_) return 0.0f;
    return std::min(1.0f, static_cast<float>(now_ms - start_ms_) / static_cast<float>(hold_ms_));
}

}  // namespace emf::core
