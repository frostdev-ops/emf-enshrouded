// Pictures in mod windows (API v26): the frames of a picture, their grid in one texture, which frame shows when, the
// limits, one handle per path, the upload states (pending, uploaded, forgotten on a backend restart).
#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/overlay/image_store.h"
#include "png_test_util.h"

using namespace emf::core;

namespace {

std::vector<Rgba> frames_of(uint32_t n, uint32_t w, uint32_t h) {
    std::vector<Rgba> v(n);
    for (uint32_t i = 0; i < n; ++i) {
        v[i].w = w;
        v[i].h = h;
        v[i].px.assign(static_cast<size_t>(w) * h * 4, static_cast<uint8_t>(i));
    }
    return v;
}

struct TempDir {
    std::filesystem::path p;
    TempDir() {
        p = std::filesystem::temp_directory_path() / ("emf_images_" + std::to_string(GetCurrentProcessId()));
        std::filesystem::remove_all(p);
        std::filesystem::create_directories(p);
    }
    ~TempDir() { std::filesystem::remove_all(p); }
};

}  // namespace

TEST_CASE("image store: frames are packed in a near-square grid, and each frame has its own UVs") {
    Grid g = pack(1, 192, 192);
    CHECK(g.cols == 1);
    CHECK(g.rows == 1);
    g = pack(2, 192, 100);
    CHECK(g.cols == 2);
    CHECK(g.rows == 1);
    g = pack(24, 192, 192);
    CHECK(g.cols == 5);
    CHECK(g.rows == 5);
    g = pack(64, 512, 512);
    CHECK(g.cols == 8);
    CHECK(g.rows == 8);
    const Grid two = pack(2, 10, 20);
    const UvRect a = frame_uv(two, 0), b = frame_uv(two, 1);
    CHECK(a.u0 == doctest::Approx(0.0f));
    CHECK(a.u1 == doctest::Approx(0.5f));
    CHECK(b.u0 == doctest::Approx(0.5f));
    CHECK(b.u1 == doctest::Approx(1.0f));
    CHECK(b.v0 == doctest::Approx(0.0f));
    CHECK(b.v1 == doctest::Approx(1.0f));
    const Grid five = pack(24, 4, 4);  // 5 x 5: frame 23 is row 4, column 3
    const UvRect last = frame_uv(five, 23);
    CHECK(last.u0 == doctest::Approx(0.6f));
    CHECK(last.v0 == doctest::Approx(0.8f));
}

TEST_CASE("image store: the frame at a time stays in range for any time and fps; fps 0 shows the first") {
    CHECK(frame_at(5.0, 0, 24) == 0);
    CHECK(frame_at(0.25, 10, 24) == 2);
    CHECK(frame_at(2.5, 10, 24) == 1);  // frame 25 wraps to 1
    CHECK(frame_at(1e9, 30, 24) < 24);
    CHECK(frame_at(-3.0, 10, 24) == 0);
    CHECK(frame_at(1.0, 10, 1) == 0);
    CHECK(frame_at(1.0, 10, 0) == 0);
}

TEST_CASE("image store: a picture's frames must share one size and stay inside the limits") {
    ImageStore s;
    std::string why;
    auto mixed = frames_of(2, 8, 8);
    mixed[1].w = 9;
    mixed[1].px.resize(9 * 8 * 4);
    CHECK(s.add("mixed", mixed, &why) == 0);
    CHECK(why.find("size") != std::string::npos);
    CHECK(s.add("big", frames_of(1, kMaxImageSide + 1, 8), &why) == 0);
    CHECK(s.add("many", frames_of(kMaxImageFrames + 1, 2, 2), &why) == 0);
    CHECK(s.add("none", {}, &why) == 0);
}

TEST_CASE("image store: the pixels of every picture stay within the budget (64 MB in the game)") {
    ImageStore s(64 * 1024);  // a small budget for the test
    std::string why;
    CHECK(s.add("fits", frames_of(4, 64, 64), &why) != 0);  // 4 x 64 x 64 x 4 bytes: the whole budget
    CHECK(s.add("over the budget", frames_of(1, 8, 8), &why) == 0);
    CHECK(why.find("budget") != std::string::npos);
}

TEST_CASE("image store: one handle per key; at most kMaxImages pictures") {
    ImageStore s;
    std::string why;
    const uint32_t a = s.add("a", frames_of(1, 2, 2), &why);
    REQUIRE(a != 0);
    CHECK(s.add("a", frames_of(3, 4, 4), &why) == a);  // the key's picture stays as it was loaded
    CHECK(s.find_key("a") == a);
    CHECK(s.find_key("b") == 0);
    for (uint32_t i = 1; i < kMaxImages; ++i) REQUIRE(s.add("p" + std::to_string(i), frames_of(1, 1, 1), &why) != 0);
    CHECK(s.add("one too many", frames_of(1, 1, 1), &why) == 0);
}

