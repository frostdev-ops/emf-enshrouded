// EML content end to end: the stand-in game (tools/standin_game, as enshrouded.exe) imports EMF's built dinput8.dll,
// the stand-in emf_content.dll (tools/standin_content) is installed as <game>\emf\emf_content.dll, a stand-in DLL mod
// (tools/standin_mod) is chained as chain_standin.dll and is also EMF's "core" (emf\emf_core.dll: it has no
// emf_core_start, so the loader says so once it loaded it). The stand-in game writes what it read and which of these
// were loaded when its own code ran; the stand-in content DLL writes, at its build, whether the chained DLL and the core
// were loaded then.
#include <doctest.h>

#include <windows.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include "content_fixtures.h"
#include "loader/content_state.h"
#include "loader/play_once.h"
#include "test_scratch.h"

using content_fixtures::file_text;
using content_fixtures::kfc_bytes;
using content_fixtures::put_file;

namespace {
std::wstring bin_dir() {
    wchar_t buf[MAX_PATH];
    const std::wstring exe(buf, GetModuleFileNameW(nullptr, buf, MAX_PATH));
    return exe.substr(0, exe.find_last_of(L'\\'));
}

// Waits until no other process holds the file (a virus scanner may keep a file a moment after its writer closed it).
void wait_unlocked(const std::wstring& path) {
    for (int i = 0; i < 50; ++i) {
        const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            CloseHandle(f);
            return;
        }
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return;
        Sleep(100);
    }
}

// How often `part` is in `text`.
size_t count_of(const std::string& text, const std::string& part) {
    size_t n = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) ++n;
    return n;
}

struct ContentGame {
    std::wstring dir, shadow;
    // `exe`: which stand-in game (standin_game.exe has MSVC's start-up at its entry, standin_game_bind.exe Steam's DRM
    // stub as the real client, standin_game_jump.exe another tool's jump); `word`: what the stand-in DLL does
    explicit ContentGame(const wchar_t* name, bool with_dll = true, const wchar_t* exe = L"standin_game.exe",
                         const char* word = "ok")
        : dir(test_scratch::temp_path(name)) {
        test_scratch::remove_at_exit(dir);
        shadow = dir + L"\\emf\\content\\client";
        std::filesystem::create_directories(dir + L"\\emf");
        std::filesystem::copy_file(bin_dir() + L"\\standin_game\\" + exe, dir + L"\\enshrouded.exe");
        std::filesystem::copy_file(bin_dir() + L"\\dist\\dinput8.dll", dir + L"\\dinput8.dll");
        std::filesystem::copy_file(bin_dir() + L"\\standin\\dinput8.dll", dir + L"\\chain_standin.dll");
        std::filesystem::copy_file(bin_dir() + L"\\standin\\dinput8.dll", dir + L"\\emf\\emf_core.dll");
        if (with_dll)
            std::filesystem::copy_file(bin_dir() + L"\\standin_content\\emf_content.dll", dir + L"\\emf\\emf_content.dll");
        put_file(dir + L"\\dinput8-chain.ini", "[Chain]\r\nchain_standin.dll\r\n");
        put_file(dir + L"\\enshrouded.kfc", kfc_bytes("1|e2e|t", 2));
        put_file(dir + L"\\enshrouded.kfc_resources", "the game's resources\r\n");
        put_file(dir + L"\\enshrouded_000.dat", "the game's dat 0\r\n");
        put_file(dir + L"\\enshrouded_001.dat", "the game's dat 1\r\n");
        put_file(dir + L"\\mods\\beasts\\mod.json",
                 R"({"id": "beasts", "name": "Beasts", "version": "1.0.0", "capabilities": ["patch"]})");
        put_file(dir + L"\\mods\\beasts\\bear.txt", "bear 1");
        if (with_dll) put_file(shadow + L"\\standin.txt", std::string(word) + "\r\n");  // what the stand-in DLL does
        check("wait emf_core.dll\r\nemf_content.dll\r\nchain_standin.dll\r\nemf_core.dll\r\n");
    }
    ~ContentGame() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    // the modules the stand-in game reports (and waits for, with "wait <name>")
    void check(const std::string& lines) { put_file(dir + L"\\standin_check.txt", lines); }
    std::string loader_log() const { return file_text(dir + L"\\emf\\emf_loader.log"); }
    // how many processes loaded the stand-in DLL (its DllMain writes emf\standin_attach_<pid>.txt)
    size_t dll_attaches() const {
        size_t n = 0;
        for (const auto& e : std::filesystem::directory_iterator(dir + L"\\emf"))
            if (e.path().filename().wstring().rfind(L"standin_attach_", 0) == 0) ++n;
        return n;
    }
    // runs the stand-in game and returns its standin_out.txt
    std::string run() {
        for (const wchar_t* f : {L"\\emf\\emf_loader.log", L"\\emf\\chain_status.txt", L"\\emf\\file_opens.log",
                                 L"\\emf\\content.log", L"\\standin_out.txt", L"\\emf\\content\\client\\built.txt",
                                 L"\\emf\\content\\client\\standin_seq.txt", L"\\emf\\content\\client\\enshrouded.kfc",
                                 L"\\emf\\content\\client\\emf_content_state.json"})
            wait_unlocked(dir + f);
        std::wstring cmd = L"\"" + dir + L"\\enshrouded.exe\"";
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        REQUIRE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si,
                               &pi));
        const DWORD w = WaitForSingleObject(pi.hProcess, 60000);
        if (w != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
        DWORD code = 99;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CHECK(w == WAIT_OBJECT_0);
        CHECK(code == 0);
        return file_text(dir + L"\\standin_out.txt");
    }
};

