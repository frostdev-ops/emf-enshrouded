// EMF.exe's EML content card (app/content_panel.h): who applies EML content at the next start and why, what the last
// build did, the copy's disk use, and EML's dbghelp.dll set aside. The panel reuses the loader's own rules
// (src/loader/content_*.h, eml_mods.h), so these tests build temp game folders and real shadow copies with the loader's
// functions and check that the panel reports what the loader would do at the next start.
#include <doctest.h>

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include "app/content_panel.h"
#include "app_test_files.h"
#include "content_fixtures.h"
#include "core/json.h"
#include "core/known_tools.h"
#include "loader/chain_list.h"
#include "loader/content_base.h"
#include "loader/content_build.h"
#include "loader/content_fingerprint.h"
#include "loader/content_shadow.h"
#include "loader/content_start.h"
#include "loader/content_state.h"
#include "loader/eml_mods.h"

using namespace emf::app;
using app_test::TempGame;
using content_fixtures::kfc_bytes;
namespace loader = emf::loader;
namespace json = emf::core::json;

namespace {

const char* kTools =
    "shroudforge | ShroudForge | text:shroudforge | beside:shroudforge | same-loader:dbghelp.dll\n"
    "eml | EML (Enshrouded Mod Loader) | text:EML_LOG_FILE_PATH\n";

const char* kVersion = "1.2.3";  // the installed emf_core.dll's version (the fingerprint's EMF version)

std::string mod_json(const std::string& id, const std::string& name, const std::string& caps = R"(["patch"])") {
    return R"({"id": ")" + id + R"(", "name": ")" + name + R"(", "version": "1.0.0", "capabilities": )" + caps + "}";
}

// "YYYY-MM-DDTHH:MM:SSZ", `ago` seconds before now.
std::string utc_text(int ago) {
    const std::time_t t = std::time(nullptr) - ago;
    std::tm tm{};
    gmtime_s(&tm, &t);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

// Every file under `dir` last written `ago` seconds before now (a folder as it was before a build).
void age_files(const std::wstring& dir, int ago) {
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
    std::string fingerprint() const {
        return loader::content_fingerprint(loader::collect_fingerprint_input(
            dir, "enshrouded", base(), loader::scan_eml_mods(at(L"mods")).usable, at(L"emf\\emf_content.dll"), kVersion));
    }
    // What a good build leaves: the shadow copy (the loader's build_shadow), its record and a state with this
    // folder's fingerprint.
    void built(int code = 0, const std::string& message = "", int ago = 3600) const {
        const auto r = loader::build_shadow(dir, shadow(), "enshrouded", base(), loader::scan_eml_mods(at(L"mods")).usable);
        REQUIRE_MESSAGE(r.ok, r.why);
        if (code == 0) REQUIRE(loader::cio::write_file_atomic(shadow() + L"\\shadow.txt", r.record));
        loader::ContentState s;
        s.fingerprint = fingerprint();
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

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// A member of a JSON object that must be there.
const json::Value& at(const json::Value& v, const char* key) {
    const json::Value* x = v.get(key);
    REQUIRE_MESSAGE(x, key);
    return *x;
}

const ContentPanel::LeftOut* left_out(const ContentPanel& p, const std::string& folder) {
    for (const auto& l : p.left_out)
        if (l.folder == folder) return &l;
    return nullptr;
}

// The loader's own decision for the folder, gathered the way its DllMain does (dllmain.cpp facts_io): the panel's
// `why` must be the same line.
std::string loader_why(const TempGame& g) {
    loader::FactsIo io;
    const std::wstring dir = g.dir;
    io.mode = [dir] {
        return loader::content_mode([&](const wchar_t* section, const wchar_t* key) {
            wchar_t buf[128] = {};
            GetPrivateProfileStringW(section, key, L"", buf, 128, (dir + L"\\emf\\emf_core.ini").c_str());
            return std::wstring(buf);
        });
    };
    io.look.exists = [dir](const std::wstring& rel) {
        const DWORD a = GetFileAttributesW((dir + L"\\" + rel).c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    };
    io.look.tool_name = [dir](const std::wstring& rel) {
        const auto tools = emf::core::parse_known_tools(kTools);
        const std::string word = emf::core::tool_for_dll_text(tools, content_fixtures::file_text(dir + L"\\" + rel));
        for (const auto& t : tools)
            if (t.word == word) return t.name;
        return std::string();
    };
    io.look.chain = loader::parse_chain_list(content_fixtures::file_text(dir + L"\\dinput8-chain.ini"));
    io.dll_present = [dir] { return GetFileAttributesW((dir + L"\\emf\\emf_content.dll").c_str()) != INVALID_FILE_ATTRIBUTES; };
    io.scan_mods = [dir] { return loader::scan_eml_mods(dir + L"\\mods"); };
    io.base = [dir] {
        return loader::choose_base(loader::read_base_file(dir + L"\\enshrouded.kfc"), loader::read_base_file(dir + L"\\enshrouded.kfc.bak"));
    };
    return loader::decide_content(loader::collect_facts(io, loader::PlayOnce::Normal, false)).why;
}

}  // namespace

TEST_CASE("app content: the loader's constants the panel copies are the loader's") {
    CHECK(kContentBuildRunning == loader::kBuildRunning);
    CHECK(kContentMaxBuildAttempts == loader::kMaxBuildAttempts);
}

TEST_CASE("app content: without emf_content.dll the card says it is not installed") {
    ContentGame g(L"cp_nodll", false);
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::NotInstalled);
    CHECK_FALSE(p.dll.present);
    CHECK(p.why == loader_why(g));
    CHECK(has(p.detail, "emf_content.dll"));
}

TEST_CASE("app content: [content] mode=off is off, with the loader's reason") {
    ContentGame g(L"cp_off");
    g.write(L"emf\\emf_core.ini", "[backup]\r\non_start=1\r\n[content]\r\nmode=off ; never\r\n");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::Off);
    CHECK(p.mode == "off");
    CHECK(p.why == loader_why(g));
    CHECK(has(p.detail, "mode=off"));
    CHECK(p.dll.present);
    CHECK(p.dll.size == 20);
    CHECK(p.dll.time > 1600000000);  // Unix seconds
}

TEST_CASE("app content: EML's dbghelp.dll applies it, and EMF offers to take over") {
    ContentGame g(L"cp_emldbg");
    g.write(L"dbghelp.dll", "MZ EML proxy EML_LOG_FILE_PATH");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::OtherTool);
    CHECK(p.other_tool.rfind("EML", 0) == 0);
    CHECK(has(p.headline, "EML"));
    CHECK(p.why == loader_why(g));
    CHECK(p.take_over == TakeOver::Offer);
}

TEST_CASE("app content: ShroudForge's dbghelp.dll applies it, and EMF never offers to take over") {
    ContentGame g(L"cp_sfdbg");
    g.write(L"dbghelp.dll", "MZ shroudforge runtime EML_LOG_FILE_PATH");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::OtherTool);
    CHECK(p.other_tool == "ShroudForge");
    CHECK(p.take_over == TakeOver::None);
}

