#pragma once
// Fixtures for the content loader's tests (tests/test_content_*.cpp): synthetic KFC4 headers and small files.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace content_fixtures {

// Writes `text` to `path` (a new file each time, its folders made first).
inline void put_file(const std::wstring& path, const std::string& text) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

// The file's bytes ("" when it cannot be read).
inline std::string file_text(const std::wstring& path) {
    std::string text;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return text;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    return text;
}

inline void put_u32(std::string& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }

// A KFC4 header as the game's .kfc starts (offsets are relative to their own field): 64 bytes, the version string at
// byte 48, the container table said to be at byte 64 (past these bytes: the reader never looks there).
inline std::string kfc_bytes(const std::string& version, uint32_t containers) {
    std::string b(48, '\0');
    put_u32(b, 0, 0x3443464B);                                  // "KFC4"
    put_u32(b, 4, static_cast<uint32_t>(48 + version.size()));  // size
    put_u32(b, 16, 48 - 16);                                    // version: offset from byte 16
    put_u32(b, 20, static_cast<uint32_t>(version.size()));      //          length
    put_u32(b, 24, 64 - 24);                                    // containers: offset from byte 24
    put_u32(b, 28, containers);                                 //             count
    b += version;
    if (b.size() < 64) b.resize(64, '\0');
    return b;
}

}  // namespace content_fixtures
