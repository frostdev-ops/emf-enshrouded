// emf_loader: installed as dinput8.dll next to enshrouded.exe. Loads <game>\emf\emf_core.dll from a
// thread (never under the loader lock) and calls emf_core_start(game_dir). In the dedicated server it also holds the
// main thread at the exe's entry point until emf_core_start has returned (see entry_hold.h).
//
// First it starts the DLL mods listed in dinput8-chain.ini (chain_list.h): other mods that want the name dinput8.dll
// are renamed and listed there by EMF's installer. They are loaded right here in DllMain, the way the community's
// chain loaders do it and the moment they would have started as dinput8.dll themselves. The entry point is not
// touched for them: ShroudForge patches it too.
//
// Before all that, in the game (not the dedicated server), it takes EMF.exe's one-shot flag for "Play without EMF
// mods" (play_once.h): emf\play_once.txt is read and deleted at once, and for this one start EMF's core (and with
// "Play without any mods" the chained DLLs too) does not start.
//
// In the game client (an exe named enshrouded.exe), with [content] log_opens=1 or redirect_test=1 in emf\emf_core.ini
// (both off by default), it also puts the content redirect spike's hooks into the exe's file functions (file_hooks.h).
//
// EML content (content_start.h, docs/engine_parts/content_loader.md): when emf\emf_content.dll is installed and the
// game's mods\ folder has EML mods EMF can build (and no other tool applies them), the game reads its data from a
// shadow copy with the mods built in (emf\content\client\). When that copy is current, the redirect goes in here and
// the rest is as above. When it must be built, DllMain only holds the exe's entry point: the build runs there on the
// main thread (emf_content.dll loaded, used and unloaded), and only then the chained DLLs and EMF's core start. In
// every other case (no emf_content.dll above all) the loader does what it did before and writes one line saying why.
#include <windows.h>

#include <share.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>

#include "chain_list.h"
#include "chain_load.h"
#include "content_splash.h"
#include "content_start.h"
#include "core/known_tools.h"
#include "entry_hold.h"
#include "file_hooks.h"
#include "layer_env.h"
#include "loader_id.h"
#include "play_once.h"

// Marks this DLL as EMF's loader for the installer and the core (loader_id.h); exported, so it is kept.
extern "C" __declspec(dllexport) const char emf_loader_identity[] = EMF_LOADER_MARKER;
// Tells EMF.exe that this loader takes the one-shot flag of its Play menu (play_once.h).
extern "C" __declspec(dllexport) const char emf_loader_play_once[] = EMF_PLAY_ONCE_MARKER;