TEST_CASE("app content: EML's dinput8.dll in dinput8-chain.ini applies it, with no take-over") {
    ContentGame g(L"cp_emlchain");
    g.write(L"eml.dll", "MZ EML proxy EML_LOG_FILE_PATH");
    g.write(L"dinput8-chain.ini", "[Chain]\r\neml.dll\r\n");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::OtherTool);
    CHECK(has(p.other_tool, "dinput8-chain.ini"));
    CHECK(p.why == loader_why(g));
    CHECK(p.take_over == TakeOver::None);
}

TEST_CASE("app content: no EML mod EMF can build is off, and the mods left out carry the loader's reasons") {
    ContentGame g(L"cp_nomods", true, false);
    g.write(L"mods\\.sleeping\\mod.json", mod_json("sleeping", "Sleeping"));
    g.write(L"mods\\npc_explorer\\mod.json", mod_json("npc_explorer", "NPC Explorer", R"(["export"])"));
    g.write(L"mods\\runtime_mod\\mod.json", mod_json("runtime_mod", "Runtime", R"(["patch", "runtime"])"));
    g.write(L"mods\\packed.zip", "PK zip");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::Off);
    CHECK(p.why == loader_why(g));
    CHECK(has(p.detail, "no EML mod"));
    CHECK(p.mods.empty());
    REQUIRE(p.left_out.size() == 4);
    REQUIRE(left_out(p, ".sleeping"));
    CHECK(has(left_out(p, ".sleeping")->why, "starts with a dot"));
    REQUIRE(left_out(p, "npc_explorer"));
    CHECK(has(left_out(p, "npc_explorer")->why, "only exports"));
    REQUIRE(left_out(p, "runtime_mod"));
    CHECK(has(left_out(p, "runtime_mod")->why, "runtime loader"));
    REQUIRE(left_out(p, "packed.zip"));
    CHECK(has(left_out(p, "packed.zip")->why, ".zip"));
}

TEST_CASE("app content: a mod whose id another folder took is left out, the first one is built") {
    ContentGame g(L"cp_dupe");
    g.write(L"mods\\beasts_copy\\mod.json", mod_json("beasts", "Beasts again"));
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::BuildsNext);
    REQUIRE(p.mods.size() == 1);
    CHECK(p.mods[0].folder == "beasts");
    CHECK(p.mods[0].name == "Beasts of the Shroud");
    CHECK(p.mods[0].version == "1.0.0");
    REQUIRE(left_out(p, "beasts_copy"));
    CHECK(has(left_out(p, "beasts_copy")->why, "taken by the folder beasts"));
}