void play_once(const ContentGame& g, emf::loader::PlayOnce mode) {
    put_file(g.dir + L"\\emf\\play_once.txt", emf::loader::play_once_text(mode, static_cast<int64_t>(std::time(nullptr))));
}

bool before(const std::string& text, const std::string& a, const std::string& b) {
    const size_t x = text.find(a), y = text.find(b);
    return x != std::string::npos && y != std::string::npos && x < y;
}
}  // namespace

TEST_CASE("e2e content: first start builds, then chain, then core; the stand-in exe reads the shadow") {
    REQUIRE(std::filesystem::exists(bin_dir() + L"\\standin_content\\emf_content.dll"));
    ContentGame g(L"emf_e2e_first");
    const std::string out = g.run();
    CHECK(out.find("kfc=built kfc 1\n") != std::string::npos);    // the game reads the shadow's built index
    CHECK(out.find("dat=the game's dat 0\n") != std::string::npos);  // through the shadow's link
    CHECK(out.find("loaded emf_content.dll=no\n") != std::string::npos);  // unloaded before the game's code ran
    CHECK(out.find("loaded chain_standin.dll=yes\n") != std::string::npos);
    CHECK(out.find("loaded emf_core.dll=yes\n") != std::string::npos);
    CHECK(out.find("status=built\t1\t") != std::string::npos);  // what the core learns
    // the build ran before the chained DLL and the core were loaded, and after the entry's bytes went back (a chained
    // DLL that patches the entry itself is never overwritten by EMF's release)
    CHECK(file_text(g.shadow + L"\\built.txt") == "seq=1 chain_loaded=no core_loaded=no entry_held=no\n");
    CHECK(out.find("at_tls chain_standin.dll=no\n") != std::string::npos);  // not in DllMain: on the held entry
    CHECK(g.dll_attaches() == 1);
    const std::string log = g.loader_log();
    INFO(log);
    CHECK(before(log, "EML content: EMF builds it from 1 mod", "a build is needed at this start (no earlier build)"));
    CHECK(before(log, "a build is needed", "EML content: built 1 mod in "));
    CHECK(before(log, "EML content: built 1 mod in ", "chain_standin.dll"));
    CHECK(before(log, "chain_standin.dll", "emf_core.dll has no emf_core_start export"));
    CHECK(file_text(g.dir + L"\\emf\\file_opens.log").find("\\emf\\content\\client\\enshrouded.kfc\"") != std::string::npos);
    CHECK(file_text(g.dir + L"\\emf\\content.log").find("standin: building ") != std::string::npos);
    const auto state = emf::loader::read_state(g.shadow + L"\\emf_content_state.json");
    REQUIRE(state.has_value());
    CHECK(state->code == 0);
    CHECK(state->mods == std::vector<std::string>{"beasts 1.0.0"});
    CHECK(file_text(g.dir + L"\\enshrouded.kfc") == kfc_bytes("1|e2e|t", 2));  // the game's own files stay
    CHECK_FALSE(std::filesystem::exists(g.dir + L"\\enshrouded_002.dat"));
    CHECK(file_text(g.shadow + L"\\enshrouded_002.dat") == "built dat 1\r\n");  // EML's new container, in the shadow
}