namespace {
void loader_log(const std::wstring& dir, const char* msg) {
    // tiny separate log so a missing/broken core is visible
    // shared: another thread (or the manager) may have it open at the same time
    if (FILE* f = _wfsopen((dir + L"\\emf\\emf_loader.log").c_str(), L"ab", _SH_DENYNO)) {
        fprintf(f, "emf_loader: %s\n", msg);
        fclose(f);
    }
    OutputDebugStringA("emf_loader: ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

HANDLE g_core_done = nullptr;  // set once emf_core_start returned or could not run (server only)
std::atomic<emf::loader::CoreOutcome> g_core_outcome{emf::loader::CoreOutcome::Running};  // how init_thread ended
emf::loader::EntryHold g_hold;
constexpr DWORD kHoldMs = 20000;

std::wstring game_dir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    return dir.substr(0, dir.find_last_of(L"\\/"));
}

DWORD WINAPI init_thread(LPVOID) {
    struct Done {
        ~Done() {
            if (g_core_done) SetEvent(g_core_done);
        }
    } done;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    HMODULE core = LoadLibraryW((dir + L"\\emf\\emf_core.dll").c_str());
    if (!core) {
        loader_log(dir, "emf\\emf_core.dll not found or failed to load; running the game without mods");
        g_core_outcome = emf::loader::CoreOutcome::Missing;
        return 0;
    }
    using StartFn = void (*)(const wchar_t*);
    auto start = reinterpret_cast<StartFn>(GetProcAddress(core, "emf_core_start"));
    if (!start) {
        loader_log(dir, "emf_core.dll has no emf_core_start export");
        g_core_outcome = emf::loader::CoreOutcome::NoExport;
        return 0;
    }
    loader_log(dir, "starting emf_core");
    start(dir.c_str());
    g_core_outcome = emf::loader::CoreOutcome::Started;
    return 0;
}

// The dedicated server's entry point lands here first (main thread, loader lock free): it waits until EMF has hooked
// the world setup and loaded the mods, puts the entry point's bytes back and runs it.
DWORD WINAPI held_entry(void* arg) {
    const ULONGLONG start = GetTickCount64();
    const DWORD w = WaitForSingleObject(g_core_done, kHoldMs);
    loader_log(game_dir(),
               emf::loader::hold_message(w == WAIT_OBJECT_0, g_core_outcome.load(), GetTickCount64() - start).c_str());
    if (!emf::loader::release_entry(g_hold)) {
        loader_log(game_dir(), "could not restore the server's entry point; stopping");
        ExitProcess(1);
    }
    using EntryFn = DWORD(WINAPI*)(void*);
    return reinterpret_cast<EntryFn>(g_hold.entry)(arg);
}

std::string read_file(const std::wstring& path) {
    std::string text;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return text;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (1u << 20)) text.append(buf, n);
    fclose(f);
    return text;
}


// EMF.exe's one-shot flag (play_once.h), read and deleted before anything else; what this start leaves out.
emf::loader::PlayOnce take_play_once_flag(const std::wstring& dir) {
    const std::wstring path = dir + L"\\" + emf::loader::kPlayOnceFile;
    const auto r = emf::loader::take_play_once(
        [&]() -> std::optional<std::string> {
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return std::nullopt;
            return read_file(path);  // unreadable: "" is no flag, and it is deleted all the same
        },
        [&] {
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            return DeleteFileW(path.c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
        },
        static_cast<int64_t>(_time64(nullptr)));
    if (!r.why.empty()) loader_log(dir, r.why.c_str());
    return r.mode;
}

// The DLLs dinput8-chain.ini lists, read once per start (the content loader looks at them too).
const std::vector<std::wstring>& chain_names(const std::wstring& dir) {
    static const std::vector<std::wstring> names = emf::loader::parse_chain_list(read_file(dir + L"\\dinput8-chain.ini"));
    return names;
}

// Starts the DLLs listed in dinput8-chain.ini (or, for a start without mods, none of them), logs each and writes
// emf\chain_status.txt (read by EMF's core, which starts after this, and by the manager).
void run_chain(const std::wstring& dir, bool start) {
    const auto& names = chain_names(dir);
    const auto results = !start ? emf::loader::skip_chain(names) : emf::loader::load_chain(
        names,
        [&](const std::wstring& name) {
            return GetFileAttributesW((dir + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES;
        },
        [&](const std::wstring& name, unsigned long* error) {
            // A crash in the DLL's own DllMain comes back as error 1114 (Windows unloads it); one that hangs or ends
            // the process stops the game as it would have without EMF.
            if (LoadLibraryExW((dir + L"\\" + name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) return true;
            *error = GetLastError();
            return false;
        });
    for (const auto& r : results) loader_log(dir, emf::loader::chain_line(r).c_str());
    const std::string text = emf::loader::chain_status_text(results);
    if (FILE* f = _wfsopen((dir + L"\\emf\\chain_status.txt").c_str(), L"wb", _SH_DENYNO)) {
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }
}

void put_first(const wchar_t* var, const std::wstring& item) {
    std::wstring current;
    const DWORD n = GetEnvironmentVariableW(var, nullptr, 0);
    if (n) {
        current.resize(n);
        current.resize(GetEnvironmentVariableW(var, current.data(), n));
    }
    SetEnvironmentVariableW(var, emf::loader::list_with_first(current, item).c_str());
}

// The in-game console draws through EMF's Vulkan layer (emf\vk\VkLayer_EMF_console.json -> emf\emf_core.dll). The
// Vulkan loader reads these variables when the game creates its instance, so they are set here, before any game code
// runs, and only for this process (nothing in the registry).
void enable_vulkan_layer() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    const std::wstring vk = dir + L"\\emf\\vk";
    if (GetFileAttributesW((vk + L"\\VkLayer_EMF_console.json").c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (GetPrivateProfileIntW(L"overlay", L"enabled", 1, (dir + L"\\emf\\emf_core.ini").c_str()) == 0) return;
    put_first(L"VK_ADD_LAYER_PATH", vk);
    put_first(L"VK_INSTANCE_LAYERS", L"VK_LAYER_EMF_console");
}

// The content redirect spike's hooks (file_hooks.h, docs/engine_parts/file_redirect_spike.md) in the game client
// (enshrouded.exe; not the dedicated server or another program that loads this DLL). They go into the exe's import table
// before the exe's own code runs (its entry point comes after every DllMain, and a held entry runs this before it lets
// the entry go), and stay for the life of the game. After the chained DLLs: a hook one of them put into the same table
// is called through. `hooks`: the switches as emf_core.ini has them ([content] log_opens, redirect_test), or the
// redirect to EML content's shadow copy on (a good build or a current shadow) or forced off (EMF owns the content but
// has no good build, so the spike's switch must not send the game to that copy either).
void start_file_hooks(const std::wstring& dir, emf::loader::Hooks hooks) {
    try {  // no C++ exception leaves DllMain (or the held entry)
        auto sw = emf::loader::read_content_switches(dir + L"\\emf\\emf_core.ini");
        if (hooks == emf::loader::Hooks::ContentOn) sw.redirect_test = true;
        if (hooks == emf::loader::Hooks::ContentOff) sw.redirect_test = false;
        if (!sw.hooks()) return;
        for (const auto& line : emf::loader::file_hooks::start(GetModuleHandleW(nullptr), dir, sw))
            loader_log(dir, line.c_str());
    } catch (...) {
        loader_log(dir, "content spike: could not start (out of memory); the game runs without it");
    }
}

// ---- this start ----

std::wstring g_exe;  // the exe's path
std::wstring g_dir;  // the game folder
emf::loader::StartPlan g_start;  // the chained DLLs and EMF's core, after EMF.exe's one-shot flag
emf::loader::ContentFacts g_facts;
emf::loader::ContentPlan g_content;
emf::loader::EntryHold g_content_hold;  // the game client's entry, held for a content build
emf::loader::ContentSplash g_splash;
bool g_chain_ran = false, g_core_ran = false;

std::wstring content_dll() { return g_dir + L"\\emf\\emf_content.dll"; }
std::wstring shadow_dir() { return g_dir + L"\\" + emf::loader::kShadowSubdir; }
std::wstring state_path() { return shadow_dir() + L"\\" + emf::loader::kStateFile; }

// Says why an entry point is not the usual start-up code: another tool's jump (and into which DLL), or unknown code.
std::string describe_entry(const uint8_t* entry, const char* whose) {
    const uint64_t target = emf::loader::entry_jump_target(entry, [](uint64_t at, uint64_t* out) {
        SIZE_T got = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(at), out, sizeof *out, &got) &&
               got == sizeof *out;
    });
    std::string module;
    HMODULE owner = nullptr;
    wchar_t path[MAX_PATH];
    if (target && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     reinterpret_cast<LPCWSTR>(target), &owner) &&
        owner && GetModuleFileNameW(owner, path, MAX_PATH)) {
        const std::wstring p(path);
        module = emf::loader::utf8_name(p.substr(p.find_last_of(L"\\/") + 1));
    }
    return emf::loader::entry_not_usual(entry, target, module, whose);
}

// EMF's core as it started before EML content: its Vulkan layer's variables (after the chained DLLs, so EMF's layer
// comes first), the dedicated server's hold, and the thread that loads emf_core.dll.
void start_core_today() {
    g_core_ran = true;
    if (!g_start.core) return;  // no core, and no Vulkan layer either (the layer is emf_core.dll)
    enable_vulkan_layer();
    uint8_t* entry = nullptr;
    bool want_hold = false;
    if (emf::loader::is_server_exe(g_exe)) {
        entry = emf::loader::exe_entry_point();
        if (!emf::loader::entry_is_msvc_startup(entry)) {
            loader_log(g_dir, (describe_entry(entry, "the server's") + "; not held, so mods may load after its world setup").c_str());
        } else {
            g_core_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            want_hold = g_core_done != nullptr;
            if (!want_hold)
                loader_log(g_dir, "could not create an event: the server's start is not held (mods may load "
                                  "after its world setup)");
        }
    }
    // The thread first (it runs once DllMain has returned: the loader lock), the hold only if the thread exists.
    DWORD thread_error = 0;
    const auto started = emf::loader::start_core(
        want_hold,
        [&thread_error] {
            HANDLE t = CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
            if (!t) {
                thread_error = GetLastError();
                return false;
            }
            CloseHandle(t);
            return true;
        },
        [entry] { return emf::loader::hold_entry(g_hold, entry, reinterpret_cast<void*>(&held_entry)); });
    if (started == emf::loader::HoldStart::ThreadFailed) {
        char msg[128];
        std::snprintf(msg, sizeof msg, "could not start emf_core's thread (error %lu); running the game without mods",
                      thread_error);
        loader_log(g_dir, msg);
    } else if (started == emf::loader::HoldStart::HoldFailed) {
        loader_log(g_dir, "could not hold the server's entry point; mods may load after its world setup");
    }
}

// At most `limit` bytes of a file ("" when it cannot be read).
std::string read_head(const std::wstring& path, size_t limit) {
    std::string text;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return text;
    char buf[65536];
    size_t n = 0;
    while (text.size() < limit && (n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    return text;
}

// The tool a DLL in the game folder belongs to, as players know it (emf\known_tools.txt); "" when unknown or EMF.
std::string dll_tool_name(const std::wstring& rel) {
    static const auto tools = emf::core::parse_known_tools(read_head(g_dir + L"\\emf\\known_tools.txt", 1u << 20));
    const std::string word = emf::core::tool_for_dll_text(tools, read_head(g_dir + L"\\" + rel, 32u << 20));
    if (word.empty() || word == "emf") return {};
    for (const auto& t : tools)
        if (t.word == word) return t.name;
    return word;
}

// The state the last build left (read once per start).
const std::optional<emf::loader::ContentState>& prev_state() {
    static const std::optional<emf::loader::ContentState> state = emf::loader::read_state(state_path());
    return state;
}

// "<dll>TAB<size>TAB<time>" for a chained DLL as it is now.
std::string chain_stamp(const std::wstring& rel) {
    const auto s = emf::loader::cio::stat(g_dir + L"\\" + rel);
    return emf::loader::cio::to_utf8(rel) + "\t" + std::to_string(s.size) + "\t" + std::to_string(s.written);
}

emf::loader::FactsIo facts_io() {
    emf::loader::FactsIo io;
    io.mode = [] {
        const std::wstring ini = g_dir + L"\\emf\\emf_core.ini";
        return emf::loader::content_mode([&](const wchar_t* section, const wchar_t* key) {
            wchar_t buf[128] = {};
            GetPrivateProfileStringW(section, key, L"", buf, 128, ini.c_str());
            return std::wstring(buf);
        });
    };
    io.look.exists = [](const std::wstring& rel) {
        const DWORD a = GetFileAttributesW((g_dir + L"\\" + rel).c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    };
    io.look.tool_name = [](const std::wstring& rel) { return dll_tool_name(rel); };
    io.look.chain = chain_names(g_dir);
    // a chained DLL the last good build read (and found not EML's) and that has not changed since is not read again
    io.look.known_clean = [](const std::wstring& rel) {
        const auto& st = prev_state();
        if (!st || st->code != 0) return false;
        const std::string now = chain_stamp(rel);
        for (const auto& c : st->chain_checked)
            if (c == now) return true;
        return false;
    };
    io.dll_present = [] {
        const DWORD a = GetFileAttributesW(content_dll().c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    };
    io.scan_mods = [] { return emf::loader::scan_eml_mods(g_dir + L"\\mods"); };
    io.base = [] {
        return emf::loader::choose_base(emf::loader::read_base_file(g_dir + L"\\enshrouded.kfc"),
                                        emf::loader::read_base_file(g_dir + L"\\enshrouded.kfc.bak"));
    };
    return io;
}

emf::loader::ContentPlan plan_this_start(emf::loader::PlayOnce play) {
    try {
        emf::loader::PlanSteps p;
        p.facts = [play] {
            const bool server = emf::loader::is_server_exe(g_exe);
            g_facts = emf::loader::collect_facts(facts_io(), play, server,
                                                 server || emf::loader::is_game_client_exe(g_exe));
            return g_facts;
        };
        p.fingerprint = [] {
            return emf::loader::content_fingerprint(emf::loader::collect_fingerprint_input(
                g_dir, "enshrouded", g_facts.base, g_facts.mods.usable, content_dll(), EMF_VERSION));
        };
        p.read_state = [] { return prev_state(); };
        p.record_ok = [] {
            const auto game = emf::loader::normal_game_dir(g_dir);
            if (!game || !emf::loader::shadow_problem(emf::loader::file_hooks::shadow_facts(*game)).empty()) return false;
            const std::string broken = emf::loader::shadow_intact(g_dir, shadow_dir(), "enshrouded", g_facts.base.dat_count);
            if (!broken.empty()) loader_log(g_dir, ("EML content: " + broken).c_str());
            return broken.empty();
        };
        p.log = [](const std::string& line) { loader_log(g_dir, line.c_str()); };
        return emf::loader::plan_content(p);
    } catch (...) {
        loader_log(g_dir, "EML content: off (the loader ran out of memory while looking)");
        return {};
    }
}

// <game>\emf\content.log: what a build did, emptied at each build. Written from the build's thread.
struct ContentLog {
    HANDLE file = INVALID_HANDLE_VALUE;
    ULONGLONG t0 = 0;
    SRWLOCK lock = SRWLOCK_INIT;
    void open() {
        file = CreateFileW((g_dir + L"\\emf\\content.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        t0 = GetTickCount64();
    }
    void line(const char* level, const std::string& text) {
        if (file == INVALID_HANDLE_VALUE) return;
        const std::string s = "+" + std::to_string(GetTickCount64() - t0) + "ms " + level + " " + text + "\r\n";
        AcquireSRWLockExclusive(&lock);
        DWORD n = 0;
        WriteFile(file, s.data(), static_cast<DWORD>(s.size()), &n, nullptr);
        ReleaseSRWLockExclusive(&lock);
    }
    void close() {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
    }
};
ContentLog g_content_log;

std::string utc_now() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

DWORD WINAPI held_content_entry(void* arg);

emf::loader::StartSteps make_start_steps() {
    emf::loader::StartSteps s;
    s.hold_entry = [] {
        uint8_t* entry = emf::loader::exe_entry_point();
        const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        // MSVC's start-up, or Steam's DRM stub in .bind (the game client): never another tool's jump
        if (!emf::loader::entry_holdable(entry, emf::loader::section_of(base, static_cast<uint32_t>(entry - base)))) {
            loader_log(g_dir, ("EML content: " + describe_entry(entry, "the game's")).c_str());
            return false;
        }
        if (!emf::loader::hold_entry(g_content_hold, entry, reinterpret_cast<void*>(&held_content_entry))) {
            loader_log(g_dir, "EML content: could not hold the game's entry point");
            return false;
        }
        return true;
    };
    s.run_chain = [] {
        g_chain_ran = true;
        run_chain(g_dir, g_start.chain);
    };
    s.hooks = [](emf::loader::Hooks h) {
        if (!emf::loader::is_game_client_exe(g_exe)) return;
        // the content redirect also without EMF's core (Play without EMF mods keeps EML content); the spike's
        // switches only with the core, as before
        if (h == emf::loader::Hooks::ContentOn || g_start.core) start_file_hooks(g_dir, h);
    };
    s.start_core = [] { start_core_today(); };
    s.set_status = [](const emf::loader::ContentStatus& st) {
        SetEnvironmentVariableW(emf::loader::kContentStatusVariable,
                                emf::loader::cio::from_utf8(emf::loader::content_status_text(st)).c_str());
    };
    s.log = [](const std::string& line) { loader_log(g_dir, line.c_str()); };
    s.splash_begin = [](const std::string& text) { g_splash.begin(text); };
    s.splash_end = [] { g_splash.end(); };
    s.run_work = [](const std::function<void()>& work) {
        return emf::loader::run_with_splash(&g_splash, work, emf::loader::kBuildTimeoutMs);
    };
    s.build_shadow = [] {
        g_content_log.open();
        g_content_log.line("emf", "EMF " EMF_VERSION ": building EML content for " +
                                      emf::loader::mods_text(g_facts.mods.usable.size()) + "; " + g_facts.base.why);
        g_content_log.line("emf", "making the shadow copy in " + emf::loader::cio::to_utf8(shadow_dir()));
        g_splash.line("Making the shadow copy of the game's data...");
        const auto r = emf::loader::build_shadow(g_dir, shadow_dir(), "enshrouded", g_facts.base, g_facts.mods.usable);
        g_content_log.line("emf", r.ok ? "the shadow copy is ready" : "the shadow copy could not be made: " + r.why);
        if (!r.ok) g_content_log.close();
        return r;
    };
    s.run_build = [](const std::atomic<bool>& abandoned) {
        g_content_log.line("emf", "loading emf_content.dll");
        const auto out = emf::loader::run_content_build(
            content_dll(), emf::loader::content_request(shadow_dir(), "enshrouded", false),
            [](int level, const std::string& line) {
                static const char* names[] = {"error", "warning", "info", "debug"};
                g_content_log.line(level >= 0 && level <= 3 ? names[level] : "log", line);
                if (level <= 2) g_splash.line(line);
            },
            emf::loader::kReplyCapacity, &abandoned);
        g_content_log.line("emf", "code " + std::to_string(out.code) + ": " + out.message +
                                      (out.unloaded ? "; emf_content.dll is unloaded" : "; emf_content.dll stays loaded"));
        g_content_log.close();
        return out;
    };
    // EMF's own files in the shadow folder are written and deleted only while no folder on the way is a junction
    s.write_state = [](const emf::loader::ContentState& st) {
        if (!emf::loader::shadow_path_safe(g_dir, shadow_dir())) {
            loader_log(g_dir, "EML content: emf\\content is a junction or link; EMF writes nothing there");
            return;
        }
        emf::loader::ContentState with = st;
        for (const auto& dll : chain_names(g_dir)) with.chain_checked.push_back(chain_stamp(dll));  // all read: not EML's
        if (!emf::loader::write_state(state_path(), with)) loader_log(g_dir, "EML content: could not write emf_content_state.json");
    };
    s.write_record = [](const std::string& record) {
        if (!emf::loader::shadow_path_safe(g_dir, shadow_dir())) return;
        if (!emf::loader::cio::write_file_atomic(shadow_dir() + L"\\shadow.txt", record))
            loader_log(g_dir, "EML content: could not write shadow.txt; the redirect stays off");
    };
    s.drop_record = [] {
        if (emf::loader::shadow_path_safe(g_dir, shadow_dir())) DeleteFileW((shadow_dir() + L"\\shadow.txt").c_str());
    };
    s.dll_gone = [] { return GetModuleHandleW(content_dll().c_str()) == nullptr; };
    s.alert = [](const std::string& text) {
        MessageBoxW(nullptr, emf::loader::cio::from_utf8(text).c_str(), L"EMF", MB_OK | MB_ICONWARNING | MB_TOPMOST);
    };
    s.now = [] { return utc_now(); };
    return s;
}

emf::loader::StartSteps& steps() {
    static emf::loader::StartSteps s = make_start_steps();
    return s;
}

// The game client's entry point lands here first when EML content is built at this start (main thread, loader lock
// free, no game code has run): its bytes go back, then the build, the chained DLLs and EMF's core (content_start.h),
// then it runs.
DWORD WINAPI held_content_entry(void* arg) {
    // The entry point's bytes go back first: a chained DLL that patches the entry itself (ASI loaders do) must find the
    // game's own bytes, and nothing else may race EMF's change of that page's protection. The game's code still runs
    // only when this function calls it below.
    if (!emf::loader::release_entry(g_content_hold)) {
        loader_log(g_dir, "could not restore the game's entry point; stopping");
        ExitProcess(1);
    }
    try {
        emf::loader::run_held_content(g_content, steps());
    } catch (...) {
        loader_log(g_dir, "EML content: the build stopped on an error in the loader; the game starts without it");
        if (!GetModuleHandleW(content_dll().c_str())) {
            try {
                steps().set_status({"failed", 0, 0, "the build stopped on an error in EMF's loader"});
                if (!g_chain_ran) run_chain(g_dir, g_start.chain);
                if (!g_core_ran) start_core_today();
            } catch (...) {
            }
        }
    }
    using EntryFn = DWORD(WINAPI*)(void*);
    return reinterpret_cast<EntryFn>(g_content_hold.entry)(arg);
}
}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // One EMF loader per process: a copy of it listed in the chain file under another name (a player following
        // another tool's chain instructions) only forwards DirectInput and does nothing else.
        const std::wstring guard = L"Local\\EMF_loader_" + std::to_wstring(GetCurrentProcessId());
        CreateMutexW(nullptr, FALSE, guard.c_str());  // kept for the life of the process
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            wchar_t self[MAX_PATH];
            GetModuleFileNameW(inst, self, MAX_PATH);
            loader_log(game_dir(), ("another EMF loader already runs in this game; this copy (" +
                                    emf::loader::utf8_name(self) + ") does nothing").c_str());
            return TRUE;
        }
        // set again below only for this start's EML content: a value a parent process passed on never counts
        SetEnvironmentVariableW(emf::loader::kContentStatusVariable, nullptr);
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        g_exe = exe;
        g_dir = game_dir();
        // EMF.exe's "Play without EMF mods": taken first of all, so that it counts for this one start only
        const auto play = emf::loader::is_server_exe(exe) ? emf::loader::PlayOnce::Normal : take_play_once_flag(g_dir);
        g_start = emf::loader::start_plan(play);
        // EML content: off (today's order), the current shadow copy, or a build on the held entry (content_start.h)
        g_content = plan_this_start(play);
        emf::loader::start_in_dllmain(g_content, steps());
    }
    return TRUE;
}
