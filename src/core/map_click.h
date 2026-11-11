#pragma once
// Markers step 2 (API v34): the map screen's mouse in world coordinates (the inverse of the mapping the map markers
// use, marker_ui.cpp) and the Shift + hold gesture of map actions: which clicks are EMF's (swallowed: the game never
// sees them) and when a hold is done. Game-free. Private research map_click_2026-11-11 1-2 and 5.
#include <cstdint>

#include "core/marker_capture.h"

namespace emf::core {

constexpr uint64_t kMapHoldMs = 1000;

// The world x, z under (ui_x, ui_y) (UI units, as doPlayStateUi() lays out) on the map screen: false when the view has
// no size, the point is outside MapCenteringFrame, or the spot is outside the main scene's 0..10 240 m.
bool map_world_at(const MapView& view, float ui_x, float ui_y, double* x, double* z);

struct MapHoldOut {
    bool swallow = false;       // the game does not get this input
    bool rewrite_down = false;  // pass a double-click on as a plain button-down (the game never saw the click before)
    bool started = false, cancelled = false, done = false;
    double x = 0, z = 0;  // with done: the spot
};

// One gesture at a time. The window procedure feeds it the left button's messages, right-click, Esc, the wheel and the
// map closing; tick() runs every frame. A hold that ends keeps swallowing the left button until it is released.
class MapHold {
public:
    explicit MapHold(uint64_t hold_ms = kMapHoldMs) : hold_ms_(hold_ms) {}
    void set_enabled(bool on) { enabled_ = on; }  // a server end announced a map action
    void set_hold_ms(uint64_t ms) { hold_ms_ = ms ? ms : kMapHoldMs; }  // the next hold's time (0: the default)
    bool enabled() const { return enabled_; }
    MapHoldOut down(bool shift, bool on_map, double x, double z, uint64_t now_ms);
    MapHoldOut dblclk(bool shift, bool on_map, double x, double z, uint64_t now_ms);
    MapHoldOut up(uint64_t now_ms);
    MapHoldOut right();
    MapHoldOut esc();
    MapHoldOut wheel();
    MapHoldOut closed();
    MapHoldOut tick(uint64_t now_ms);
    bool holding() const { return holding_; }
    float progress(uint64_t now_ms) const;  // 0..1 while holding
    double x() const { return x_; }
    double z() const { return z_; }

private:
    MapHoldOut cancel(bool swallow);
    uint64_t hold_ms_;
    bool enabled_ = true;
    bool holding_ = false;
    bool button_ours_ = false;      // the left button's down was swallowed: its up is ours too
    bool rewrite_next_ = false;     // the last click was swallowed: a double-click now is the game's first down
    uint64_t start_ms_ = 0;
    double x_ = 0, z_ = 0;
};

}  // namespace emf::core
