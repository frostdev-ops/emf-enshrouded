#pragma once
// Small helpers shared by the content loader's units (content_*.h, eml_mods.h): UTF-8 <-> UTF-16, JSON strings, and
// file reads and stamps through Windows' own calls (no C runtime file I/O, no exceptions from Windows errors).
#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace emf::loader::cio {

inline std::string to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

inline std::wstring from_utf8(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// `s` (UTF-8) as a JSON string, quotes included.
inline std::string json_string(std::string_view s) {
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += ch;
                }
        }
    }
    return out + "\"";
}

// A file's bytes; nullopt when it cannot be opened or read, or is bigger than `limit`.
inline std::optional<std::string> read_file(const std::wstring& path, uint64_t limit) {
    const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::nullopt;
    std::optional<std::string> out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && static_cast<uint64_t>(size.QuadPart) <= limit) {
        std::string text(static_cast<size_t>(size.QuadPart), '\0');
        DWORD got = 0;
        if (text.empty() || (ReadFile(f, text.data(), static_cast<DWORD>(text.size()), &got, nullptr) && got == text.size()))
            out = std::move(text);
    }
    CloseHandle(f);
    return out;
}

// What GetFileAttributesExW says about a path.
struct Stat {
    bool exists = false;
    bool dir = false;
    bool reparse = false;  // a junction or symbolic link (never followed by the content loader)
    uint64_t size = 0;
    uint64_t written = 0;  // last write time, UTC, a FILETIME as one number
};

inline Stat stat(const std::wstring& path) {
    Stat s;
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return s;
    s.exists = true;
    s.dir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    s.reparse = (d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    s.size = (static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    s.written = (static_cast<uint64_t>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
    return s;
}

// Writes `text` as `path`.tmp and renames it over `path`, so a reader sees the old file or the new one, never half of
// one. A file another process just closed may stay locked for a moment (a virus scanner): tried again for 3 s.
inline bool write_file_atomic(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (attempt) Sleep(100);
        DeleteFileW(tmp.c_str());  // a leftover (a crash); a link there is removed, never written through
        const HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        DWORD n = 0;
        const BOOL ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &n, nullptr) && n == text.size() &&
                        FlushFileBuffers(f);
        CloseHandle(f);
        if (!ok) continue;
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    }
    DeleteFileW(tmp.c_str());
    return false;
}

// `dir` + "\" + `name`.
inline std::wstring join(const std::wstring& dir, std::wstring_view name) {
    std::wstring out = dir;
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/') out += L'\\';
    out.append(name);
    return out;
}

}  // namespace emf::loader::cio
