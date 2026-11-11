#pragma once
// Markers step 2 (API v34): the map gesture between the game window's messages and MapHold. The window procedure asks
// message() for the left and right button, double-click and wheel messages: `swallow` keeps one from the game,
// `rewrite_down` passes a double-click on as a plain press. frame() runs every overlay frame (the hold's time, the map
// closing). A finished hold calls `done(x, z)` (the overlay thread or the window thread). Thread-safe (one mutex).
// Esc needs nothing of its own: the game closes the map on it, and a closed map cancels the hold.
#include <windows.h>

#include <cstdint>
#include <functional>
#include <mutex>

#include "core/map_click.h"
#include "emf/emf_api.h"

namespace emf::core {

struct MapInputEnv {
    bool map_open = false;   // the game's map screen was seen within kMarkerFreshMs
    bool announced = false;  // a server end announced a map action
    bool shift = false;      // Shift is down
    bool alt = false;        // Alt is down (on_map_click's EMF_MAP_ALT)
    bool captured = false;   // EMF's console, wheel or a mod window takes the mouse: a hold ends
    float client_w = 0, client_h = 0;  // the game window's client area, pixels
    MapView view{};
    uint64_t now_ms = 0;
};

struct MapInputResult {
    bool swallow = false;
    bool rewrite_down = false;
};

struct MapRing {
    bool holding = false;
    float progress = 0;
    double x = 0, z = 0;  // the spot (the overlay maps it forward to draw the ring)
};

class MapInput {
public:
    explicit MapInput(std::function<void(double x, double z)> done = nullptr) : done_(std::move(done)) {}
    void set_done(std::function<void(double x, double z)> f);
    void set_click_listener(std::function<void(const EmfMapClick&)> f);
    void set_hold_ms(uint64_t ms);
    void cancel();  // focus lost: a hold ends (its release stays EMF's)  // what the server announced (0: 1 s); a hold under way keeps its time
    MapInputResult message(UINT msg, WPARAM wp, LPARAM lp, const MapInputEnv& env);
    void frame(const MapInputEnv& env);
    MapRing ring(uint64_t now_ms) const;

private:
    void finish(const MapHoldOut& o);
    void take_pending_hold();  // m_ held
    mutable std::mutex m_;
    MapHold hold_;
    std::function<void(double, double)> done_;
    std::function<void(const EmfMapClick&)> click_;
    uint64_t pending_hold_ms_ = 0;  // set during a hold: for the next one
    bool right_ours_ = false;  // a right-click swallowed during a hold: its release too
};

// The game's one gesture (the window procedure, the overlay and the core share it).
MapInput& map_input();
// A server end announced a map action to this game (map_actions.h); off: Shift+clicks are the game's.
void map_input_set_announced(bool on);
bool map_input_announced();
// The environment now: the map's view and sight from the capture, Shift from the keyboard, the window's client size.
MapInputEnv map_input_env(float client_w, float client_h, uint64_t now_ms);
// The map's layout counts as the open map only while it is at most kMapGestureFreshMs old (the 3D markers' 250 ms would
// let a hold finish after Esc closed the map).
constexpr uint64_t kMapGestureFreshMs = 120;
bool map_view_fresh(const MapView& v, uint64_t now_ms);

}  // namespace emf::core
