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
    auto s = std::make_shared<FogSnapshot>();
    s->bytes = copy_;
    s->header = h_;
    s->header.data = s->bytes.data();
    snap_ = std::move(s);
    return true;
}

std::shared_ptr<const FogSnapshot> FogStore::snapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    return snap_;
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
    snap_.reset();
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

bool minimap_wanted(bool has_canvas, uint64_t asked_ms, uint64_t now_ms) {
    return has_canvas || (asked_ms != 0 && now_ms >= asked_ms && now_ms - asked_ms < kMinimapAskedMs);
}

}  // namespace emf::core
