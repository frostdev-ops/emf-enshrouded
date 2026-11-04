#pragma once
// Pictures in mod windows (API v26): a PNG file decoded with Windows Imaging Component (no other library) into RGBA8.
#include <cstdint>
#include <string>
#include <vector>

namespace emf::core {

struct Rgba {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;  // w * h * 4 bytes, rows top to bottom, straight (not premultiplied) alpha
};

// True and `out` filled when `file` is a PNG WIC can read, at most `max_side` pixels wide and high (checked before its
// pixels are allocated); else false and `why` says what failed. Any thread (COM is started on it when needed).
bool decode_png(const std::wstring& file, Rgba* out, std::string* why, uint32_t max_side = 16384);

}  // namespace emf::core
