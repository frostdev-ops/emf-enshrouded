// The HUD layout files in <game>\emf\hud: one per name's slug, written whole, read back sorted, broken ones listed.
#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/hud_editor/layout_store.h"
#include "test_scratch.h"

using namespace emf::core::hud;

namespace {

// A folder of this run's own, removed at the end of the test.
struct Folder {
    std::wstring dir;
    explicit Folder(const std::wstring& stem) {
        static int n = 0;
        dir = test_scratch::temp_path(L"emf_hud_store_" + stem + L"_" + std::to_wstring(++n));
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    ~Folder() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void put(const std::wstring& name, const std::string& text) const {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream f(std::filesystem::path(dir) / name, std::ios::binary);
        f << text;
    }
    bool has(const std::wstring& name) const { return std::filesystem::exists(std::filesystem::path(dir) / name); }
};

HudLayout named(const std::string& name) {
    HudLayout l;
    l.name = name;
    return l;
}

}  // namespace

TEST_CASE("layout files: written whole under the name's slug, read back sorted") {
    Folder f(L"a");
    HudLayout building = named("Building 2");
    building.elements[0] = {5, -2.5f, 1.25f, false};
    building.elements[3].hidden = true;
    std::string err;
    REQUIRE(write_layout_file(f.dir, named("Default"), 1287369, &err));
    REQUIRE(write_layout_file(f.dir, building, 1287369, &err));
    CHECK(f.has(L"building-2.json"));
    CHECK(f.has(L"default.json"));
    CHECK_FALSE(f.has(L"building-2.json.tmp"));
    const auto files = read_layout_dir(f.dir);
    REQUIRE(files.size() == 2);
    CHECK(files[0].slug == "building-2");
    CHECK(files[1].slug == "default");
    REQUIRE(files[0].layout.has_value());
    REQUIRE(files[1].layout.has_value());
    CHECK(files[0].layout->name == "Building 2");
    CHECK(files[1].layout->name == "Default");
    for (size_t i = 0; i < kHudElementCount; ++i) CHECK(files[0].layout->elements[i] == building.elements[i]);
    CHECK(files[0].notes.empty());
}

TEST_CASE("layout files: a missing folder is an empty list; non-.json files and .tmp leftovers are not layouts") {
    Folder f(L"b");
    CHECK(read_layout_dir(f.dir).empty());
    f.put(L"notes.txt", "hello");
    f.put(L"default.json.tmp", "{");
    f.put(L"other.jsonx", "{");
    CHECK(read_layout_dir(f.dir).empty());
    f.put(L"UPPER.JSON", "{\"format\":1}");
    const auto files = read_layout_dir(f.dir);
    REQUIRE(files.size() == 1);
    CHECK(files[0].slug == "upper");
}

TEST_CASE("layout files: a broken file is listed with its error, the others still load") {
    Folder f(L"c");
    f.put(L"bad.json", "{");
    std::string err;
    REQUIRE(write_layout_file(f.dir, named("Default"), 1287369, &err));
    const auto files = read_layout_dir(f.dir);
    REQUIRE(files.size() == 2);
    CHECK(files[0].slug == "bad");
    CHECK_FALSE(files[0].layout.has_value());
    CHECK(files[0].error.rfind("not JSON", 0) == 0);
    CHECK(files[1].layout.has_value());
}

TEST_CASE("layout files: a name that does not match the file shows as the file's slug") {
    Folder f(L"d");
    f.put(L"foo.json", "{\"format\":1,\"name\":\"Bar\"}");
    f.put(L"baz.json", "{\"format\":1}");
    const auto files = read_layout_dir(f.dir);
    REQUIRE(files.size() == 2);
    REQUIRE(files[1].layout.has_value());
    CHECK(files[1].layout->name == "foo");
    REQUIRE(files[1].notes.size() == 1);
    CHECK(files[1].notes[0] == "name \"Bar\" does not match the file name; shown as \"foo\"");
    REQUIRE(files[0].layout.has_value());
    CHECK(files[0].layout->name == "baz");
    CHECK(files[0].notes.empty());
}

TEST_CASE("layout files: delete removes only a slug's file; a missing file is fine; a path is refused") {
    Folder f(L"e");
    std::string err;
    REQUIRE(write_layout_file(f.dir, named("Default"), 1287369, &err));
    REQUIRE(write_layout_file(f.dir, named("Keep"), 1287369, &err));
    CHECK(delete_layout_file(f.dir, "default", &err));
    CHECK_FALSE(f.has(L"default.json"));
    CHECK(f.has(L"keep.json"));
    CHECK(delete_layout_file(f.dir, "default", &err));
    CHECK_FALSE(delete_layout_file(f.dir, "..\\x", &err));
    CHECK(err == "not a layout's file name");
}
