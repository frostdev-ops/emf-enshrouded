#pragma once
// The content redirect spike (docs/engine_parts/file_redirect_spike.md), the parts without side effects: which paths the
// loader sends to the shadow copy <game>\emf\content\client\, the lines of emf\file_opens.log, and the [content]
// switches in emf\emf_core.ini. The hooks that use them are in file_hooks.h.
//
// A path is redirected when it names enshrouded.kfc, enshrouded.kfc_resources or enshrouded_<digits>.dat directly in
// the game folder (any case, either slash, relative to the current folder; with a \\?\ prefix only when it names the
// file literally, as Windows reads such a path: no "/", "." or ".."); the shadow path is <game>\emf\content\client\<the
// same name>. Whether that file exists is the caller's check.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emf::loader {

constexpr const wchar_t* kShadowSubdir = L"emf\\content\\client";

inline wchar_t ascii_lower(wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c; }

inline bool iequals_ascii(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    return true;
}

// enshrouded.kfc, enshrouded.kfc_resources and enshrouded_<digits>.dat, any case.
inline bool is_content_file_name(std::wstring_view n) {
    if (iequals_ascii(n, L"enshrouded.kfc") || iequals_ascii(n, L"enshrouded.kfc_resources")) return true;
    constexpr std::wstring_view head = L"enshrouded_", tail = L".dat";
    if (n.size() <= head.size() + tail.size()) return false;
    if (!iequals_ascii(n.substr(0, head.size()), head) || !iequals_ascii(n.substr(n.size() - tail.size()), tail))
        return false;
    for (const wchar_t c : n.substr(head.size(), n.size() - head.size() - tail.size()))
        if (c < L'0' || c > L'9') return false;
    return true;
}

inline bool is_slash(wchar_t c) { return c == L'\\' || c == L'/'; }

inline bool is_drive_letter(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); }

// An absolute path: "C:\a\b" or "\\server\share\a", backslashes only, no "." or "..", no trailing backslash (a bare
// root is "C:\" or "\\server\share"). `verbatim`: it came with the \\?\ prefix.
struct NormalPath {
    std::wstring path;
    bool verbatim = false;
};

// True when `path` means something only together with the current folder: relative ("a.txt", ".\a"), rooted ("\a")
// or drive-relative ("C:a").
inline bool path_needs_cwd(std::wstring_view p) {
    if (p.size() >= 2 && is_slash(p[0]) && is_slash(p[1])) return false;  // UNC or a \\?\ / \\.\ path
    if (p.size() >= 3 && is_drive_letter(p[0]) && p[1] == L':' && is_slash(p[2])) return false;
    return true;
}

namespace detail {
// Windows drops trailing dots and spaces from each name of a plain path ("a.kfc. " is "a.kfc").
inline std::wstring_view trim_name(std::wstring_view s) {
    if (s == L"." || s == L"..") return s;
    while (!s.empty() && (s.back() == L'.' || s.back() == L' ')) s.remove_suffix(1);
    return s;
}

// `root` ("C:" or "\\server\share") + the names in `rest` (either slash), with "." and ".." applied.
inline std::wstring join_names(const std::wstring& root, std::wstring_view rest, bool trim) {
    std::vector<std::wstring_view> names;
    size_t from = 0;
    while (from <= rest.size()) {
        size_t to = from;
        while (to < rest.size() && !is_slash(rest[to])) ++to;
        std::wstring_view name = rest.substr(from, to - from);
        if (trim) name = trim_name(name);
        if (name == L"..") {
            if (!names.empty()) names.pop_back();
        } else if (!name.empty() && name != L".") {
            names.push_back(name);
        }
        from = to + 1;
    }
    std::wstring out = root;
    if (names.empty() && root.size() == 2) out += L'\\';  // "C:\"
    for (const auto& n : names) {
        out += L'\\';
        out.append(n);
    }
    return out;
}

// Splits an absolute path (no prefix) into its root ("C:" or "\\server\share") and the rest; false when it is not one.
inline bool split_root(std::wstring_view p, std::wstring* root, std::wstring_view* rest) {
    if (p.size() >= 3 && is_drive_letter(p[0]) && p[1] == L':' && is_slash(p[2])) {
        *root = std::wstring(p.substr(0, 2));
        *rest = p.substr(3);
        return true;
    }
    if (p.size() >= 2 && is_slash(p[0]) && is_slash(p[1])) {
        size_t a = 2;
        while (a < p.size() && !is_slash(p[a])) ++a;  // the server
        if (a == 2 || a >= p.size()) return false;
        size_t b = a + 1;
        while (b < p.size() && !is_slash(p[b])) ++b;  // the share
        if (b == a + 1) return false;
        *root = L"\\\\" + std::wstring(p.substr(2, a - 2)) + L"\\" + std::wstring(p.substr(a + 1, b - a - 1));
        *rest = b < p.size() ? p.substr(b + 1) : std::wstring_view();
        return true;
    }
    return false;
}

// The names after a \\?\ path's root, as Windows takes them: true when each is an ordinary name (no "." or "..", no
// empty name between two backslashes; one trailing backslash is allowed). Windows does not resolve them there, so the
// redirect leaves such a path alone rather than guess what it opens.
inline bool literal_names_ok(std::wstring_view rest) {
    if (!rest.empty() && rest.back() == L'\\') rest.remove_suffix(1);
    if (rest.empty()) return true;
    size_t from = 0;
    while (from <= rest.size()) {
        size_t to = rest.find(L'\\', from);
        if (to == std::wstring_view::npos) to = rest.size();
        const std::wstring_view name = rest.substr(from, to - from);
        if (name.empty() || name == L"." || name == L"..") return false;
        from = to + 1;
    }
    return true;
}
}  // namespace detail

