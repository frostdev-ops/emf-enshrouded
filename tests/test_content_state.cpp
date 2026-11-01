// What the content loader remembers between starts: the fingerprint of everything a build depends on
// (src/loader/content_fingerprint.h) and the state file next to the shadow copy (src/loader/content_state.h). A start
// whose fingerprint equals the state's, with a build that worked, uses the shadow without loading emf_content.dll.
#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "content_fixtures.h"
#include "loader/content_fingerprint.h"
#include "loader/content_state.h"
#include "test_scratch.h"

using content_fixtures::file_text;
using content_fixtures::kfc_bytes;
using content_fixtures::put_file;
using emf::loader::content_fingerprint;
using emf::loader::FingerprintInput;

namespace {
emf::loader::FileFact fact(const std::string& name, uint64_t size, uint64_t written) {
    emf::loader::FileFact f;
    f.name = name;
    f.size = size;
    f.written = written;
    f.exists = true;
    return f;
}

FingerprintInput sample() {
    FingerprintInput in;
    in.base = fact("C:\\Games\\Enshrouded\\enshrouded.kfc", 9000, 111);
    in.game_kfc = fact("enshrouded.kfc", 9000, 111);
    in.resources = fact("enshrouded.kfc_resources", 214000, 222);
    in.exe = fact("enshrouded.exe", 50000, 333);
    in.dll = fact("emf_content.dll", 20000, 444);
    in.dat_count = 55;
    in.game_dats = 55;
    in.emf_version = "0.4.0";
    emf::loader::ModFiles a;
    a.id = "beasts";
    a.version = "1.0.0";
    a.files = {fact("mod.json", 100, 1), fact("patch\\bear.lua", 2000, 2)};
    emf::loader::ModFiles b;
    b.id = "necro";
    b.version = "2.0.0";
    b.files = {fact("mod.json", 90, 3)};
    in.mods = {a, b};
    return in;
}
}  // namespace

TEST_CASE("fingerprint: stable for the same input") {
    CHECK(content_fingerprint(sample()) == content_fingerprint(sample()));
    CHECK(content_fingerprint(sample()).rfind("sha256:", 0) == 0);
    CHECK(content_fingerprint(sample()).size() == 7 + 64);
}

TEST_CASE("fingerprint: changes when a mod file changes, a mod is added, the dll changes, the game's kfc changes") {
    const std::string fp = content_fingerprint(sample());
    auto in = sample();
    in.mods[0].files[1].written = 5;  // a pack file touched
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.mods[0].files[1].size = 2001;
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.mods[0].files.push_back(fact("patch\\new.lua", 1, 1));
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.mods.push_back(in.mods[1]);
    in.mods.back().id = "third";
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.mods[1].version = "2.0.1";
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.dll.written = 445;  // a new emf_content.dll
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.game_kfc.written = 112;  // the game's own .kfc (patched again, or a Steam update)
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.base.name = "C:\\Games\\Enshrouded\\enshrouded.kfc.bak";
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.exe.size = 50001;
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.resources.written = 223;
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.game_dats = 56;
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.dat_count = 54;
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.emf_version = "0.4.1";
    CHECK(content_fingerprint(in) != fp);
    in = sample();
    in.mods[0].files[0].name = "Mod.json2";
    CHECK(content_fingerprint(in) != fp);
}

TEST_CASE("fingerprint: mod order does not matter") {
    auto in = sample();
    std::swap(in.mods[0], in.mods[1]);
    std::swap(in.mods[1].files[0], in.mods[1].files[1]);
    CHECK(content_fingerprint(in) == content_fingerprint(sample()));
}

TEST_CASE("fingerprint: collected from the game folder, the mods and the dll") {
    const std::wstring game = test_scratch::temp_path(L"emf_content_fp");
    test_scratch::remove_at_exit(game);
    put_file(game + L"\\enshrouded.exe", "exe");
    put_file(game + L"\\enshrouded.kfc", kfc_bytes("1|a|b", 2));
    put_file(game + L"\\enshrouded.kfc_resources", "resources");
    put_file(game + L"\\enshrouded_000.dat", "0");
    put_file(game + L"\\enshrouded_001.dat", "1");
    put_file(game + L"\\emf\\emf_content.dll", "dll");
    put_file(game + L"\\mods\\beasts\\mod.json", R"({"id": "beasts", "name": "B", "version": "1.0.0"})");
    put_file(game + L"\\mods\\beasts\\sub\\bear.lua", "bear");
    emf::loader::BaseChoice base;
    base.ok = true;
    base.dat_count = 2;
    emf::loader::EmlMod m;
    m.folder = L"beasts";
    m.id = "beasts";
    m.version = "1.0.0";
    const std::wstring dll = game + L"\\emf\\emf_content.dll";
    auto in = emf::loader::collect_fingerprint_input(game, "enshrouded", base, {m}, dll, "0.4.0");
    CHECK(in.base.exists);
    CHECK(in.base.name.find("enshrouded.kfc") != std::string::npos);
    CHECK(in.exe.size == 3);
    CHECK(in.dll.size == 3);
    CHECK(in.game_dats == 2);
    REQUIRE(in.mods.size() == 1);
    REQUIRE(in.mods[0].files.size() == 2);
    CHECK(in.mods[0].files[0].name == "mod.json");
    CHECK(in.mods[0].files[1].name == "sub\\bear.lua");
    const std::string fp = content_fingerprint(in);
    CHECK(content_fingerprint(emf::loader::collect_fingerprint_input(game, "enshrouded", base, {m}, dll, "0.4.0")) == fp);
    put_file(game + L"\\mods\\beasts\\sub\\bear.lua", "a bigger bear");
    CHECK(content_fingerprint(emf::loader::collect_fingerprint_input(game, "enshrouded", base, {m}, dll, "0.4.0")) != fp);
    base.use_bak = true;  // the base is the .bak now
    in = emf::loader::collect_fingerprint_input(game, "enshrouded", base, {m}, dll, "0.4.0");
    CHECK(in.base.name.find("enshrouded.kfc.bak") != std::string::npos);
    CHECK_FALSE(in.base.exists);
    std::filesystem::remove_all(game);
}

