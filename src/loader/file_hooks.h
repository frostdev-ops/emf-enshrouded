#pragma once
// The content redirect spike's hooks (docs/engine_parts/file_redirect_spike.md). With [content] log_opens=1 or
// redirect_test=1 in emf\emf_core.ini the loader puts them into enshrouded.exe's import table in DllMain (iat_patch.h)
// and leaves them there for the life of the process:
//   CreateFileW, CreateFileA, GetFileAttributesW, GetFileAttributesA, GetFileAttributesExW   logged; with
//       redirect_test=1 the content files (file_redirect.h) go to <game>\emf\content\client\<name> when that file is there
//   FindFirstFileW, FindFirstFileExW, FindNextFileW, SetCurrentDirectoryW                    logged only
// The lines go to emf\file_opens.log, up to max_lines lines: with log_opens=1 every call for the first log_seconds; with
// redirect_test=1 the content files' calls and the current-folder changes (relative paths depend on them) for as long
// as the game runs. With log_opens=1 alone nothing is logged after log_seconds.
//
// With redirect_test=1, start() first checks the shadow copy's record (emf\content\client\shadow.txt) against the
// game's files and turns the redirect off when the game changed since the copy was made (shadow_problem).
//
// Each hook calls the function that was in the slot before (kernel32, or another tool's hook) and leaves the caller
// the Windows error that function left, whatever the logging did in between.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "file_redirect.h"
#include "iat_patch.h"

