#pragma once
// The header of the game's content index, <name>.kfc ("KFC4"): its version string and how many containers
// (<name>_NNN.dat) it uses. The content loader reads it to tell a vanilla .kfc from one EML or ShroudForge patched in
// place (content_base.h). Offsets in bytes, little-endian; each offset is relative to its own field:
//    0  u32        magic 0x3443464B ("KFC4")
//    4  u32        size
//    8  u32        unknown, then 4 bytes of padding
//   16  u32 + u32  version: offset (from byte 16) and length; the string at 16 + offset
//   24  u32 + u32  containers: offset (from byte 24) and count
// Game build 1273043: version "1273043|^/game38/branches/release_1.0|2026-10-20T14:55:29.904125Z", 55 containers.
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace emf::loader {

struct KfcHeader {
    std::string version;
    uint32_t containers = 0;
};

constexpr uint32_t kKfcMagic = 0x3443464B;
constexpr size_t kKfcHeadBytes = 64 * 1024;   // what read_kfc_header reads
constexpr uint32_t kKfcMaxVersion = 4096;     // a longer version string is not a header EMF knows

namespace detail {
inline uint32_t kfc_u32(std::string_view b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}
}  // namespace detail

// nullopt when `bytes` do not start with a KFC4 header whose version string lies inside them. Reads only within them.
inline std::optional<KfcHeader> parse_kfc_header(std::string_view bytes) {
    if (bytes.size() < 32 || detail::kfc_u32(bytes, 0) != kKfcMagic) return std::nullopt;
    const uint64_t at = 16ull + detail::kfc_u32(bytes, 16);
    const uint32_t length = detail::kfc_u32(bytes, 20);
    if (length > kKfcMaxVersion || at + length > bytes.size()) return std::nullopt;
    KfcHeader h;
    h.version.assign(bytes.data() + at, length);
    h.containers = detail::kfc_u32(bytes, 28);
    return h;
}

// The header of the file at `path` (its first 64 KiB); nullopt when it cannot be read or is not a KFC4 header.
inline std::optional<KfcHeader> read_kfc_header(const std::wstring& path) {
    const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::nullopt;
    std::string head(kKfcHeadBytes, '\0');
    DWORD got = 0;
    const BOOL ok = ReadFile(f, head.data(), static_cast<DWORD>(head.size()), &got, nullptr);
    CloseHandle(f);
    if (!ok) return std::nullopt;
    head.resize(got);
    return parse_kfc_header(head);
}

}  // namespace emf::loader