// `path` made absolute against `cwd` (the current folder, itself absolute; only read when the path needs it), the way
// Windows reads it; nullopt when that cannot be done here: empty, a device ("\\.\pipe\x", "\\?\Volume{..}"), relative
// without a current folder, or drive-relative on another drive ("D:a" when the current folder is on C:).
// A path with the exact prefix \\?\ is taken literally, as Windows takes it: backslashes only, each name as it is; one
// with "/", "." or ".." or an empty name after the prefix is nullopt (never redirected), and so is a "//?/" form.
inline std::optional<NormalPath> normalize_path(std::wstring_view p, std::wstring_view cwd) {
    if (p.empty()) return std::nullopt;
    NormalPath out;
    std::wstring root;
    std::wstring_view rest;
    std::wstring joined;  // keeps what `rest` points into alive
    bool trim = true;
    if (p.size() >= 4 && is_slash(p[0]) && is_slash(p[1]) && (p[2] == L'?' || p[2] == L'.') && is_slash(p[3])) {
        std::wstring_view r = p.substr(4);
        if (p[2] == L'?') {
            if (p.substr(0, 4) != L"\\\\?\\") return std::nullopt;  // "//?/": not the verbatim prefix
            if (r.find(L'/') != std::wstring_view::npos) return std::nullopt;
            out.verbatim = true;
            trim = false;
        }
        if (r.size() >= 4 && iequals_ascii(r.substr(0, 3), L"UNC") && is_slash(r[3])) {
            joined = L"\\\\" + std::wstring(r.substr(4));
            r = joined;
        }
        if (!detail::split_root(r, &root, &rest)) return std::nullopt;
        if (out.verbatim && !detail::literal_names_ok(rest)) return std::nullopt;
    } else if (!path_needs_cwd(p)) {
        if (!detail::split_root(p, &root, &rest)) return std::nullopt;
    } else {
        if (cwd.empty() || path_needs_cwd(cwd)) return std::nullopt;
        const auto base = normalize_path(cwd, L"");
        if (!base) return std::nullopt;
        std::wstring base_root;
        std::wstring_view base_rest;
        if (!detail::split_root(base->path, &base_root, &base_rest)) return std::nullopt;
        if (is_slash(p[0])) {  // "\a": the current folder's root
            joined = base_root + std::wstring(p);
        } else if (p.size() >= 2 && is_drive_letter(p[0]) && p[1] == L':') {  // "C:a"
            if (base_root.size() != 2 || ascii_lower(base_root[0]) != ascii_lower(p[0])) return std::nullopt;
            joined = base->path + L"\\" + std::wstring(p.substr(2));
        } else {
            joined = base->path + L"\\" + std::wstring(p);
        }
        if (!detail::split_root(joined, &root, &rest)) return std::nullopt;
    }
    out.path = detail::join_names(root, rest, trim);
    return out;
}

// The same path for Windows (case-insensitive, as NTFS compares names).
inline bool same_path(std::wstring_view a, std::wstring_view b) {
    while (!a.empty() && a.back() == L'\\') a.remove_suffix(1);
    while (!b.empty() && b.back() == L'\\') b.remove_suffix(1);
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) ==
           CSTR_EQUAL;
}

