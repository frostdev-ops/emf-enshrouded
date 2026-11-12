#pragma once
// The shroud at a spot, on the server (a map teleport's landing): the game's fog classifier rebuilt from one reading of
// its FogSample (weights w[0] no fog, w[1..16] fog levels 0..15, w[17] removed fog): 0.75 of one kind decides; a fog
// level whose index is above the world's progress level is deadly. fog_worst combines the points read around a landing.
// Game-free. Research: private docs/research/fog_query_2026-11-12.md.
#include <array>
#include <cstdint>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

constexpr uint32_t EMF_FOG_MIXED = 254;  // internal: no 0.75 majority (an edge)
constexpr float kFogDeadlyRefuse = 0.25f;  // deadly fog weight that counts as deadly (stricter than the game)

struct FogReading {
    bool loaded = false;
    uint32_t progress = 0;  // the world's progress level (ServerProgressSystem +0x14)
    float w[18] = {};
};

struct FogVerdict {
    uint32_t type = EMF_FOG_UNKNOWN;
    float deadly = 0;  // the weight of fog levels above the progress
};

FogVerdict fog_classify(const FogReading& r);
FogVerdict fog_worst(const std::vector<FogVerdict>& v);
// Where to read for a landing at (x, ground_y, z): 1 m and 2 m over it, and 3 m to each side at 1 m.
std::vector<std::array<double, 3>> fog_points(double x, double ground_y, double z);

}  // namespace emf::core