namespace {
emf::loader::ContentState sample_state() {
    emf::loader::ContentState s;
    s.fingerprint = "sha256:abc";
    s.code = 0;
    s.message = "built 3 mods \"fine\" in C:\\Games\\B\xC3\xA4r";
    s.mods = {"beasts 1.0.0", "necro 2.0.0", "packs 0.1.0"};
    s.errors = {"C:\\x\\mods\\broken: no mod.json"};
    s.ms = 28000;
    s.built_at = "2026-11-01T20:00:00Z";
    s.chain_checked = {"creative.dll 1000 132000000000000000"};  // chained DLLs read at this build: not EML's
    return s;
}
}  // namespace

TEST_CASE("content state: round trip") {
    const std::wstring dir = test_scratch::temp_path(L"emf_content_state");
    test_scratch::remove_at_exit(dir);
    std::filesystem::create_directories(dir);
    const std::wstring path = dir + L"\\emf_content_state.json";
    REQUIRE(emf::loader::write_state(path, sample_state()));
    const auto back = emf::loader::read_state(path);
    REQUIRE(back.has_value());
    CHECK(back->fingerprint == "sha256:abc");
    CHECK(back->code == 0);
    CHECK(back->message == sample_state().message);
    CHECK(back->mods == sample_state().mods);
    CHECK(back->errors == sample_state().errors);
    CHECK(back->ms == 28000);
    CHECK(back->built_at == "2026-11-01T20:00:00Z");
    CHECK(back->chain_checked == sample_state().chain_checked);
    CHECK(back->unloaded);
    CHECK_FALSE(back->truncated);
    CHECK(emf::loader::state_current(back, "sha256:abc"));
    CHECK_FALSE(emf::loader::state_current(back, "sha256:abd"));
    // written as <name>.tmp and renamed: no .tmp stays, and a second write replaces the first
    auto again = sample_state();
    again.code = 2;
    REQUIRE(emf::loader::write_state(path, again));
    CHECK(emf::loader::read_state(path)->code == 2);
    CHECK_FALSE(std::filesystem::exists(path + L".tmp"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("content state: code 2 is not current") {
    auto s = sample_state();
    s.code = 2;
    CHECK_FALSE(emf::loader::state_current(s, "sha256:abc"));
    s = sample_state();
    s.truncated = true;  // the reply was cut: its result is unknown
    CHECK_FALSE(emf::loader::state_current(s, "sha256:abc"));
    s = sample_state();
    s.unloaded = false;
    CHECK_FALSE(emf::loader::state_current(s, "sha256:abc"));
    CHECK_FALSE(emf::loader::state_current(std::nullopt, "sha256:abc"));
    CHECK(emf::loader::state_current(sample_state(), "sha256:abc"));
}

TEST_CASE("content state: broken file is not current") {
    const std::wstring dir = test_scratch::temp_path(L"emf_content_state_broken");
    test_scratch::remove_at_exit(dir);
    put_file(dir + L"\\emf_content_state.json", "{\"fingerprint\": \"sha256:abc\", \"code\": 0");  // cut off by a crash
    CHECK_FALSE(emf::loader::read_state(dir + L"\\emf_content_state.json").has_value());
    put_file(dir + L"\\emf_content_state.json", "[1, 2]");
    CHECK_FALSE(emf::loader::read_state(dir + L"\\emf_content_state.json").has_value());
    put_file(dir + L"\\emf_content_state.json", "{\"code\": 0}");  // no fingerprint
    CHECK_FALSE(emf::loader::state_current(emf::loader::read_state(dir + L"\\emf_content_state.json"), ""));
    CHECK_FALSE(emf::loader::read_state(dir + L"\\missing.json").has_value());
    std::filesystem::remove_all(dir);
}
