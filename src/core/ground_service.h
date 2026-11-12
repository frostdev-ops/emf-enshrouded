#pragma once
// Markers step 2 (API v34): ground_at: the ground under a spot, from the game's voxel ray cast after each server step
// (GroundSearch decides when a reading counts). A place that does not load within 12 s is answered "not loaded" (a
// caller with a player to move may hop there and ask again: the Teleports mod does). The game is reached only through
// Deps; core.cpp gives the real ones (the voxel grid, the reset box, Sig::VoxelRay). Server world's thread only.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/fog_check.h"
#include "core/ground_search.h"
#include "emf/emf_api.h"

namespace emf::core {

constexpr size_t kGroundRequestsMax = 64;
// The water over a landing (private research water_2026-11-12): read half a metre over the ground, 1 m down at most; a
// surface at least 1 m over the ground is water to land on (a shallower pond is ground).
constexpr double kWaterProbeUp = 0.5, kWaterLandDepth = 1.0;
constexpr double kWaterMaxDepth = 64.0;  // a deeper "surface" is not believed (a misread plane, a waterfall's top)
constexpr float kWaterMaxDown = 1.0f;

struct WaterReading {
    bool loaded = false;  // the world's water has loaded (else unknown)
    bool water = false;   // a surface was found
    double surface_y = 0;
};
constexpr uint64_t kFogLoadMs = 5000;  // the fog at a landing that does not load in this time stays unknown

struct GroundAnswer {
    uint32_t status = EMF_GROUND_FAILED;  // EMF_GROUND_*
    double x = 0, y = 0, z = 0;
    double hop_y = 0;  // EMF_GROUND_NOT_LOADED: a height over the spot where a player may wait for it to load
    std::string why;
    uint32_t fog = EMF_FOG_UNKNOWN;  // OK: the shroud at the landing (fog_check.h)
    float deadly = 0;
    bool water = false;  // OK: the spot is under water at least kWaterLandDepth deep: a player lands at water_y
    double water_y = 0;
};

class GroundService {
public:
    using Callback = std::function<void(EmfWorld*, const GroundAnswer&)>;
    struct Deps {
        // the world's limits now: false (why) when they cannot be read
        std::function<bool(EmfWorld*, GroundLimits*, std::string* why)> limits;
        // one ray straight down at (x, z) from from_y to to_y: false when it could not be cast
        std::function<bool(EmfWorld*, double x, double z, double from_y, double to_y, RayResult*)> ray;
        // the game's fog reading at a point: false when it cannot be read on this build (the fog stays unknown)
        std::function<bool(EmfWorld*, double x, double y, double z, FogReading*)> fog;
        // the water at a point: the surface over it, or within max_down under it; false when it cannot be read
        std::function<bool(EmfWorld*, double x, double y, double z, float max_down, WaterReading*)> water;
        std::function<uint64_t()> now_ms;
    };
    explicit GroundService(Deps d) : d_(std::move(d)) {}

    // Queued for the next server steps of `world`; false (why) when refused at once.
    bool request(EmfWorld* world, double x, double z, Callback cb, std::string* why);
    void on_server_tick(EmfWorld* world);  // after a server step (no world jobs running)
    void world_reset();                     // a new server world: what waits is answered (not ok)
    size_t waiting() const { return reqs_.size(); }

private:
    struct Req {
        EmfWorld* world;
        GroundSearch search;
        Callback cb;
        // after the ground: the fog at its points, read until loaded (kFogLoadMs at most)
        bool fog_phase = false;
        double ground_y = 0;
        bool water = false;  // the landing is on water (water_y)
        double water_y = 0;
        uint64_t fog_start = 0;
        std::vector<std::array<double, 3>> points;
        std::vector<FogVerdict> verdicts;  // per point (EMF_FOG_UNKNOWN: not loaded yet)
    };
    // true when the request is answered (fog read, or not readable, or out of time)
    bool fog_step(Req& r, uint64_t now, GroundAnswer* a);
    Deps d_;
    std::vector<Req> reqs_;
};

}  // namespace emf::core