// "\\?\C:\..." or "\\?\UNC\server\share\..." for an absolute path.
inline std::wstring with_verbatim_prefix(const std::wstring& abs) {
    if (abs.size() >= 2 && abs[0] == L'\\' && abs[1] == L'\\') return L"\\\\?\\UNC\\" + abs.substr(2);
    return L"\\\\?\\" + abs;
}

// `dir` + "\" + `name`, without a second backslash after a root that has one already ("C:\").
inline std::wstring path_join(const std::wstring& dir, std::wstring_view name) {
    std::wstring out = dir;
    if (!out.empty() && out.back() != L'\\') out += L'\\';
    out.append(name);
    return out;
}

// The game folder as the redirect compares it: absolute and normal (normalize_path). "C:", which the loader gets for
// C:\enshrouded.exe (the exe's path up to its last backslash), is that drive's root "C:\". nullopt when it is not an
// absolute path.
inline std::optional<std::wstring> normal_game_dir(std::wstring_view dir) {
    std::wstring d(dir);
    if (d.size() == 2 && is_drive_letter(d[0]) && d[1] == L':') d += L'\\';
    const auto n = normalize_path(d, L"");
    if (!n) return std::nullopt;
    return n->path;
}

// <game>\emf\content\client
inline std::wstring shadow_folder(std::wstring_view game_dir) {
    const auto g = normal_game_dir(game_dir);
    return g ? path_join(*g, kShadowSubdir) : std::wstring();
}

// Where an open of `opened` goes with the redirect on: <game>\emf\content\client\<name> for a content file directly in
// the game folder, "" for anything else. A \\?\ path stays one, and so does a result too long for a plain path.
// `game` is the game folder as normal_game_dir gives it (the hooks work it out once, in start()).
inline std::wstring shadow_path_in(std::wstring_view opened, const std::wstring& game, std::wstring_view cwd) {
    if (game.empty()) return {};
    const auto o = normalize_path(opened, cwd);
    if (!o) return {};
    const size_t slash = o->path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return {};
    const std::wstring_view name = std::wstring_view(o->path).substr(slash + 1);
    if (!is_content_file_name(name)) return {};
    if (!same_path(std::wstring_view(o->path).substr(0, slash), game)) return {};
    std::wstring out = path_join(path_join(game, kShadowSubdir), name);
    if (o->verbatim || out.size() >= MAX_PATH) out = with_verbatim_prefix(out);
    return out;
}

// The same for a game folder in any form ("C:/Games/Enshrouded/", "C:").
inline std::wstring shadow_path(std::wstring_view opened, std::wstring_view game_dir, std::wstring_view cwd) {
    const auto g = normal_game_dir(game_dir);
    return g ? shadow_path_in(opened, *g, cwd) : std::wstring();
}

// The access rights an open may ask for and still go to the shadow copy: reading only.
constexpr DWORD kReadOnlyAccess = GENERIC_READ | GENERIC_EXECUTE | FILE_READ_DATA | FILE_READ_EA | FILE_READ_ATTRIBUTES |
                                  FILE_EXECUTE | READ_CONTROL | SYNCHRONIZE;

// Only an open that reads a file that is there goes to the shadow copy: every right it asks for is a read right (an
// allowlist, so a right not named here never passes), the file must exist (OPEN_EXISTING; any other disposition goes to
// the game's own path), no delete-on-close. The shadow's .dat files are hard links to the game's own: a write through
// one would change the game's file.
inline bool redirect_allowed(DWORD access, DWORD disposition, DWORD flags) {
    if ((access & ~kReadOnlyAccess) != 0) return false;
    if (disposition != OPEN_EXISTING) return false;
    return (flags & FILE_FLAG_DELETE_ON_CLOSE) == 0;
}

// ---- [content] in emf\emf_core.ini ----

// The hooks go only into the game client, an exe named enshrouded.exe (any case): not the dedicated server, not another
// program that loads the DLL. The tests run their stand-in game under that name.
inline bool is_game_client_exe(std::wstring_view path) {
    const size_t slash = path.find_last_of(L"\\/");
    return iequals_ascii(slash == std::wstring_view::npos ? path : path.substr(slash + 1), L"enshrouded.exe");
}

constexpr unsigned kDefaultLogSeconds = 300;

struct ContentSwitches {
    bool log_opens = false;      // log_opens=1: log the exe's file calls for the first log_seconds
    bool redirect_test = false;  // redirect_test=1: send the content files' opens to the shadow copy (and log them)
    unsigned log_seconds = kDefaultLogSeconds;
    bool hooks() const { return log_opens || redirect_test; }
};

