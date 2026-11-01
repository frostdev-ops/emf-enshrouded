// The shadow copy the EML content is built into (src/loader/content_shadow.h), on fake game folders: hard links to the
// game's exe and containers, private copies of the index files, real folders with copies of the mods, and the record
// the redirect checks. The game's own files are never written.
#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "common/sha256.h"
#include "content_fixtures.h"
#include "loader/content_build.h"
#include "loader/content_shadow.h"
#include "loader/file_hooks.h"
#include "test_scratch.h"

using content_fixtures::file_text;
using content_fixtures::kfc_bytes;
using content_fixtures::put_file;

namespace {
struct FileId {
    DWORD volume = 0, high = 0, low = 0, links = 0;
    bool ok = false;
    bool same(const FileId& o) const { return ok && o.ok && volume == o.volume && high == o.high && low == o.low; }
};

FileId file_id(const std::wstring& path) {
    FileId id;
    const HANDLE f = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (f == INVALID_HANDLE_VALUE) return id;
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(f, &info)) {
        id = {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow, info.nNumberOfLinks, true};
    }
    CloseHandle(f);
    return id;
}

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

const std::wstring kUtfMod = L"B\u00e4r \u2713";

// A fake game folder: enshrouded.exe, a KFC4 .kfc with `dats` containers, .kfc_resources, the containers, two mods.
struct FakeGame {
    std::wstring dir, shadow;
    explicit FakeGame(const wchar_t* name, uint32_t dats = 3) : dir(test_scratch::temp_path(name)) {
        test_scratch::remove_at_exit(dir);
        shadow = dir + L"\\emf\\content\\client";
        put_file(dir + L"\\enshrouded.exe", "the game's exe");
        put_file(dir + L"\\enshrouded.kfc", kfc_bytes("1|g|t", dats));
        put_file(dir + L"\\enshrouded.kfc_resources", "the game's resources");
        for (uint32_t i = 0; i < dats; ++i) put_file(dat(i), "the game's dat " + std::to_string(i));
        put_file(dir + L"\\mods\\beasts\\mod.json", R"({"id": "beasts", "name": "Beasts", "version": "1.0.0"})");
        put_file(dir + L"\\mods\\beasts\\sub\\bear.lua", "-- the bear");
        put_file(dir + L"\\mods\\" + kUtfMod + L"\\mod.json", R"({"id": "baer", "name": "B", "version": "0.2.0"})");
    }
    ~FakeGame() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::wstring dat(uint32_t i, bool in_shadow = false) const {
        wchar_t n[32];
        swprintf_s(n, L"\\enshrouded_%03u.dat", i);
        return (in_shadow ? shadow : dir) + n;
    }
    emf::loader::BaseChoice base() const {
        return emf::loader::choose_base(emf::loader::read_base_file(dir + L"\\enshrouded.kfc"),
                                        emf::loader::read_base_file(dir + L"\\enshrouded.kfc.bak"));
    }
    std::vector<emf::loader::EmlMod> mods() const { return emf::loader::scan_eml_mods(dir + L"\\mods").usable; }
    emf::loader::ShadowResult build(const emf::loader::ShadowIo& io = {}) const {
        return emf::loader::build_shadow(dir, shadow, "enshrouded", base(), mods(), io);
    }
    // name -> sha256 and stamp of every file in the game folder outside emf\ and mods\.
    std::map<std::wstring, std::string> game_files() const {
        std::map<std::wstring, std::string> out;
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            WIN32_FILE_ATTRIBUTE_DATA d{};
            GetFileAttributesExW(e.path().c_str(), GetFileExInfoStandard, &d);
            out[e.path().filename().wstring()] = emf::sha256_file(e.path().wstring()) + " " +
                                                 std::to_string(d.ftLastWriteTime.dwLowDateTime) + " " +
                                                 std::to_string(d.dwFileAttributes);
        }
        return out;
    }
};

bool no_tmp_in(const std::wstring& dir) {
    for (const auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == L".tmp") return false;
    return true;
}
}  // namespace

