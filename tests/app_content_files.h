#pragma once
// Temp game folders for the tests of EMF.exe's EML content card (tests/test_app_content_*.cpp): EMF installed, the
// game's data (a KFC4 header with 2 containers), emf_content.dll and one EML mod, and what a build leaves (a real
// shadow copy made with the loader's build_shadow, its record and a state).
#include <doctest.h>

#include <windows.h>

#include <ctime>
#include <filesystem>
#include <string>

#include "app_test_files.h"
#include "content_fixtures.h"
#include "loader/content_base.h"
#include "loader/content_fingerprint.h"
#include "loader/content_shadow.h"
#include "loader/content_state.h"
#include "loader/eml_mods.h"

namespace app_content_test {

using app_test::TempGame;
using content_fixtures::kfc_bytes;
namespace loader = emf::loader;

inline const char* kTools =
    "shroudforge | ShroudForge | text:shroudforge | beside:shroudforge | same-loader:dbghelp.dll\n"
    "eml | EML (Enshrouded Mod Loader) | text:EML_LOG_FILE_PATH\n";

inline const char* kVersion = "1.2.3";  // the installed emf_core.dll's version (the fingerprint's EMF version)

inline std::string mod_json(const std::string& id, const std::string& name, const std::string& caps = R"(["patch"])") {
    return R"({"id": ")" + id + R"(", "name": ")" + name + R"(", "version": "1.0.0", "capabilities": )" + caps + "}";
}

// "YYYY-MM-DDTHH:MM:SSZ", `ago` seconds before now.
inline std::string utc_text(int ago) {
    const std::time_t t = std::time(nullptr) - ago;
    std::tm tm{};
    gmtime_s(&tm, &t);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

// Every file under `dir` last written `ago` seconds before now (a folder as it was before a build).
inline void age_files(const std::wstring& dir, int ago) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t{};
    t.LowPart = now.dwLowDateTime;
    t.HighPart = now.dwHighDateTime;
    t.QuadPart -= static_cast<ULONGLONG>(ago) * 10000000ull;
    const FILETIME then{t.LowPart, t.HighPart};
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const HANDLE f = CreateFileW(e.path().c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
        REQUIRE(f != INVALID_HANDLE_VALUE);
        SetFileTime(f, nullptr, nullptr, &then);
        CloseHandle(f);
    }
}

// A game folder with EMF, the game's data (a KFC4 header with 2 containers) and, unless told otherwise,
// emf_content.dll and one EML mod; every file last written two hours ago.
struct ContentGame : TempGame {
    explicit ContentGame(const wchar_t* name, bool with_dll = true, bool with_mod = true) : TempGame(name) {
        write(L"dinput8.dll", "MZ EMF-LOADER-7d3c9f fake");
        write(L"emf\\emf_core.dll", std::string("MZ emf_core 1.2.3\0 more", 23));
        write(L"emf\\known_tools.txt", kTools);
        write(L"enshrouded.kfc", kfc_bytes("7|r|t", 2));
        write(L"enshrouded.kfc_resources", "resources");
        write(L"enshrouded_000.dat", "dat 0");
        write(L"enshrouded_001.dat", "dat 1");
        if (with_dll) write(L"emf\\emf_content.dll", "emf_content stand-in");
        if (with_mod) {
            write(L"mods\\beasts\\mod.json", mod_json("beasts", "Beasts of the Shroud"));
            write(L"mods\\beasts\\main.lua", "-- the mod");
        }
        age_files(dir, 7200);
    }
    std::wstring shadow() const { return at(L"emf\\content\\client"); }
    loader::BaseChoice base() const {
        return loader::choose_base(loader::read_base_file(at(L"enshrouded.kfc")), loader::read_base_file(at(L"enshrouded.kfc.bak")));
    }
    // `spelled`: the game folder's path as the loader got it ("" = as this test has it)
    std::string fingerprint(const std::wstring& spelled = L"") const {
        const std::wstring d = spelled.empty() ? dir : spelled;
        return loader::content_fingerprint(loader::collect_fingerprint_input(
            d, "enshrouded", base(), loader::scan_eml_mods(at(L"mods")).usable, loader::cio::join(d, L"emf\\emf_content.dll"),
            kVersion));
    }
    // What a good build leaves: the shadow copy (the loader's build_shadow), its record and a state with this
    // folder's fingerprint (computed from the path as `spelled`).
    void built(int code = 0, const std::string& message = "", int ago = 3600, const std::wstring& spelled = L"") const {
        const auto r = loader::build_shadow(dir, shadow(), "enshrouded", base(), loader::scan_eml_mods(at(L"mods")).usable);
        REQUIRE_MESSAGE(r.ok, r.why);
        if (code == 0) REQUIRE(loader::cio::write_file_atomic(shadow() + L"\\shadow.txt", r.record));
        loader::ContentState s;
        s.fingerprint = fingerprint(spelled);
        s.code = code;
        s.message = message;
        s.mods = {"beasts 1.0.0"};
        if (code != 0) s.errors = {"mods\\beasts\\main.lua: attempt to index a nil value"};
        s.ms = 28000;
        s.built_at = utc_text(ago);
        s.reported = true;
        REQUIRE(loader::write_state(shadow() + L"\\emf_content_state.json", s));
    }
};

}  // namespace app_content_test
