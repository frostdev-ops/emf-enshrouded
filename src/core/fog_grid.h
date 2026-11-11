#pragma once
// Markers step 3 (API v35): the game's fog of war (the client's FogOfWar singleton: +0 float2 world size, +8 u32 x2
// block counts, +0x18 data, +0x20 block count; blocks of 32 x 32 one-byte pixels of 8 m, row-major by block, row 0 the
// north edge; 0 unexplored .. 255 explored). FogStore keeps a copy (refreshed at most twice a second) for fog_at and
// a picture with a quarter of the pixels for the overlay. Game-free.
#include <cstdint>
#include <mutex>
#include <vector>

namespace emf::core {

constexpr double kFogPixelMetres = 8.0;
constexpr uint32_t kFogBlockSide = 32;
constexpr uint64_t kFogRefreshMs = 500;

struct FogHeader {
    float w = 0, h = 0;          // world size, metres
    uint32_t cols = 0, rows = 0;  // blocks
    const uint8_t* data = nullptr;
    uint64_t blocks = 0;
};

bool fog_header_ok(const FogHeader& h);
uint8_t fog_pixel(const FogHeader& h, uint32_t px, uint32_t py);  // 0 outside
float fog_explored(const FogHeader& h, double x, double z);        // 0..1; 0 outside

class FogStore {
public:
    // The client world's thread: true when a new copy was taken (it changed).
    bool update(const FogHeader& h, uint64_t now_ms);
    void clear();
    bool due(uint64_t now_ms) const;  // a new copy would be taken now (the glue reads the game's grid only then)
    bool explored(double x, double z, float* out) const;
    float world_size() const;
    uint32_t version() const;  // changes with every new copy
    // A picture with half the pixels on each side (each the mean of 2 x 2), row 0 north: false without a grid.
    bool picture(std::vector<uint8_t>* out, uint32_t* side) const;

private:
    mutable std::mutex m_;
    FogHeader h_;              // data -> copy_
    std::vector<uint8_t> copy_;
    uint64_t taken_ms_ = 0;
    uint64_t sum_ = 0;
    uint32_t version_ = 0;
    bool have_ = false;
};

}  // namespace emf::core