// `get_int(section, key, fallback)` reads the ini like GetPrivateProfileIntW. Only 1 turns a switch on.
template <class GetInt>
ContentSwitches content_switches(GetInt get_int) {
    ContentSwitches s;
    s.log_opens = get_int(L"content", L"log_opens", 0) == 1;
    s.redirect_test = get_int(L"content", L"redirect_test", 0) == 1;
    const int seconds = get_int(L"content", L"log_seconds", static_cast<int>(kDefaultLogSeconds));
    s.log_seconds = seconds >= 1 && seconds <= 86400 ? static_cast<unsigned>(seconds) : kDefaultLogSeconds;
    return s;
}

inline ContentSwitches read_content_switches(const std::wstring& ini) {
    return content_switches([&](const wchar_t* section, const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini.c_str()));
    });
}

// ---- the lines of emf\file_opens.log ----

inline std::string utf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

struct FlagName {
    DWORD bit;
    const char* name;
};

// "0x<hex>" and, when any are set, the names of the known bits: "0x80000000(GENERIC_READ)".
inline std::string bits_text(DWORD v, const FlagName* names, size_t count) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%lx", static_cast<unsigned long>(v));
    std::string s = hex, list;
    for (size_t i = 0; i < count; ++i) {
        if ((v & names[i].bit) != names[i].bit) continue;
        if (!list.empty()) list += '|';
        list += names[i].name;
    }
    return list.empty() ? s : s + "(" + list + ")";
}

inline std::string access_text(DWORD v) {
    static const FlagName names[] = {
        {GENERIC_READ, "GENERIC_READ"}, {GENERIC_WRITE, "GENERIC_WRITE"}, {GENERIC_EXECUTE, "GENERIC_EXECUTE"},
        {GENERIC_ALL, "GENERIC_ALL"}, {MAXIMUM_ALLOWED, "MAXIMUM_ALLOWED"}, {SYNCHRONIZE, "SYNCHRONIZE"},
        {WRITE_OWNER, "WRITE_OWNER"}, {WRITE_DAC, "WRITE_DAC"}, {READ_CONTROL, "READ_CONTROL"}, {DELETE, "DELETE"},
        {FILE_READ_DATA, "READ_DATA"}, {FILE_WRITE_DATA, "WRITE_DATA"}, {FILE_APPEND_DATA, "APPEND_DATA"},
        {FILE_READ_EA, "READ_EA"}, {FILE_WRITE_EA, "WRITE_EA"}, {FILE_EXECUTE, "EXECUTE"},
        {FILE_READ_ATTRIBUTES, "READ_ATTRIBUTES"}, {FILE_WRITE_ATTRIBUTES, "WRITE_ATTRIBUTES"}};
    return bits_text(v, names, sizeof names / sizeof names[0]);
}

inline std::string share_text(DWORD v) {
    static const FlagName names[] = {{FILE_SHARE_READ, "READ"}, {FILE_SHARE_WRITE, "WRITE"}, {FILE_SHARE_DELETE, "DELETE"}};
    return bits_text(v, names, sizeof names / sizeof names[0]);
}

inline std::string flags_text(DWORD v) {
    static const FlagName names[] = {
        {FILE_FLAG_WRITE_THROUGH, "WRITE_THROUGH"}, {FILE_FLAG_OVERLAPPED, "OVERLAPPED"},
        {FILE_FLAG_NO_BUFFERING, "NO_BUFFERING"}, {FILE_FLAG_RANDOM_ACCESS, "RANDOM_ACCESS"},
        {FILE_FLAG_SEQUENTIAL_SCAN, "SEQUENTIAL_SCAN"}, {FILE_FLAG_DELETE_ON_CLOSE, "DELETE_ON_CLOSE"},
        {FILE_FLAG_BACKUP_SEMANTICS, "BACKUP_SEMANTICS"}, {FILE_FLAG_POSIX_SEMANTICS, "POSIX_SEMANTICS"},
        {FILE_FLAG_OPEN_REPARSE_POINT, "OPEN_REPARSE_POINT"}, {FILE_FLAG_OPEN_NO_RECALL, "OPEN_NO_RECALL"},
        {FILE_ATTRIBUTE_READONLY, "READONLY"}, {FILE_ATTRIBUTE_HIDDEN, "HIDDEN"}, {FILE_ATTRIBUTE_SYSTEM, "SYSTEM"},
        {FILE_ATTRIBUTE_ARCHIVE, "ARCHIVE"}, {FILE_ATTRIBUTE_NORMAL, "NORMAL"}, {FILE_ATTRIBUTE_TEMPORARY, "TEMPORARY"}};
    return bits_text(v, names, sizeof names / sizeof names[0]);
}