namespace emf::loader::file_hooks {

using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using GetFileAttributesW_t = DWORD(WINAPI*)(LPCWSTR);
using GetFileAttributesA_t = DWORD(WINAPI*)(LPCSTR);
using GetFileAttributesExW_t = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
using FindFirstFileW_t = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
using FindFirstFileExW_t = HANDLE(WINAPI*)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
using FindNextFileW_t = BOOL(WINAPI*)(HANDLE, LPWIN32_FIND_DATAW);
using SetCurrentDirectoryW_t = BOOL(WINAPI*)(LPCWSTR);

// What was in the exe's slots before the hooks (null for a function the exe does not import).
struct Real {
    CreateFileW_t CreateFileW = nullptr;
    CreateFileA_t CreateFileA = nullptr;
    GetFileAttributesW_t GetFileAttributesW = nullptr;
    GetFileAttributesA_t GetFileAttributesA = nullptr;
    GetFileAttributesExW_t GetFileAttributesExW = nullptr;
    FindFirstFileW_t FindFirstFileW = nullptr;
    FindFirstFileExW_t FindFirstFileExW = nullptr;
    FindNextFileW_t FindNextFileW = nullptr;
    SetCurrentDirectoryW_t SetCurrentDirectoryW = nullptr;
};

constexpr unsigned kMaxLines = 50000;  // about 10 MB

inline unsigned long long default_now() { return GetTickCount64(); }
inline std::wstring default_cwd() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetCurrentDirectoryW(static_cast<DWORD>(sizeof buf / sizeof buf[0]), buf);
    return n > 0 && n < sizeof buf / sizeof buf[0] ? std::wstring(buf, n) : std::wstring();
}
// The loader's own import of GetFileAttributesW, not the exe's hooked one.
inline bool default_is_file(const wchar_t* path) {
    const DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

struct State {
    Real real;
    std::wstring game_dir;  // normal (normal_game_dir), worked out once in start(): decide() compares every path with it
    ContentSwitches sw;
    unsigned long long start_ms = 0;  // when the hooks went in
    unsigned max_lines = kMaxLines;
    std::atomic<unsigned> lines{0};
    std::atomic<bool> window_said{false}, cap_said{false};
    void (*write)(const char* data, size_t n) = nullptr;  // where the lines go; null: nothing is logged
    bool (*is_file)(const wchar_t* path) = default_is_file;
    unsigned long long (*now_ms)() = default_now;
    std::wstring (*cwd)() = default_cwd;
    std::vector<std::pair<void**, void*>> patched;  // slot, what was there (for the tests' uninstall)
};

// Made by start() (or a test), so a loader with the switches off makes nothing at all. Never destroyed: the hooks stay
// in the exe's table until the process is gone, so a call that comes late at exit must not find a destroyed state.
inline State* g_state = nullptr;

// The hooks' state; start() makes it before any hook goes in.
inline State& state() { return *g_state; }
inline State& make_state() {
    if (!g_state) g_state = new State;
    return *g_state;
}
inline bool has_state() { return g_state != nullptr; }

inline unsigned long long elapsed() { return state().now_ms() - state().start_ms; }

inline LineHead head(const char* fn) { return {elapsed(), GetCurrentThreadId(), fn}; }

inline void write_raw(const std::string& s) {
    if (state().write) state().write(s.data(), s.size());
}

// Takes one line of the log: true while there is a log and it is below max_lines; at the limit it says once that the
// log stops. Asked before a line is built, so a full log (or none) costs a call nothing.
inline bool reserve_line() {
    auto& g = state();
    if (!g.write) return false;
    if (g.lines.load(std::memory_order_relaxed) < g.max_lines && g.lines.fetch_add(1) < g.max_lines) return true;
    if (!g.cap_said.exchange(true))
        write_raw("# the log stops here: " + std::to_string(g.max_lines) + " lines (the limit)\r\n");
    return false;
}

// One line into the log; `build` makes it, and only once there is room for it.
template <class Build>
void emit(Build build) {
    if (!reserve_line()) return;
    const std::string line = build();
    state().write(line.data(), line.size());
}

// True while every call is logged (log_opens=1, within log_seconds); says once when that time is over.
inline bool in_window() {
    auto& g = state();
    if (!g.sw.log_opens || !g.write) return false;
    const unsigned long long ms = elapsed();
    if (ms < g.sw.log_seconds * 1000ull) return true;
    if (!g.window_said.exchange(true))
        write_raw("# the logging window (" + std::to_string(g.sw.log_seconds) + " s, [content] log_seconds) is over at +" +
                  std::to_string(ms) + "ms: from here only the content files (with redirect_test=1) are logged\r\n");
    return false;
}

// What happens to one call: `content` = redirect_test is on and it names a content file in the game folder; `use` = it
// goes to `shadow`; `note` = why not.
struct Decision {
    std::wstring shadow;
    const char* note = "";
    bool content = false;
    bool use = false;
};

// `open`: a CreateFile (only reads are redirected); else an attribute query.
inline Decision decide(const wchar_t* path, bool open, DWORD access, DWORD disposition, DWORD flags) {
    auto& g = state();
    Decision d;
    if (!g.sw.redirect_test || !path || g.game_dir.empty()) return d;
    const std::wstring cwd = path_needs_cwd(path) ? g.cwd() : std::wstring();
    std::wstring s = shadow_path_in(path, g.game_dir, cwd);  // game_dir: made normal once, in start()
    if (s.empty()) return d;
    d.content = true;
    if (open && !redirect_allowed(access, disposition, flags)) {  // a write, a create, a delete: the game's own path
        d.note = "not redirected: not a read-only open of a file that is there";
        return d;
    }
    if (!g.is_file(s.c_str())) {
        d.note = "not redirected: no shadow copy";
        return d;
    }
    d.shadow = std::move(s);
    d.use = true;
    return d;
}

// An ANSI path as the A functions read it (the ANSI code page, or the OEM one after SetFileApisToOEM).
inline std::wstring from_ansi(const char* s) {
    if (!s || !*s) return {};
    const UINT cp = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    const int n = MultiByteToWideChar(cp, 0, s, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(cp, 0, s, -1, w.data(), n);
    w.resize(static_cast<size_t>(n - 1));
    return w;
}

// The originals, or the loader's own imports for one the exe does not import (the A hooks redirect through W).
inline CreateFileW_t real_create_w() { return state().real.CreateFileW ? state().real.CreateFileW : &::CreateFileW; }
inline GetFileAttributesW_t real_attrs_w() {
    return state().real.GetFileAttributesW ? state().real.GetFileAttributesW : &::GetFileAttributesW;
}

// No C++ exception ever leaves a hook into the game's code (a std::bad_alloc while a path or a line is built, say).
// The deciding part, before the real call: when it throws, the call goes through unchanged.
template <class Step>
Decision decide_safely(Step step) noexcept {
    try {
        return step();
    } catch (...) {
        return Decision{};
    }
}
// The logging part, after the real call: when it throws, only the line is lost; the result and its error stand.
template <class Log>
void log_safely(Log log) noexcept {
    try {
        log();
    } catch (...) {
    }
}

inline HANDLE WINAPI create_file_w(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                                   DWORD flags, HANDLE template_file) {
    const DWORD entry_error = GetLastError();
    const Decision d = decide_safely([&] { return decide(name, true, access, disposition, flags); });
    SetLastError(entry_error);
    const HANDLE h = real_create_w()(d.use ? d.shadow.c_str() : name, access, share, sa, disposition, flags, template_file);
    const DWORD error = GetLastError();
    log_safely([&] {
        if (d.content || in_window())
            emit([&] {
                return open_line(head("CreateFileW"), name ? name : L"", d.shadow, d.note, access, share, disposition,
                                 flags, h, error);
            });
    });
    SetLastError(error);
    return h;
}

inline HANDLE WINAPI create_file_a(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                                   DWORD flags, HANDLE template_file) {
    const DWORD entry_error = GetLastError();
    std::wstring wide;
    const Decision d = decide_safely([&] {
        wide = from_ansi(name);
        return name ? decide(wide.c_str(), true, access, disposition, flags) : Decision{};
    });
    SetLastError(entry_error);
    const HANDLE h = d.use ? real_create_w()(d.shadow.c_str(), access, share, sa, disposition, flags, template_file)
                     : state().real.CreateFileA
                         ? state().real.CreateFileA(name, access, share, sa, disposition, flags, template_file)
                         : ::CreateFileA(name, access, share, sa, disposition, flags, template_file);
    const DWORD error = GetLastError();
    log_safely([&] {
        if (d.content || in_window())
            emit([&] {
                return open_line(head("CreateFileA"), wide, d.shadow, d.note, access, share, disposition, flags, h, error);
            });
    });
    SetLastError(error);
    return h;
}

inline DWORD WINAPI get_file_attributes_w(LPCWSTR name) {
    const DWORD entry_error = GetLastError();
    const Decision d = decide_safely([&] { return decide(name, false, 0, 0, 0); });
    SetLastError(entry_error);
    const DWORD a = real_attrs_w()(d.use ? d.shadow.c_str() : name);
    const DWORD error = GetLastError();
    log_safely([&] {
        if (d.content || in_window())
            emit([&] { return attributes_line(head("GetFileAttributesW"), name ? name : L"", d.shadow, d.note, a, error); });
    });
    SetLastError(error);
    return a;
}

inline DWORD WINAPI get_file_attributes_a(LPCSTR name) {
    const DWORD entry_error = GetLastError();
    std::wstring wide;
    const Decision d = decide_safely([&] {
        wide = from_ansi(name);
        return name ? decide(wide.c_str(), false, 0, 0, 0) : Decision{};
    });
    SetLastError(entry_error);
    const DWORD a = d.use                            ? real_attrs_w()(d.shadow.c_str())
                    : state().real.GetFileAttributesA ? state().real.GetFileAttributesA(name)
                                                      : ::GetFileAttributesA(name);
    const DWORD error = GetLastError();
    log_safely([&] {
        if (d.content || in_window())
            emit([&] { return attributes_line(head("GetFileAttributesA"), wide, d.shadow, d.note, a, error); });
    });
    SetLastError(error);
    return a;
}

inline BOOL WINAPI get_file_attributes_ex_w(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    const DWORD entry_error = GetLastError();
    const Decision d = decide_safely([&] { return decide(name, false, 0, 0, 0); });
    SetLastError(entry_error);
    const auto real = state().real.GetFileAttributesExW ? state().real.GetFileAttributesExW : &::GetFileAttributesExW;
    const BOOL ok = real(d.use ? d.shadow.c_str() : name, level, info);
    const DWORD error = GetLastError();
    log_safely([&] {
        if (d.content || in_window())
            emit([&] {
                return attributes_ex_line(head("GetFileAttributesExW"), name ? name : L"", d.shadow, d.note,
                                          static_cast<int>(level), ok != FALSE,
                                          static_cast<const WIN32_FILE_ATTRIBUTE_DATA*>(info), error);
            });
    });
    SetLastError(error);
    return ok;
}

inline HANDLE WINAPI find_first_file_w(LPCWSTR pattern, LPWIN32_FIND_DATAW data) {
    const auto real = state().real.FindFirstFileW ? state().real.FindFirstFileW : &::FindFirstFileW;
    const HANDLE h = real(pattern, data);
    const DWORD error = GetLastError();
    log_safely([&] {
        if (in_window())
            emit([&] {
                return find_first_line(head("FindFirstFileW"), pattern ? pattern : L"", "", h,
                                       h != INVALID_HANDLE_VALUE && data ? data->cFileName : L"", error);
            });
    });
    SetLastError(error);
    return h;
}

inline HANDLE WINAPI find_first_file_ex_w(LPCWSTR pattern, FINDEX_INFO_LEVELS level, LPVOID data,
                                          FINDEX_SEARCH_OPS search, LPVOID filter, DWORD flags) {
    const auto real = state().real.FindFirstFileExW ? state().real.FindFirstFileExW : &::FindFirstFileExW;
    const HANDLE h = real(pattern, level, data, search, filter, flags);
    const DWORD error = GetLastError();
    log_safely([&] {
        if (in_window())
            emit([&] {
                char extra[64];
                std::snprintf(extra, sizeof extra, "level=%d search=%d flags=0x%lx", static_cast<int>(level),
                              static_cast<int>(search), static_cast<unsigned long>(flags));
                const auto* found = static_cast<const WIN32_FIND_DATAW*>(data);
                return find_first_line(head("FindFirstFileExW"), pattern ? pattern : L"", extra, h,
                                       h != INVALID_HANDLE_VALUE && found ? found->cFileName : L"", error);
            });
    });
    SetLastError(error);
    return h;
}

inline BOOL WINAPI find_next_file_w(HANDLE find, LPWIN32_FIND_DATAW data) {
    const auto real = state().real.FindNextFileW ? state().real.FindNextFileW : &::FindNextFileW;
    const BOOL ok = real(find, data);
    const DWORD error = GetLastError();
    log_safely([&] {
        if (in_window())
            emit([&] {
                return find_next_line(head("FindNextFileW"), find, ok != FALSE, ok && data ? data->cFileName : L"",
                                      error);
            });
    });
    SetLastError(error);
    return ok;
}

inline BOOL WINAPI set_current_directory_w(LPCWSTR path) {
    const auto real = state().real.SetCurrentDirectoryW ? state().real.SetCurrentDirectoryW : &::SetCurrentDirectoryW;
    const BOOL ok = real(path);
    const DWORD error = GetLastError();
    log_safely([&] {
        // with redirect_test=1 relative content paths depend on it: then it is logged for as long as content files are
        if (in_window() || state().sw.redirect_test)
            emit([&] { return set_dir_line(head("SetCurrentDirectoryW"), path ? path : L"", ok != FALSE, error); });
    });
    SetLastError(error);
    return ok;
}

struct HookSpec {
    const char* name;
    void* hook;
    void** real;
};

inline std::vector<HookSpec> hook_specs() {
    auto& r = state().real;
    return {
        {"CreateFileW", reinterpret_cast<void*>(&create_file_w), reinterpret_cast<void**>(&r.CreateFileW)},
        {"CreateFileA", reinterpret_cast<void*>(&create_file_a), reinterpret_cast<void**>(&r.CreateFileA)},
        {"GetFileAttributesW", reinterpret_cast<void*>(&get_file_attributes_w),
         reinterpret_cast<void**>(&r.GetFileAttributesW)},
        {"GetFileAttributesA", reinterpret_cast<void*>(&get_file_attributes_a),
         reinterpret_cast<void**>(&r.GetFileAttributesA)},
        {"GetFileAttributesExW", reinterpret_cast<void*>(&get_file_attributes_ex_w),
         reinterpret_cast<void**>(&r.GetFileAttributesExW)},
        {"FindFirstFileW", reinterpret_cast<void*>(&find_first_file_w), reinterpret_cast<void**>(&r.FindFirstFileW)},
        {"FindFirstFileExW", reinterpret_cast<void*>(&find_first_file_ex_w), reinterpret_cast<void**>(&r.FindFirstFileExW)},
        {"FindNextFileW", reinterpret_cast<void*>(&find_next_file_w), reinterpret_cast<void**>(&r.FindNextFileW)},
        {"SetCurrentDirectoryW", reinterpret_cast<void*>(&set_current_directory_w),
         reinterpret_cast<void**>(&r.SetCurrentDirectoryW)},
    };
}

// Other file functions in the exe's imports, named in the log's header (reads, sizes, mappings, I/O completion).
inline bool is_other_file_function(const char* name) {
    const std::string n = name;
    for (const char* skip : {"Time", "Module", "PcToFileHeader"})
        if (n.find(skip) != std::string::npos) return false;
    for (const char* hit : {"File", "Directory", "IoRing", "IoCompletion", "Overlapped", "MapViewOf"})
        if (n.find(hit) != std::string::npos) return true;
    return false;
}

struct Installed {
    std::vector<std::string> hooked;      // "CreateFileW (KERNEL32.dll)"
    std::vector<std::string> missing;     // not imported by the exe, so not hooked
    std::vector<std::string> failed;      // imported, but the slot could not be written
    std::vector<std::string> dlls;        // every DLL the exe imports from
    std::vector<std::string> other_file;  // other file functions it imports (not hooked)
};

// Puts the hooks into `exe`'s import table (kernel32 and its API sets only); `state()` must be set up first. Each
// original is kept before its slot changes.
inline Installed install(HMODULE exe) {
    auto& g = state();
    Installed out;
    if (!g.start_ms) g.start_ms = g.now_ms();
    const auto specs = hook_specs();
    std::vector<bool> found(specs.size(), false);
    for_each_import(exe, [&](const char* dll, const char* name, void** slot) {
        bool known = false;
        for (const auto& d : out.dlls) known = known || _stricmp(d.c_str(), dll) == 0;
        if (!known) out.dlls.emplace_back(dll);
        if (!is_kernel_dll(dll)) return;
        for (size_t i = 0; i < specs.size(); ++i) {
            if (std::strcmp(name, specs[i].name) != 0) continue;
            found[i] = true;
            void* before = *slot;
            if (before == specs[i].hook) return;  // already ours
            // One original per function: an exe that imported the same function through two descriptors (KERNEL32 and
            // an api-ms-win-core set, say) would have both slots' calls go to the first slot's original. Keeping one
            // per slot needs a hook per slot (one function cannot tell which slot called it), and the game imports
            // each file function once, from KERNEL32 only (docs/engine_parts/file_redirect_spike.md).
            if (!*specs[i].real) *specs[i].real = before;
            if (patch_import_slot(slot, specs[i].hook)) {
                g.patched.emplace_back(slot, before);
                out.hooked.push_back(std::string(name) + " (" + dll + ")");
            } else {
                out.failed.push_back(std::string(name) + " (" + dll + ")");
            }
            return;
        }
        if (is_other_file_function(name)) out.other_file.emplace_back(name);
    });
    for (size_t i = 0; i < specs.size(); ++i)
        if (!found[i]) out.missing.emplace_back(specs[i].name);
    return out;
}

// Puts back what install() replaced. Only the tests do this: the loader keeps its hooks for the life of the game.
inline void uninstall_for_test(HMODULE) {
    for (const auto& [slot, before] : state().patched) patch_import_slot(slot, before);
    state().patched.clear();
}

inline void reset_for_test() {
    auto& g = make_state();
    g.real = {};
    g.game_dir.clear();
    g.sw = {};
    g.start_ms = 0;
    g.max_lines = kMaxLines;
    g.lines = 0;
    g.window_said = false;
    g.cap_said = false;
    g.write = nullptr;
    g.is_file = default_is_file;
    g.now_ms = default_now;
    g.cwd = default_cwd;
    g.patched.clear();
}

// Drops the state, as before start(): only a test does this, with no hook in place.
inline void discard_for_test() {
    delete g_state;
    g_state = nullptr;
}

inline std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s.empty() ? "(none)" : s;
}

// The first lines of emf\file_opens.log: when, which exe, the switches (as the ini has them), the shadow folder, why
// the redirect is off (`redirect_off`, "" when it is not), what was hooked (`inst`; null: no hooks went in).
inline std::string header_text(const Installed* inst, const std::wstring& exe_path, unsigned long pe_stamp,
                               const std::wstring& game_dir, bool shadow_there, const ContentSwitches& sw,
                               const std::string& redirect_off, const std::string& started_at) {
    char stamp[16];
    std::snprintf(stamp, sizeof stamp, "0x%08lx", pe_stamp);
    std::string s = "# EMF file-open log: the content redirect spike (docs/engine_parts/file_redirect_spike.md)\r\n";
    s += "# started " + started_at + " local time; exe \"" + utf8(exe_path) + "\" (PE time stamp " + stamp + ")\r\n";
    s += "# switches ([content] in emf\\emf_core.ini): log_opens=" + std::to_string(sw.log_opens ? 1 : 0) +
         " log_seconds=" + std::to_string(sw.log_seconds) + " redirect_test=" + std::to_string(sw.redirect_test ? 1 : 0) +
         "\r\n";
    s += "# shadow folder \"" + utf8(shadow_folder(game_dir)) + "\": " + (shadow_there ? "there" : "not there") + "\r\n";
    if (!redirect_off.empty()) s += "# " + redirect_off + "\r\n";
    if (!inst) return s + "# no hooks: with the redirect off and log_opens=0 there is nothing to log or redirect\r\n";
    s += "# the exe imports from: " + joined(inst->dlls) + "\r\n";
    s += "# hooked in the exe's import table: " + joined(inst->hooked) + "\r\n";
    s += "# not imported by the exe, so not hooked: " + joined(inst->missing) + "\r\n";
    if (!inst->failed.empty())
        s += "# could not be hooked (the slot could not be written): " + joined(inst->failed) + "\r\n";
    s += "# other file functions the exe imports (not hooked): " + joined(inst->other_file) + "\r\n";
    s += "# each line: +<ms since the hooks went in>ms t<thread id> <function> \"<path>\" [=> \"<shadow path>\"] "
         "<arguments> -> <result>\r\n";
    return s;
}

// ---- the loader's log file ----

inline SRWLOCK g_log_lock = SRWLOCK_INIT;
inline HANDLE g_log_file = INVALID_HANDLE_VALUE;

// Opens (and empties) the log; false when it cannot (no emf folder): the hooks then redirect without logging.
inline bool open_log(const std::wstring& path) {
    g_log_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return g_log_file != INVALID_HANDLE_VALUE;
}

// Called from any game thread: one line at a time, written through at once (nothing is lost if the game stops).
inline void write_log(const char* data, size_t n) {
    if (g_log_file == INVALID_HANDLE_VALUE) return;
    AcquireSRWLockExclusive(&g_log_lock);
    DWORD written = 0;
    WriteFile(g_log_file, data, static_cast<DWORD>(n), &written, nullptr);
    ReleaseSRWLockExclusive(&g_log_lock);
}

inline void close_log_for_test() {
    if (g_log_file != INVALID_HANDLE_VALUE) CloseHandle(g_log_file);
    g_log_file = INVALID_HANDLE_VALUE;
}

// ---- the check of the shadow copy against the game's files (shadow_problem in file_redirect.h) ----
// Run in start(), before the hooks go in, through the loader's own imports (fine in DllMain: no other DLL is loaded).

inline std::optional<FileStamp> stamp_of(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return std::nullopt;
    return FileStamp{(static_cast<unsigned long long>(d.nFileSizeHigh) << 32) | d.nFileSizeLow,
                     (static_cast<unsigned long long>(d.ftLastWriteTime.dwHighDateTime) << 32) |
                         d.ftLastWriteTime.dwLowDateTime};
}

// The enshrouded_NNN.dat files directly in `game`.
inline unsigned long count_dat_files(const std::wstring& game) {
    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileExW(path_join(game, L"enshrouded_*").c_str(), FindExInfoBasic, &fd,
                                      FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    unsigned long n = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && is_dat_file_name(fd.cFileName)) ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

// A small text file's bytes; nullopt when it cannot be opened or read, or is bigger than `limit`.
inline std::optional<std::string> read_small_file(const std::wstring& path, DWORD limit) {
    const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::nullopt;
    std::optional<std::string> out;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart <= limit) {
        std::string text(static_cast<size_t>(size.QuadPart), '\0');
        DWORD got = 0;
        if (text.empty() || (ReadFile(f, text.data(), static_cast<DWORD>(text.size()), &got, nullptr) && got == text.size()))
            out = std::move(text);
    }
    CloseHandle(f);
    return out;
}

// `game`: the normal game folder.
inline ShadowFacts shadow_facts(const std::wstring& game) {
    ShadowFacts f;
    const std::wstring record = path_join(shadow_folder(game), L"shadow.txt");
    const DWORD a = GetFileAttributesW(record.c_str());
    f.record_there = a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    if (f.record_there) f.record = read_small_file(record, 64 * 1024);
    for (size_t i = 0; i < kStampedCount; ++i) {
        const std::string name = kStampedFiles[i];
        f.now[i] = stamp_of(path_join(game, std::wstring(name.begin(), name.end())));
    }
    f.dats_now = count_dat_files(game);
    return f;
}

// "" when the redirect may run; else why not (the line for both logs).
inline std::string redirect_problem(const std::optional<std::wstring>& game) {
    if (!game) return "content redirect off: the game folder's path is not one the redirect can read";
    return shadow_problem(shadow_facts(*game));
}

// The loader's whole step (DllMain, the game only, once the switches said yes): with redirect_test=1 the check of the
// shadow copy first, then the log file, the hooks (none when nothing is left to do) and the log's header. Returns the
// lines for emf_loader.log.
inline std::vector<std::string> start(HMODULE exe, const std::wstring& game_dir, const ContentSwitches& asked) {
    auto& g = make_state();
    const auto normal = normal_game_dir(game_dir);
    const std::wstring folder = normal.value_or(game_dir);
    ContentSwitches sw = asked;
    const std::string redirect_off = sw.redirect_test ? redirect_problem(normal) : std::string();
    if (!redirect_off.empty()) sw.redirect_test = false;
    g.game_dir = normal.value_or(L"");
    g.sw = sw;
    const bool logging = open_log(path_join(folder, L"emf\\file_opens.log"));
    g.write = logging ? &write_log : nullptr;
    // before the hooks: in a module whose own imports are hooked (the tests) these calls would be logged
    const DWORD shadow = GetFileAttributesW(shadow_folder(folder).c_str());
    g.start_ms = g.now_ms();
    Installed inst;
    if (sw.hooks()) inst = install(exe);
    if (logging) {
        wchar_t exe_path[MAX_PATH] = {};
        GetModuleFileNameW(exe, exe_path, MAX_PATH);
        const auto* base = reinterpret_cast<const uint8_t*>(exe);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        SYSTEMTIME t{};
        GetLocalTime(&t);
        char when[32];
        std::snprintf(when, sizeof when, "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                      t.wSecond);
        write_raw(header_text(sw.hooks() ? &inst : nullptr, exe_path, nt->FileHeader.TimeDateStamp, folder,
                              shadow != INVALID_FILE_ATTRIBUTES && (shadow & FILE_ATTRIBUTE_DIRECTORY), asked,
                              redirect_off, when));
    }
    std::vector<std::string> lines;
    if (!redirect_off.empty()) lines.push_back(redirect_off);
    std::string line = sw.hooks() ? "content spike: hooked " + std::to_string(inst.hooked.size()) +
                                        " file functions in the exe's import table (log_opens=" +
                                        std::to_string(sw.log_opens ? 1 : 0) +
                                        ", redirect_test=" + std::to_string(sw.redirect_test ? 1 : 0) + "); "
                                  : std::string("content spike: no hooks (the redirect is off and log_opens=0); ");
    line += logging ? "the log is emf\\file_opens.log" : "could not create emf\\file_opens.log, so nothing is logged";
    lines.push_back(line);
    return lines;
}

}  // namespace emf::loader::file_hooks