TEST_CASE("shadow: fresh build links, copies, and real mod folders") {
    FakeGame g(L"emf_shadow_fresh");
    const auto r = g.build();
    REQUIRE_MESSAGE(r.ok, r.why);
    // hard links: the game's own exe and containers under a second name
    CHECK(file_id(g.shadow + L"\\enshrouded.exe").same(file_id(g.dir + L"\\enshrouded.exe")));
    for (uint32_t i = 0; i < 3; ++i) {
        CHECK(file_id(g.dat(i, true)).same(file_id(g.dat(i))));
        CHECK(file_id(g.dat(i)).links == 2);
    }
    CHECK_FALSE(exists(g.dat(3, true)));
    // private copies of the index files (EML writes them in place): the same bytes, a file of their own
    for (const wchar_t* name : {L"\\enshrouded.kfc", L"\\enshrouded.kfc_resources"}) {
        CHECK(file_text(g.shadow + name) == file_text(g.dir + name));
        CHECK_FALSE(file_id(g.shadow + name).same(file_id(g.dir + name)));
        CHECK(file_id(g.shadow + name).links == 1);
    }
    // the mods: real folders (EML skips junctions) holding copies
    const DWORD a = GetFileAttributesW((g.shadow + L"\\mods\\beasts").c_str());
    CHECK((a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT)));
    CHECK(file_text(g.shadow + L"\\mods\\beasts\\sub\\bear.lua") == "-- the bear");
    CHECK(file_id(g.shadow + L"\\mods\\beasts\\mod.json").links == 1);
    CHECK(no_tmp_in(g.shadow));
    // the record of the game's files is taken before the copy, but written only after a good build (content_start.h)
    CHECK_FALSE(exists(g.shadow + L"\\shadow.txt"));
    CHECK(r.record.find("source enshrouded.exe ") != std::string::npos);
    CHECK(r.record.find("dat_count 3\r\n") != std::string::npos);
}

TEST_CASE("shadow: non-ASCII mod folder") {
    FakeGame g(L"emf_shadow_utf \u00e9");  // a game folder with a space and an accent too
    REQUIRE(g.build().ok);
    CHECK(file_text(g.shadow + L"\\mods\\" + kUtfMod + L"\\mod.json").find("\"baer\"") != std::string::npos);
}

TEST_CASE("shadow: rebuild keeps links and removes extra dat past the base") {
    FakeGame g(L"emf_shadow_rebuild");
    REQUIRE(g.build().ok);
    const FileId link0 = file_id(g.dat(0, true));
    // what EML wrote into the shadow at the last build, and leftovers of a mod that is gone
    put_file(g.dat(3, true), "EML's new container");
    put_file(g.dat(4, true), "an empty one");
    put_file(g.shadow + L"\\enshrouded.kfc", "patched by EML");
    put_file(g.shadow + L"\\enshrouded.kfc.bak", "EML's backup");
    put_file(g.shadow + L"\\.cache\\types.json", "{}");
    put_file(g.shadow + L"\\mods\\gone\\mod.json", "{}");
    // Steam replaced a container since (a new file under the old name)
    REQUIRE(DeleteFileW(g.dat(1).c_str()));
    put_file(g.dat(1), "the game's dat 1, updated");

    const auto r = g.build();
    REQUIRE_MESSAGE(r.ok, r.why);
    CHECK_FALSE(exists(g.dat(3, true)));
    CHECK_FALSE(exists(g.dat(4, true)));
    CHECK_FALSE(exists(g.shadow + L"\\enshrouded.kfc.bak"));
    CHECK_FALSE(exists(g.shadow + L"\\.cache"));
    CHECK_FALSE(exists(g.shadow + L"\\mods\\gone"));
    CHECK(file_text(g.shadow + L"\\enshrouded.kfc") == file_text(g.dir + L"\\enshrouded.kfc"));
    CHECK(file_id(g.dat(0, true)).same(link0));  // kept
    CHECK(file_id(g.dat(1, true)).same(file_id(g.dat(1))));  // linked again to the new file
    CHECK(file_text(g.dat(1, true)) == "the game's dat 1, updated");
    CHECK(file_text(g.dat(0)) == "the game's dat 0");
}

