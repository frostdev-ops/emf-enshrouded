// The content loader (docs/engine_parts/content_loader.md): the parts in src/loader/ that decide whether EMF builds the
// EML mods' content into a shadow copy of the game's data, build it, and remember what was built.
#include <doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "content_fixtures.h"
#include "loader/content_base.h"
#include "loader/eml_mods.h"
#include "loader/kfc_header.h"
#include "test_scratch.h"

using content_fixtures::file_text;
using content_fixtures::kfc_bytes;
using content_fixtures::put_file;

TEST_CASE("kfc header: synthetic header") {
    const auto h = emf::loader::parse_kfc_header(kfc_bytes("1|x|y", 3));
    REQUIRE(h.has_value());
    CHECK(h->version == "1|x|y");
    CHECK(h->containers == 3);
}

TEST_CASE("kfc header: wrong magic") {
    std::string b = kfc_bytes("1|x|y", 3);
    b[3] = '5';  // "KFC5"
    CHECK_FALSE(emf::loader::parse_kfc_header(b).has_value());
}

TEST_CASE("kfc header: version past the end") {
    std::string b = kfc_bytes("1|x|y", 3);
    content_fixtures::put_u32(b, 20, 100);  // a 100-byte version in 64 bytes
    CHECK_FALSE(emf::loader::parse_kfc_header(b).has_value());
    b = kfc_bytes("1|x|y", 3);
    content_fixtures::put_u32(b, 16, 0xFFFFFFF0u);  // an offset that wraps around
    CHECK_FALSE(emf::loader::parse_kfc_header(b).has_value());
}

TEST_CASE("kfc header: truncated") {
    CHECK_FALSE(emf::loader::parse_kfc_header(kfc_bytes("1|x|y", 3).substr(0, 20)).has_value());
    CHECK_FALSE(emf::loader::parse_kfc_header("").has_value());
}
// ---- Task 2: the base the shadow is built from, and the EML mods in <game>\mods ----

namespace {
emf::loader::BaseFile base_file(const std::string& version, uint32_t containers, uint64_t size) {
    emf::loader::BaseFile f;
    f.header = emf::loader::parse_kfc_header(kfc_bytes(version, containers));
    f.size = size;
    f.exists = true;
    return f;
}
}  // namespace

TEST_CASE("content base: no bak") {
    const auto c = emf::loader::choose_base(base_file("7|r|t", 55, 9000), {});
    CHECK(c.ok);
    CHECK_FALSE(c.use_bak);
    CHECK(c.dat_count == 55);
}

TEST_CASE("content base: stale bak (other version)") {
    // an update since EML's patch: the .bak is of an older build
    const auto c = emf::loader::choose_base(base_file("8|r|t", 56, 9100), base_file("7|r|t", 64, 12000));
    CHECK(c.ok);
    CHECK_FALSE(c.use_bak);
    CHECK(c.dat_count == 56);
}

TEST_CASE("content base: identical bak") {
    const auto c = emf::loader::choose_base(base_file("7|r|t", 55, 9000), base_file("7|r|t", 55, 9000));
    CHECK(c.ok);
    CHECK_FALSE(c.use_bak);
    CHECK(c.dat_count == 55);
}

TEST_CASE("content base: patched kfc") {
    // EML or ShroudForge patched the game's files in place: never patch on top of that
    auto c = emf::loader::choose_base(base_file("7|r|t", 64, 12000), base_file("7|r|t", 55, 9000));
    CHECK(c.ok);
    CHECK(c.use_bak);
    CHECK(c.dat_count == 55);
    c = emf::loader::choose_base(base_file("7|r|t", 55, 9500), base_file("7|r|t", 55, 9000));  // same count, other size
    CHECK(c.ok);
    CHECK(c.use_bak);
    CHECK(c.dat_count == 55);
    CHECK(c.why.find("enshrouded.kfc.bak") != std::string::npos);
}

TEST_CASE("content base: unreadable header") {
    emf::loader::BaseFile broken;
    broken.exists = true;
    broken.size = 10;
    auto c = emf::loader::choose_base(broken, {});
    CHECK_FALSE(c.ok);
    CHECK(c.why.find("enshrouded.kfc") != std::string::npos);
    c = emf::loader::choose_base(base_file("7|r|t", 55, 9000), broken);
    CHECK_FALSE(c.ok);
    CHECK(c.why.find("enshrouded.kfc.bak") != std::string::npos);
    c = emf::loader::choose_base({}, {});  // no .kfc at all
    CHECK_FALSE(c.ok);
    CHECK(c.why.find("enshrouded.kfc") != std::string::npos);
    c = emf::loader::choose_base(broken, {}, "enshrouded_server");
    CHECK(c.why.find("enshrouded_server.kfc") != std::string::npos);
}