TEST_CASE("e2e content: second start does not load the dll") {
    ContentGame g(L"emf_e2e_second");
    g.run();
    const std::string out = g.run();
    CHECK(file_text(g.shadow + L"\\built.txt").find("seq=1 ") == 0);  // no second build
    CHECK(out.find("kfc=built kfc 1\n") != std::string::npos);
    CHECK(out.find("loaded chain_standin.dll=yes\n") != std::string::npos);
    CHECK(out.find("status=current\t1\t") != std::string::npos);
    const std::string log = g.loader_log();
    INFO(log);
    CHECK(count_of(log, "a build is needed") == 1);
    CHECK(count_of(log, "EML content: the shadow copy is current (1 mod, built ") == 1);
    CHECK(count_of(log, "standin: building") == 0);  // the DLL's lines go to content.log only, which a build empties
    CHECK(count_of(file_text(g.dir + L"\\emf\\content.log"), "standin: building ") == 1);
    CHECK(g.dll_attaches() == 1);  // the stand-in DLL's DllMain ran in the first start only
    CHECK(out.find("at_tls chain_standin.dll=yes\n") != std::string::npos);  // chained in DllMain: no hold
}

TEST_CASE("e2e content: a changed mod file rebuilds, and so does a game update") {
    ContentGame g(L"emf_e2e_changed");
    g.run();
    put_file(g.dir + L"\\mods\\beasts\\bear.txt", "a bigger bear 2");
    std::string out = g.run();
    CHECK(file_text(g.shadow + L"\\built.txt").find("seq=2 ") == 0);
    CHECK(out.find("kfc=built kfc 2\n") != std::string::npos);
    CHECK(file_text(g.shadow + L"\\mods\\beasts\\bear.txt") == "a bigger bear 2");
    CHECK(file_text(g.shadow + L"\\enshrouded_002.dat") == "built dat 2\r\n");  // the last build's container went first
    put_file(g.dir + L"\\enshrouded.kfc", kfc_bytes("2|e2e|t", 2));  // Steam updated the game
    out = g.run();
    CHECK(file_text(g.shadow + L"\\built.txt").find("seq=3 ") == 0);
    CHECK(out.find("kfc=built kfc 3\n") != std::string::npos);
    CHECK(count_of(g.loader_log(), "the game, the mods or emf_content.dll changed since the last build") == 2);
}

TEST_CASE("e2e content: Play without any mods: no build, no redirect, no chain") {
    ContentGame g(L"emf_e2e_nomods");
    g.check("emf_content.dll\r\nchain_standin.dll\r\nemf_core.dll\r\n");
    play_once(g, emf::loader::PlayOnce::NoMods);
    const std::string out = g.run();
    CHECK(out.find("kfc=KFC4") != std::string::npos);  // the game's own index
    CHECK(out.find("loaded chain_standin.dll=no\n") != std::string::npos);
    CHECK(out.find("loaded emf_core.dll=no\n") != std::string::npos);
    CHECK(out.find("status=\n") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(g.shadow + L"\\built.txt"));
    CHECK_FALSE(std::filesystem::exists(g.shadow + L"\\shadow.txt"));
    CHECK(g.loader_log().find("EML content: off for this start (Play without any mods in EMF.exe)") != std::string::npos);
}