TEST_CASE("image store: pictures wait for the upload; a backend restart puts every one back to waiting") {
    ImageStore s;
    std::string why;
    const uint32_t a = s.add("a", frames_of(2, 2, 2), &why);
    const uint32_t b = s.add("b", frames_of(1, 3, 3), &why);
    CHECK(s.pending() == std::vector<uint32_t>{a, b});
    CHECK_FALSE(s.frame(a, 0.0, 0).has_value());  // not on the GPU yet: nothing to draw
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    REQUIRE(s.pixels(a, &w, &h, &px));
    CHECK(w == 4);  // two 2x2 frames side by side
    CHECK(h == 2);
    CHECK(px[0] == 0);           // frame 0's first pixel
    CHECK(px[2 * 4] == 1);       // frame 1 starts at x = 2
    s.set_texture(a, 0xABCD);
    CHECK(s.pending() == std::vector<uint32_t>{b});
    const auto f = s.frame(a, 0.15, 10);  // frame 1
    REQUIRE(f.has_value());
    CHECK(f->texture == 0xABCD);
    CHECK(f->uv.u0 == doctest::Approx(0.5f));
    CHECK(f->aspect == doctest::Approx(1.0f));
    s.forget_textures();
    CHECK(s.pending() == std::vector<uint32_t>{a, b});
    CHECK_FALSE(s.frame(a, 0.0, 0).has_value());
    CHECK_FALSE(s.frame(0, 0.0, 0).has_value());
    CHECK_FALSE(s.frame(999, 0.0, 0).has_value());
}

TEST_CASE("image store: a picture's files: a PNG, or a folder's PNGs sorted by name; refusals say why") {
    TempDir d;
    std::string why;
    std::filesystem::create_directories(d.p / "turn");
    REQUIRE(emf::test::write_png(d.p / "turn" / "b.png", 2, 2, emf::test::solid(2, 2, 0, 255, 0)));
    REQUIRE(emf::test::write_png(d.p / "turn" / "A.png", 2, 2, emf::test::solid(2, 2, 255, 0, 0)));
    std::ofstream(d.p / "turn" / "notes.txt") << "not a frame";
    auto files = frame_files((d.p / "turn").wstring(), &why);
    REQUIRE(files.size() == 2);
    CHECK(std::filesystem::path(files[0]).filename() == L"A.png");
    CHECK(std::filesystem::path(files[1]).filename() == L"b.png");
    CHECK(frame_files((d.p / "turn" / "b.png").wstring(), &why).size() == 1);
    CHECK(frame_files((d.p / "turn" / "notes.txt").wstring(), &why).empty());
    CHECK(frame_files((d.p / "missing").wstring(), &why).empty());
    std::filesystem::create_directories(d.p / "empty");
    CHECK(frame_files((d.p / "empty").wstring(), &why).empty());
    CHECK_FALSE(why.empty());

    ImageStore s;
    const uint32_t h = load_image(s, (d.p / "turn").wstring(), "turn", &why);
    REQUIRE(h != 0);
    uint32_t w = 0, hh = 0;
    std::vector<uint8_t> px;
    REQUIRE(s.pixels(h, &w, &hh, &px));
    CHECK(w == 4);
    CHECK(px[0] == 255);      // A.png (red) is frame 0
    CHECK(px[2 * 4 + 1] == 255);  // b.png (green) is frame 1
    CHECK(load_image(s, (d.p / "empty").wstring(), "empty", &why) == 0);
    CHECK(load_image(s, (d.p / "turn").wstring(), "turn", &why) == h);  // the same key: the same handle, no new decode
}

TEST_CASE("image store: pixels for an HDR10 swapchain are PQ-encoded BT.2020 at a 203-nit white; scRGB is linear") {
    std::vector<uint8_t> px = {255, 255, 255, 255,  // white
                               0,   0,   0,   128,  // black, half transparent
                               255, 0,   0,   255,  // red
                               188, 188, 188, 255}; // mid grey (sRGB 188 = linear 0.5)
    std::vector<uint8_t> hdr = px;
    convert_for_swapchain(hdr, PictureSpace::Hdr10Pq);
    CHECK(hdr[0] == hdr[1]);  // white stays neutral
    CHECK(hdr[0] >= 147);      // PQ(203 nits) = 0.584
    CHECK(hdr[0] <= 151);
    CHECK(hdr[4] == 0);
    CHECK(hdr[7] == 128);      // alpha unchanged
    CHECK(hdr[8] > hdr[9]);    // red stays red: BT.709 red in BT.2020 is mostly R, a little G and B
    CHECK(hdr[9] > 0);
    CHECK(hdr[12] < hdr[0]);   // grey darker than white
    CHECK(hdr[12] > 100);      // PQ is far from linear: half the light is not half the code
    std::vector<uint8_t> lin = px;
    convert_for_swapchain(lin, PictureSpace::LinearScrgb);
    CHECK(lin[0] == 255);
    CHECK(lin[12] >= 126);     // sRGB 188 -> linear 0.5
    CHECK(lin[12] <= 129);
    CHECK(lin[7] == 128);
    std::vector<uint8_t> same = px;
    convert_for_swapchain(same, PictureSpace::Srgb);
    CHECK(same == px);
}
