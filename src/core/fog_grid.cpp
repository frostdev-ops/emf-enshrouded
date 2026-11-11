// Markers step 3 (fog_grid.h).
#include "core/fog_grid.h"

#include <cmath>
#include <cstring>

namespace emf::core {

namespace {

uint32_t blocks_for(float metres) {
    return static_cast<uint32_t>(std::ceil(metres / kFogPixelMetres / kFogBlockSide));
}

uint64_t checksum(const std::vector<uint8_t>& v) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a over 8-byte words
    const size_t n = v.size() / 8;
    for (size_t i = 0; i < n; ++i) {
        uint64_t w;
        std::memcpy(&w, v.data() + i * 8, 8);
        h = (h ^ w) * 1099511628211ull;
    }
    for (size_t i = n * 8; i < v.size(); ++i) h = (h ^ v[i]) * 1099511628211ull;
    return h;
}

}  // namespace

bool fog_header_ok(const FogHeader& h) {
    if (!std::isfinite(h.w) || !std::isfinite(h.h) || h.w <= 0 || h.h <= 0 || h.w > 100000 || h.h > 100000) return false;
    if (!h.data || h.cols == 0 || h.rows == 0) return false;
    if (h.cols != blocks_for(h.w) || h.rows != blocks_for(h.h)) return false;
    return h.blocks == static_cast<uint64_t>(h.cols) * h.rows;
}

uint8_t fog_pixel(const FogHeader& h, uint32_t px, uint32_t py) {
    if (px >= h.cols * kFogBlockSide || py >= h.rows * kFogBlockSide || !h.data) return 0;
    const uint64_t block = static_cast<uint64_t>(py / kFogBlockSide) * h.cols + px / kFogBlockSide;
    if (block >= h.blocks) return 0;
    return h.data[block * kFogBlockSide * kFogBlockSide + (py % kFogBlockSide) * kFogBlockSide + px % kFogBlockSide];
}

float fog_explored(const FogHeader& h, double x, double z) {
    if (!std::isfinite(x) || !std::isfinite(z) || x < 0 || z < 0 || x >= h.w || z > h.h) return 0.0f;
    const double fy = (static_cast<double>(h.h) - z) / kFogPixelMetres;  // row 0 is the north edge
    if (fy < 0) return 0.0f;
    return fog_pixel(h, static_cast<uint32_t>(x / kFogPixelMetres), static_cast<uint32_t>(fy)) / 255.0f;
}

bool FogStore::update(const FogHeader& h, uint64_t now_ms) {
    if (!fog_header_ok(h)) {
        clear();
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_);
        if (have_ && now_ms >= taken_ms_ && now_ms - taken_ms_ < kFogRefreshMs) return false;
    }
    std::vector<uint8_t> copy(h.data, h.data + h.blocks * kFogBlockSide * kFogBlockSide);
    const uint64_t sum = checksum(copy);
    std::lock_guard<std::mutex> lock(m_);
    taken_ms_ = now_ms;
    if (have_ && sum == sum_ && h.w == h_.w && h.h == h_.h) return false;
    copy_ = std::move(copy);
    h_ = h;
    h_.data = copy_.data();
    sum_ = sum;
    have_ = true;
    ++version_;
    return true;
}

bool FogStore::due(uint64_t now_ms) const {
    std::lock_guard<std::mutex> lock(m_);
    return !have_ || now_ms < taken_ms_ || now_ms - taken_ms_ >= kFogRefreshMs;
}

void FogStore::clear() {
    std::lock_guard<std::mutex> lock(m_);
    if (have_) ++version_;
    have_ = false;
    copy_.clear();
    h_ = FogHeader{};
}

bool FogStore::explored(double x, double z, float* out) const {
    std::lock_guard<std::mutex> lock(m_);
    if (!have_ || !out) return false;
    *out = fog_explored(h_, x, z);
    return true;
}

float FogStore::world_size() const {
    std::lock_guard<std::mutex> lock(m_);
    return have_ ? h_.w : 0.0f;
}

uint32_t FogStore::version() const {
    std::lock_guard<std::mutex> lock(m_);
    return version_;
}

bool FogStore::picture(std::vector<uint8_t>* out, uint32_t* side) const {
    std::lock_guard<std::mutex> lock(m_);
    if (!have_ || !out || !side) return false;
    const uint32_t px = static_cast<uint32_t>(std::ceil(h_.w / kFogPixelMetres));
    const uint32_t s = (px + 1) / 2;
    out->assign(static_cast<size_t>(s) * s, 0);
    for (uint32_t y = 0; y < s; ++y)
        for (uint32_t x = 0; x < s; ++x) {
            const uint32_t sum = fog_pixel(h_, 2 * x, 2 * y) + fog_pixel(h_, 2 * x + 1, 2 * y) +
                                 fog_pixel(h_, 2 * x, 2 * y + 1) + fog_pixel(h_, 2 * x + 1, 2 * y + 1);
            (*out)[static_cast<size_t>(y) * s + x] = static_cast<uint8_t>(sum / 4);
        }
    *side = s;
    return true;
}

}  // namespace emf::core
