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
    MapInputResult message(UINT msg, WPARAM wp, LPARAM lp, const MapInputEnv& env);
    void frame(const MapInputEnv& env);
    MapRing ring(uint64_t now_ms) const;

private:
    void finish(const MapHoldOut& o);
    mutable std::mutex m_;
    MapHold hold_;
    std::function<void(double, double)> done_;
    std::function<void(const EmfMapClick&)> click_;
    bool right_ours_ = false;  // a right-click swallowed during a hold: its release too
};

// The game's one gesture (the window procedure, the overlay and the core share it).
MapInput& map_input();
// A server end announced a map action to this game (map_actions.h); off: Shift+clicks are the game's.
void map_input_set_announced(bool on);
bool map_input_announced();
// The environment now: the map's view and sight from the capture, Shift from the keyboard, the window's client size.
MapInputEnv map_input_env(float client_w, float client_h, uint64_t now_ms);

}  // namespace emf::core