TEST_CASE("app content: never built yet builds at the next start, and says so") {
    ContentGame g(L"cp_never");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::BuildsNext);
    CHECK(p.why == loader_why(g));
    CHECK(p.headline == "Builds at the next start");
    CHECK(has(p.detail, "not built"));
    CHECK_FALSE(p.last_build);
    REQUIRE(p.mods.size() == 1);
    CHECK_FALSE(p.mods[0].built);
}

TEST_CASE("app content: a current copy with a good build is ready") {
    ContentGame g(L"cp_ready");
    g.built();
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::Ready);
    CHECK(p.headline == "Ready");
    REQUIRE(p.last_build);
    CHECK(p.last_build->code == 0);
    CHECK(p.last_build->ms == 28000);
    CHECK(p.last_build->mods == std::vector<std::string>{"beasts 1.0.0"});
    REQUIRE(p.mods.size() == 1);
    CHECK(p.mods[0].built);
    CHECK(p.copy_there);
    CHECK(p.copy_bytes > 0);
    CHECK_FALSE(p.patched_in_place);
}

TEST_CASE("app content: a mod file changed since the build: it builds at the next start, naming the mod") {
    ContentGame g(L"cp_touched");
    g.built();
    g.write(L"mods\\beasts\\main.lua", "-- the mod, changed");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::BuildsNext);
    CHECK(has(p.detail, "Beasts of the Shroud changed"));
    CHECK_FALSE(has(p.detail, "game changed"));
    CHECK_FALSE(has(p.detail, "emf_content.dll changed"));
    REQUIRE(p.mods.size() == 1);
    CHECK(p.mods[0].built);  // in the last build, in its earlier form
}

TEST_CASE("app content: a mod added since the build is named as new") {
    ContentGame g(L"cp_added");
    g.built();
    g.write(L"mods\\wolves\\mod.json", mod_json("wolves", "Wolf Pack"));
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::BuildsNext);
    CHECK(has(p.detail, "Wolf Pack"));
    REQUIRE(p.mods.size() == 2);
}

TEST_CASE("app content: a current state with the copy gone builds again") {
    ContentGame g(L"cp_gone");
    g.built();
    std::filesystem::remove(g.shadow() + L"\\enshrouded.kfc");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::BuildsNext);
    CHECK(has(p.detail, "copy"));
}

TEST_CASE("app content: the same files failed before: failed, with the message, until something changes") {
    ContentGame g(L"cp_failed");
    g.built(1, "a mod could not be read");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::Failed);
    CHECK(p.headline == "Failed");
    CHECK(has(p.detail, "a mod could not be read"));
    CHECK(has(p.detail, "EMF tries again when a mod, the game or emf_content.dll changes"));
    REQUIRE(p.last_build);
    CHECK(p.last_build->code == 1);
    REQUIRE(p.last_build->errors.size() == 1);
}

TEST_CASE("app content: a build the game stopped in is tried once more, then it counts as failed") {
    ContentGame g(L"cp_running");
    g.built();
    auto s = *loader::read_state(g.shadow() + L"\\emf_content_state.json");
    s.code = kContentBuildRunning;
    s.message = "a build was running";
    s.attempts = 1;
    REQUIRE(loader::write_state(g.shadow() + L"\\emf_content_state.json", s));
    ContentPanel p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::BuildsNext);
    CHECK(has(p.detail, "did not finish"));
    s.attempts = 2;
    REQUIRE(loader::write_state(g.shadow() + L"\\emf_content_state.json", s));
    p = content_panel(g.dir, false);
    CHECK(p.status == ContentStatus::Failed);
    CHECK(has(p.detail, "stopped the game 2 times"));
}

TEST_CASE("app content: a corrupt, empty or huge state builds at the next start and never crashes") {
    ContentGame g(L"cp_corrupt");
    g.built();
    const std::wstring state = g.shadow() + L"\\emf_content_state.json";
    for (const std::string& text : {std::string("{not json"), std::string(), std::string(5u << 20, 'x'),
                                    std::string(R"({"fingerprint": 7, "code": "zero"})")}) {
        content_fixtures::put_file(state, text);
        const ContentPanel p = content_panel(g.dir, false);
        CHECK(p.status == ContentStatus::BuildsNext);
        CHECK(has(p.detail, "could not be read"));
        CHECK_FALSE(p.last_build);
    }
    // unknown fields are passed over
    content_fixtures::put_file(state, R"({"fingerprint": ")" + g.fingerprint() +
                                          R"(", "code": 0, "mods": ["beasts 1.0.0"], "unloaded": true, "new_field": {"x": [1]}})");
    CHECK(content_panel(g.dir, false).status == ContentStatus::Ready);
}