inline std::string disposition_text(DWORD v) {
    switch (v) {
        case CREATE_NEW: return "CREATE_NEW";
        case CREATE_ALWAYS: return "CREATE_ALWAYS";
        case OPEN_EXISTING: return "OPEN_EXISTING";
        case OPEN_ALWAYS: return "OPEN_ALWAYS";
        case TRUNCATE_EXISTING: return "TRUNCATE_EXISTING";
        default: return std::to_string(v);
    }
}

inline std::string handle_text(HANDLE h) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)));
    return buf;
}

inline std::string hex_text(unsigned long v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%lx", v);
    return buf;
}

// The start of every line: "+<ms since the hooks went in>ms t<thread id> <function>".
struct LineHead {
    unsigned long long ms;
    unsigned long tid;
    const char* fn;
};

inline std::string head_text(const LineHead& h) {
    return "+" + std::to_string(h.ms) + "ms t" + std::to_string(h.tid) + " " + h.fn;
}

// ` "<path>"`, then ` => "<shadow>"` when it was redirected, or ` [<note>]` when the log says why not.
inline std::string path_text(std::wstring_view path, std::wstring_view shadow, std::string_view note) {
    std::string s = " \"" + utf8(path) + "\"";
    if (!shadow.empty()) s += " => \"" + utf8(shadow) + "\"";
    if (!note.empty()) s += " [" + std::string(note) + "]";
    return s;
}

inline std::string open_line(const LineHead& h, std::wstring_view path, std::wstring_view shadow, std::string_view note,
                             DWORD access, DWORD share, DWORD disposition, DWORD flags, HANDLE result, DWORD error) {
    return head_text(h) + path_text(path, shadow, note) + " access=" + access_text(access) + " share=" +
           share_text(share) + " disp=" + disposition_text(disposition) + " flags=" + flags_text(flags) + " -> " +
           (result == INVALID_HANDLE_VALUE ? "error " + std::to_string(error) : "handle " + handle_text(result)) +
           "\r\n";
}

inline std::string attributes_line(const LineHead& h, std::wstring_view path, std::wstring_view shadow,
                                   std::string_view note, DWORD attrs, DWORD error) {
    return head_text(h) + path_text(path, shadow, note) + " -> " +
           (attrs == INVALID_FILE_ATTRIBUTES ? "error " + std::to_string(error) : "attrs " + hex_text(attrs)) + "\r\n";
}

inline std::string attributes_ex_line(const LineHead& h, std::wstring_view path, std::wstring_view shadow,
                                      std::string_view note, int level, bool ok, const WIN32_FILE_ATTRIBUTE_DATA* data,
                                      DWORD error) {
    std::string s = head_text(h) + path_text(path, shadow, note) + " level=" + std::to_string(level) + " -> ";
    if (!ok) return s + "error " + std::to_string(error) + "\r\n";
    if (level != GetFileExInfoStandard || !data) return s + "ok\r\n";
    const unsigned long long size = (static_cast<unsigned long long>(data->nFileSizeHigh) << 32) | data->nFileSizeLow;
    return s + "attrs " + hex_text(data->dwFileAttributes) + " size " + std::to_string(size) + "\r\n";
}

// `extra`: FindFirstFileExW's arguments ("level=1 search=0 flags=0x2"), "" for FindFirstFileW.
inline std::string find_first_line(const LineHead& h, std::wstring_view pattern, std::string_view extra, HANDLE result,
                                   std::wstring_view first, DWORD error) {
    std::string s = head_text(h) + path_text(pattern, L"", "");
    if (!extra.empty()) s += " " + std::string(extra);
    if (result == INVALID_HANDLE_VALUE) return s + " -> error " + std::to_string(error) + "\r\n";
    return s + " -> handle " + handle_text(result) + " first \"" + utf8(first) + "\"\r\n";
}

inline std::string find_next_line(const LineHead& h, HANDLE find, bool ok, std::wstring_view name, DWORD error) {
    return head_text(h) + " handle " + handle_text(find) + " -> " +
           (ok ? "\"" + utf8(name) + "\"" : "error " + std::to_string(error)) + "\r\n";
}

inline std::string set_dir_line(const LineHead& h, std::wstring_view path, bool ok, DWORD error) {
    return head_text(h) + path_text(path, L"", "") + " -> " + (ok ? std::string("ok") : "error " + std::to_string(error)) +
           "\r\n";
}

}  // namespace emf::loader