TEST_CASE("content base: read from the files") {
    const std::wstring dir = test_scratch::temp_path(L"emf_content_base");
    test_scratch::remove_at_exit(dir);
    std::filesystem::create_directories(dir);
    put_file(dir + L"\\enshrouded.kfc", kfc_bytes("7|r|t", 3) + std::string(100, 'x'));
    const auto f = emf::loader::read_base_file(dir + L"\\enshrouded.kfc");
    CHECK(f.exists);
    CHECK(f.size == 164);
    REQUIRE(f.header.has_value());
    CHECK(f.header->containers == 3);
    const auto none = emf::loader::read_base_file(dir + L"\\enshrouded.kfc.bak");
    CHECK_FALSE(none.exists);
    CHECK_FALSE(none.header.has_value());
    std::filesystem::remove_all(dir);
}

namespace {
const char* kGoodMod = R"({"id": "good", "name": "Good", "version": "1.0.0", "capabilities": ["patch", "export"]})";

std::string reason_for(const emf::loader::ModScan& s, const std::wstring& folder) {
    for (const auto& [f, why] : s.skipped)
        if (f == folder) return why;
    return "(not skipped)";
}
}  // namespace

TEST_CASE("eml mods: usable, hidden, runtime, broken json, no id") {
    const std::wstring mods = test_scratch::temp_path(L"emf_eml_mods");
    test_scratch::remove_at_exit(mods);
    put_file(mods + L"\\good\\mod.json", kGoodMod);
    put_file(mods + L"\\good\\patch.lua", "-- patch\n");
    put_file(mods + L"\\.hidden\\mod.json", kGoodMod);
    put_file(mods + L"\\runtime\\mod.json",
             R"({"id": "rt", "name": "Rt", "version": "1.0.0", "capabilities": ["patch", "runtime"]})");
    put_file(mods + L"\\broken\\mod.json", "{ not json");
    put_file(mods + L"\\noid\\mod.json", R"({"name": "No id", "version": "1.0.0"})");
    put_file(mods + L"\\readme.txt", "not a mod\n");  // a file: passed over without a word

    const auto s = emf::loader::scan_eml_mods(mods);
    REQUIRE(s.usable.size() == 1);
    CHECK(s.usable[0].folder == L"good");
    CHECK(s.usable[0].id == "good");
    CHECK(s.usable[0].version == "1.0.0");
    CHECK(s.usable[0].capabilities == std::vector<std::string>{"patch", "export"});
    CHECK(s.skipped.size() == 4);
    CHECK(reason_for(s, L".hidden").find("starts with a dot") != std::string::npos);
    CHECK(reason_for(s, L"runtime") == "needs EML's runtime loader, which EMF does not run");
    CHECK(reason_for(s, L"broken").find("mod.json") != std::string::npos);
    CHECK(reason_for(s, L"noid").find("id") != std::string::npos);
    std::filesystem::remove_all(mods);
}

TEST_CASE("eml mods: no mod.json, no name, a zip, the same id twice, no folder") {
    const std::wstring mods = test_scratch::temp_path(L"emf_eml_mods_more");
    test_scratch::remove_at_exit(mods);
    CHECK(emf::loader::scan_eml_mods(mods).usable.empty());  // no mods folder at all
    put_file(mods + L"\\a_first\\mod.json", kGoodMod);
    put_file(mods + L"\\b_again\\mod.json", kGoodMod);
    put_file(mods + L"\\empty\\notes.txt", "x");
    put_file(mods + L"\\noname\\mod.json", R"({"id": "nn", "version": "1.0.0"})");
    put_file(mods + L"\\caps\\mod.json", R"({"id": "c", "name": "C", "version": "1.0.0", "capabilities": "patch"})");
    put_file(mods + L"\\packed.zip", "PK");
    const auto s = emf::loader::scan_eml_mods(mods);
    REQUIRE(s.usable.size() == 1);
    CHECK(s.usable[0].folder == L"a_first");
    CHECK(reason_for(s, L"b_again").find("a_first") != std::string::npos);
    CHECK(reason_for(s, L"empty").find("no mod.json") != std::string::npos);
    CHECK(reason_for(s, L"noname").find("name") != std::string::npos);
    CHECK(reason_for(s, L"caps").find("capabilities") != std::string::npos);
    CHECK(reason_for(s, L"packed.zip").find(".zip") != std::string::npos);
    std::filesystem::remove_all(mods);
}

TEST_CASE("eml mods: non-ASCII folder name") {
    const std::wstring mods = test_scratch::temp_path(L"emf_eml_mods_utf");
    test_scratch::remove_at_exit(mods);
    const std::wstring folder = L"Bär mod ✓";
    put_file(mods + L"\\" + folder + L"\\mod.json",
             "{\"id\": \"baer\", \"name\": \"B\xC3\xA4r\", \"version\": \"2.1.0\", \"capabilities\": [\"patch\"]}");
    const auto s = emf::loader::scan_eml_mods(mods);
    REQUIRE(s.usable.size() == 1);
    CHECK(s.usable[0].folder == folder);
    CHECK(s.usable[0].id == "baer");
    std::filesystem::remove_all(mods);
}