TEST_CASE("app content: a .kfc.bak of the same game build with another size means the game's files were patched in place") {
    ContentGame g(L"cp_patched");
    g.write(L"enshrouded.kfc", kfc_bytes("7|r|t", 2) + std::string(100, 'p'));  // EML grew the index in place
    g.write(L"enshrouded.kfc.bak", kfc_bytes("7|r|t", 2));
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.patched_in_place);
    CHECK(p.status == ContentStatus::BuildsNext);
}

TEST_CASE("app content: the copy's disk use counts only its own files, never the links to the game's files") {
    ContentGame g(L"cp_bytes");
    g.mkdir(L"emf\\content\\client\\mods\\beasts");
    REQUIRE(CreateHardLinkW((g.shadow() + L"\\enshrouded_000.dat").c_str(), g.at(L"enshrouded_000.dat").c_str(), nullptr));
    content_fixtures::put_file(g.shadow() + L"\\enshrouded.kfc", std::string(1000, 'k'));
    content_fixtures::put_file(g.shadow() + L"\\mods\\beasts\\main.lua", std::string(234, 'l'));
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.copy_there);
    CHECK(p.copy_bytes == 1234);
}

TEST_CASE("app content: EML's dbghelp.dll set aside by EMF is given") {
    ContentGame g(L"cp_given");
    g.write(L"eml-dbghelp.dll", "MZ EML proxy EML_LOG_FILE_PATH");
    g.write(L"emf\\moved.txt", "moved\tcreative.dll\r\nset-aside\teml-dbghelp.dll\tdbghelp.dll\r\n");
    const ContentPanel p = content_panel(g.dir, false);
    CHECK(p.take_over == TakeOver::Given);
    CHECK(p.set_aside == "eml-dbghelp.dll");
    CHECK(p.status == ContentStatus::BuildsNext);
}

TEST_CASE("app content: the dedicated server's folder gets one line") {
    ContentGame g(L"cp_server");
    const ContentPanel p = content_panel(g.dir, true);
    CHECK(p.status == ContentStatus::ServerLater);
    CHECK(p.headline == "The dedicated server gets EML content in a later version of EMF.");
    CHECK(p.mods.empty());
}

TEST_CASE("app content: the panel as JSON for the page") {
    ContentGame g(L"cp_json");
    g.write(L"mods\\.off\\mod.json", mod_json("off", "<img src=x onerror=alert(1)>"));
    g.built();
    const auto r = json::parse(content_panel_json(content_panel(g.dir, false)));
    REQUIRE(r.value);
    const auto& v = *r.value;
    CHECK(at(v, "status").s == "ready");
    CHECK(at(v, "headline").s == "Ready");
    CHECK(at(v, "mode").s == "auto");
    CHECK(at(at(v, "dll"), "present").b);
    REQUIRE(at(v, "mods").arr.size() == 1);
    CHECK(at(at(v, "mods").arr[0], "name").s == "Beasts of the Shroud");
    CHECK(at(at(v, "mods").arr[0], "built").b);
    REQUIRE(at(v, "left_out").arr.size() == 1);
    CHECK(at(at(v, "left_out").arr[0], "folder").s == ".off");
    CHECK(at(at(v, "last_build"), "ms").n == 28000);
    CHECK(at(v, "copy_bytes").n > 0);
    CHECK_FALSE(at(v, "copy_text").s.empty());
    CHECK(at(v, "take_over").s == "none");
    CHECK_FALSE(at(v, "patched_in_place").b);
}

TEST_CASE("app content: the fingerprint of a 150 MB pack folder with 2000 files costs under 200 ms") {
    ContentGame g(L"cp_speed");
    for (int i = 0; i < 2000; ++i) {
        wchar_t name[64];
        swprintf_s(name, L"mods\\beasts\\textures\\t%04d.png", i);
        g.write(name, "png");
    }
    {  // one 150 MB file, made by setting its end (no data written)
        const HANDLE f = CreateFileW(g.at(L"mods\\beasts\\big.lua").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        REQUIRE(f != INVALID_HANDLE_VALUE);
        LARGE_INTEGER at{};
        at.QuadPart = 150ll << 20;
        SetFilePointerEx(f, at, nullptr, FILE_BEGIN);
        SetEndOfFile(f);
        CloseHandle(f);
    }
    (void)content_panel(g.dir, false);  // the folder's entries in the file system's cache, as on a second look
    const auto t0 = std::chrono::steady_clock::now();
    const ContentPanel p = content_panel(g.dir, false);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(p.status == ContentStatus::BuildsNext);
    CHECK_MESSAGE(ms < 200, "the panel took " << ms << " ms");
}
