#pragma once
// Markers step 2 (API v34): the ground under a map spot, from the game's voxel ray cast after each server step. Two
// ticks that agree within 0.5 m, under the reset box's top less 4 m and at least 1 m under the ray's start, are the
// ground. A spot not loaded within 12 s gets one hop high over it (the game loads the place around a teleported
// player), then the ray again; no ground 2 s after the hop: back. Game-free. Private research map_click_2026-11-11 3.7.
#include <cstdint>
#include <string>

namespace emf::core {

constexpr double kGroundWorldSize = 10240.0;
constexpr uint64_t kGroundLoadMs = 12000, kGroundAfterHopMs = 2000;  // the game takes about 5 s to load a column (live)

struct GroundLimits {
    double box_top = 0;      // the scene's reset box (box 0) top
    double grid_top = 0;     // the voxel grid's top and bottom (world y)
    double grid_bottom = 0;
};

struct RayResult {
    bool loaded = false;  // the cells the ray crossed were all loaded
    bool hit = false;
    double y = 0;  // the hit's height
};

// x, z inside the main scene (0..10 240 m on both); `why` says why not.
bool ground_spot_ok(double x, double z, std::string* why);

class GroundSearch {
public:
    enum class Step { Wait, Accept, Hop, Back, Fail };
    struct Out {
        Step step = Step::Wait;
        double y = 0;  // Accept: the ground; Hop: the hop's height
        std::string why;
    };
    GroundSearch(double x, double z, GroundLimits limits, uint64_t now_ms);
    double x() const { return x_; }
    double z() const { return z_; }
    double ray_start() const { return lim_.grid_top + 0.25; }
    double ray_end() const { return lim_.grid_bottom; }
    Out on_ray(const RayResult& r, uint64_t now_ms);  // after each server step
    Out on_hopped(bool moved, uint64_t now_ms);       // the hop's teleport answered

private:
    double x_, z_;
    GroundLimits lim_;
    uint64_t start_ms_;
    bool hop_asked_ = false, hopped_ = false;
    uint64_t hopped_ms_ = 0;
    bool have_candidate_ = false;
    double candidate_ = 0;
};

}  // namespace emf::core