TEST_CASE("e2e content: Play without EMF mods keeps EML content, without EMF's core") {
    ContentGame g(L"emf_e2e_noemf");
    g.check("emf_content.dll\r\nchain_standin.dll\r\nemf_core.dll\r\n");
    play_once(g, emf::loader::PlayOnce::NoEmf);
    const std::string out = g.run();
    CHECK(out.find("kfc=built kfc 1\n") != std::string::npos);
    CHECK(out.find("loaded chain_standin.dll=yes\n") != std::string::npos);
    CHECK(out.find("loaded emf_core.dll=no\n") != std::string::npos);
    CHECK(out.find("loaded emf_content.dll=no\n") != std::string::npos);
}

TEST_CASE("e2e content: no emf_content.dll: today's behaviour") {
    ContentGame g(L"emf_e2e_dormant", false);
    const std::string out = g.run();
    CHECK(out.find("kfc=KFC4") != std::string::npos);
    CHECK(out.find("dat=the game's dat 0\n") != std::string::npos);
    CHECK(out.find("loaded chain_standin.dll=yes\n") != std::string::npos);
    CHECK(out.find("loaded emf_core.dll=yes\n") != std::string::npos);
    CHECK(out.find("status=\n") != std::string::npos);  // nothing for the core
    // the chained DLL was there before the exe's TLS callbacks ran: loaded in DllMain, as today, so no hold
    CHECK(out.find("at_tls chain_standin.dll=yes\n") != std::string::npos);
    const std::string log = g.loader_log();
    INFO(log);
    CHECK(count_of(log, "EML content") == 1);  // one line, and it says why
    CHECK(log.find("emf_loader: EML content: off (emf_content.dll is not installed)\n") == 0);  // before everything
    CHECK(before(log, "EML content: off", "chain_standin.dll"));
    // nothing else written: no shadow copy, no content log, no file-open log (the redirect spike's switches are off)
    std::vector<std::wstring> emf_files;
    for (const auto& e : std::filesystem::directory_iterator(g.dir + L"\\emf")) emf_files.push_back(e.path().filename());
    std::sort(emf_files.begin(), emf_files.end());
    CHECK(emf_files == std::vector<std::wstring>{L"chain_status.txt", L"emf_core.dll", L"emf_loader.log"});
}

TEST_CASE("e2e content: a shadow copy whose record is gone is built again, never used half-checked") {
    ContentGame g(L"emf_e2e_record");
    g.run();
    wait_unlocked(g.shadow + L"\\shadow.txt");
    REQUIRE(DeleteFileW((g.shadow + L"\\shadow.txt").c_str()));
    const std::string out = g.run();
    CHECK(file_text(g.shadow + L"\\built.txt").find("seq=2 ") == 0);
    CHECK(out.find("kfc=built kfc 2\n") != std::string::npos);
    CHECK(out.find("status=built\t1\t") != std::string::npos);
    CHECK(g.loader_log().find("the shadow copy's record, shadow.txt, is missing") != std::string::npos);
}

TEST_CASE("e2e content: the real client's entry (Steam's DRM stub in .bind) is held, and the build runs") {
    ContentGame g(L"emf_e2e_bind", true, L"standin_game_bind.exe");
    const std::string out = g.run();
    CHECK(out.find("kfc=built kfc 1\n") != std::string::npos);
    CHECK(out.find("status=built\t1\t") != std::string::npos);
    CHECK(file_text(g.shadow + L"\\built.txt").find("seq=1 chain_loaded=no core_loaded=no") == 0);
}

TEST_CASE("e2e content: an entry another tool patched is not held: content off, chain and core as today, a warning") {
    ContentGame g(L"emf_e2e_jump", true, L"standin_game_jump.exe");
    const std::string out = g.run();
    CHECK(out.find("kfc=KFC4") != std::string::npos);
    CHECK(out.find("loaded chain_standin.dll=yes\n") != std::string::npos);
    CHECK(out.find("loaded emf_core.dll=yes\n") != std::string::npos);
    CHECK(out.find("status=failed\t") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(g.shadow + L"\\built.txt"));
    const std::string log = g.loader_log();
    INFO(log);
    CHECK(log.find("EML content: the game's entry point is not the usual start-up code: another tool patched it") !=
          std::string::npos);
    CHECK(log.find("EML content: off for this start") != std::string::npos);
}