TEST_CASE("shadow: patched game uses the bak and links only its count") {
    FakeGame g(L"emf_shadow_patched", 5);
    // EML patched the game's own files: 5 containers now, the vanilla index (3 containers) kept as .kfc.bak
    put_file(g.dir + L"\\enshrouded.kfc.bak", kfc_bytes("1|g|t", 3));
    const auto base = g.base();
    REQUIRE(base.use_bak);
    REQUIRE(base.dat_count == 3);
    REQUIRE(g.build().ok);
    CHECK(file_text(g.shadow + L"\\enshrouded.kfc") == file_text(g.dir + L"\\enshrouded.kfc.bak"));
    CHECK(exists(g.dat(2, true)));
    CHECK_FALSE(exists(g.dat(3, true)));
    CHECK_FALSE(exists(g.dat(4, true)));
    CHECK_FALSE(exists(g.shadow + L"\\enshrouded.kfc.bak"));  // EML makes its own from the shadow's .kfc
}

TEST_CASE("shadow: tmp leftovers are cleared") {
    FakeGame g(L"emf_shadow_tmp");
    // a crash in the middle of an earlier build
    for (const wchar_t* name : {L"\\enshrouded.kfc.tmp", L"\\enshrouded.kfc_resources.tmp", L"\\shadow.txt.tmp",
                                L"\\emf_content_state.json.tmp"})
        put_file(g.shadow + name, "half");
    REQUIRE(g.build().ok);
    CHECK(no_tmp_in(g.shadow));
}

TEST_CASE("shadow: the game's files are unchanged") {
    FakeGame g(L"emf_shadow_untouched");
    const auto before = g.game_files();
    REQUIRE(g.build().ok);
    put_file(g.dat(3, true), "EML's new container");
    REQUIRE(g.build().ok);
    CHECK(g.game_files() == before);
    CHECK(file_text(g.dir + L"\\mods\\beasts\\sub\\bear.lua") == "-- the bear");
}

TEST_CASE("shadow: record accepted by the redirect's staleness check") {
    FakeGame g(L"emf_shadow_record");
    const auto r = g.build();
    REQUIRE(r.ok);
    put_file(g.shadow + L"\\shadow.txt", r.record);
    const auto game = emf::loader::normal_game_dir(g.dir);
    REQUIRE(game.has_value());
    CHECK(emf::loader::shadow_problem(emf::loader::file_hooks::shadow_facts(*game)) == "");
    // and a game update after the build turns it away
    put_file(g.dir + L"\\enshrouded.kfc", kfc_bytes("2|g|t", 3));
    CHECK(emf::loader::shadow_problem(emf::loader::file_hooks::shadow_facts(*game)).find("enshrouded.kfc") !=
          std::string::npos);
}

TEST_CASE("shadow: the stand-in DLL builds in it (no index file is a link, the mods are folders)") {
    FakeGame g(L"emf_shadow_standin");
    REQUIRE(g.build().ok);
    put_file(g.shadow + L"\\standin.txt", "ok");
    wchar_t buf[MAX_PATH];
    const std::wstring exe(buf, GetModuleFileNameW(nullptr, buf, MAX_PATH));
    const std::wstring dll = exe.substr(0, exe.find_last_of(L'\\')) + L"\\standin_content\\emf_content.dll";
    const auto out = emf::loader::run_content_build(dll, emf::loader::content_request(g.shadow, "enshrouded", false), nullptr);
    CHECK_MESSAGE(out.code == 0, out.message);
    CHECK(out.mods == std::vector<std::string>{"baer 0.2.0", "beasts 1.0.0"});
    CHECK(file_text(g.dir + L"\\enshrouded.kfc") == kfc_bytes("1|g|t", 3));  // the game's own stays vanilla
    CHECK(file_text(g.shadow + L"\\enshrouded.kfc") == "built kfc 1\r\n");
    CHECK(file_text(g.dat(3, true)) == "built dat 1\r\n");
    CHECK_FALSE(exists(g.dat(3)));
}

