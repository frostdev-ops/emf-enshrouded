// Pictures in mod windows (API v26): PNG files decoded with Windows Imaging Component into straight-alpha RGBA8.
#include <doctest.h>

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "core/overlay/png_decode.h"
#include "png_test_util.h"

using namespace emf::core;

namespace {

std::filesystem::path temp_dir() {
    auto d = std::filesystem::temp_directory_path() / ("emf_png_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(d);
    return d;
}

}  // namespace

TEST_CASE("png decode: a PNG comes back as its RGBA pixels, rows top to bottom, straight alpha") {
    const auto file = temp_dir() / "three_by_two.png";
    const std::vector<uint8_t> px = {
        255, 0,   0,   255, 0,   255, 0,   255, 0,   0,   255, 255,  // red, green, blue
        10,  20,  30,  128, 200, 100, 50,  0,   255, 255, 255, 255,  // half-transparent, fully transparent, white
    };
    REQUIRE(emf::test::write_png(file, 3, 2, px));
    Rgba out;
    std::string why;
    REQUIRE(decode_png(file.wstring(), &out, &why));
    CHECK(out.w == 3);
    CHECK(out.h == 2);
    REQUIRE(out.px.size() == px.size());
    CHECK(std::vector<uint8_t>(out.px.begin(), out.px.begin() + 12) ==
          std::vector<uint8_t>(px.begin(), px.begin() + 12));
    CHECK(out.px[12 + 3] == 128);  // the half-transparent pixel keeps its colour (not premultiplied)
    CHECK(out.px[12 + 0] == 10);
    std::filesystem::remove_all(temp_dir());
}

TEST_CASE("png decode: a file that is not a PNG, and a missing file, fail with a reason") {
    const auto file = temp_dir() / "not_a_picture.png";
    std::ofstream(file) << "hello, not a png";
    Rgba out;
    std::string why;
    CHECK_FALSE(decode_png(file.wstring(), &out, &why));
    CHECK_FALSE(why.empty());
    why.clear();
    CHECK_FALSE(decode_png((temp_dir() / "missing.png").wstring(), &out, &why));
    CHECK_FALSE(why.empty());
    std::filesystem::remove_all(temp_dir());
}

TEST_CASE("png decode: a picture larger than the caller's limit is refused before its pixels are allocated") {
    const auto file = temp_dir() / "three_by_two.png";
    REQUIRE(emf::test::write_png(file, 3, 2, emf::test::solid(3, 2, 1, 2, 3)));
    Rgba out;
    std::string why;
    CHECK_FALSE(decode_png(file.wstring(), &out, &why, 2));
    CHECK(why.find("large") != std::string::npos);
    CHECK(out.px.empty());
    CHECK(decode_png(file.wstring(), &out, &why, 3));
    std::filesystem::remove_all(temp_dir());
}