TEST_CASE("shadow: no hard links, a missing container, no base: content off with the reason") {
    FakeGame g(L"emf_shadow_nolinks");
    emf::loader::ShadowIo io;
    io.link = [](const std::wstring&, const std::wstring&, DWORD* error) {
        *error = ERROR_NOT_SAME_DEVICE;
        return false;
    };
    auto r = g.build(io);
    CHECK_FALSE(r.ok);
    CHECK(r.why.find("does not allow hard links") != std::string::npos);
    CHECK_FALSE(exists(g.shadow + L"\\shadow.txt"));

    REQUIRE(DeleteFileW(g.dat(2).c_str()));
    r = g.build();
    CHECK_FALSE(r.ok);
    CHECK(r.why.find("enshrouded_002.dat") != std::string::npos);

    emf::loader::BaseChoice none;
    none.why = "enshrouded.kfc's header could not be read";
    r = emf::loader::build_shadow(g.dir, g.shadow, "enshrouded", none, g.mods());
    CHECK_FALSE(r.ok);
}

TEST_CASE("shadow: refused when its folder is a junction (it would write somewhere else)") {
    FakeGame g(L"emf_shadow_junction");
    const std::wstring elsewhere = g.dir + L"\\elsewhere";
    std::filesystem::create_directories(elsewhere);
    std::filesystem::create_directories(g.dir + L"\\emf");
    std::wstring cmd = L"cmd.exe /c mklink /J \"" + g.dir + L"\\emf\\content\" \"" + elsewhere + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    REQUIRE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    REQUIRE((GetFileAttributesW((g.dir + L"\\emf\\content").c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
    const auto r = g.build();
    CHECK_FALSE(r.ok);
    CHECK(r.why.find("junction") != std::string::npos);
    CHECK(std::filesystem::is_empty(elsewhere));
    RemoveDirectoryW((g.dir + L"\\emf\\content").c_str());  // the junction, not what it points to
}

TEST_CASE("shadow: intact after a build; a container Steam replaced, a missing copy or a missing link is not") {
    FakeGame g(L"emf_shadow_intact");
    REQUIRE(g.build().ok);
    CHECK(emf::loader::shadow_intact(g.dir, g.shadow, "enshrouded", 3) == "");
    // Steam replaced a container without changing the .kfc: the shadow's link is the old file now
    REQUIRE(DeleteFileW(g.dat(1).c_str()));
    put_file(g.dat(1), "the game's dat 1, updated");
    CHECK(emf::loader::shadow_intact(g.dir, g.shadow, "enshrouded", 3).find("enshrouded_001.dat") != std::string::npos);
    REQUIRE(g.build().ok);
    CHECK(emf::loader::shadow_intact(g.dir, g.shadow, "enshrouded", 3) == "");
    REQUIRE(DeleteFileW((g.shadow + L"\\enshrouded.exe").c_str()));
    CHECK(emf::loader::shadow_intact(g.dir, g.shadow, "enshrouded", 3).find("enshrouded.exe") != std::string::npos);
    REQUIRE(g.build().ok);
    REQUIRE(DeleteFileW((g.shadow + L"\\enshrouded.kfc_resources").c_str()));
    CHECK(emf::loader::shadow_intact(g.dir, g.shadow, "enshrouded", 3).find("enshrouded.kfc_resources") !=
          std::string::npos);
}

TEST_CASE("shadow: EMF's own writes there need its folders to be real folders, not junctions") {
    FakeGame g(L"emf_shadow_safe");
    CHECK(emf::loader::shadow_path_safe(g.dir, g.shadow));  // not made yet: nothing in the way
    REQUIRE(g.build().ok);
    CHECK(emf::loader::shadow_path_safe(g.dir, g.shadow));
    const std::wstring elsewhere = g.dir + L"\\elsewhere";
    std::filesystem::create_directories(elsewhere);
    std::filesystem::remove_all(g.dir + L"\\emf\\content");
    std::wstring cmd = L"cmd.exe /c mklink /J \"" + g.dir + L"\\emf\\content\" \"" + elsewhere + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    REQUIRE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CHECK_FALSE(emf::loader::shadow_path_safe(g.dir, g.shadow));
    RemoveDirectoryW((g.dir + L"\\emf\\content").c_str());
}
