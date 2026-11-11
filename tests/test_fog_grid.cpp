// Markers step 3 (API v35): the fog of war: the game's grid (32x32 blocks of 8 m pixels, row 0 north) checked and
// sampled, kept as a copy twice a second, and a smaller picture of it for the overlay (game-free).
#include <doctest.h>

#include <cmath>
#include <vector>

#include "core/api.h"
#include "core/fog_grid.h"
#include "core/marker_capture.h"

using namespace emf::core;

namespace {

// A 512 m world: 64 x 64 pixels, 2 x 2 blocks.
struct Grid {
    std::vector<uint8_t> data = std::vector<uint8_t>(4 * 1024, 0);
    FogHeader h() {
        FogHeader g;
        g.w = g.h = 512;
        g.cols = g.rows = 2;
        g.data = data.data();
        g.blocks = 4;
        return g;
    }
    void set(uint32_t px, uint32_t py, uint8_t v) {
        data[((py / 32) * 2 + px / 32) * 1024 + (py % 32) * 32 + px % 32] = v;
    }
};

}  // namespace

TEST_CASE("fog grid: the header must agree with itself (size, block counts, data)") {
    Grid g;
    CHECK(fog_header_ok(g.h()));
    FogHeader bad = g.h();
    bad.blocks = 3;
    CHECK_FALSE(fog_header_ok(bad));
    bad = g.h();
    bad.cols = 3;
    CHECK_FALSE(fog_header_ok(bad));
    bad = g.h();
    bad.w = std::nanf("");
    CHECK_FALSE(fog_header_ok(bad));
    bad = g.h();
    bad.data = nullptr;
    CHECK_FALSE(fog_header_ok(bad));
    bad = g.h();
    bad.w = bad.h = 200000;  // a world this big is not the game's
    CHECK_FALSE(fog_header_ok(bad));
}

TEST_CASE("fog grid: a pixel by block, row 0 is the north edge; outside the world is unexplored") {
    Grid g;
    g.set(37, 3, 255);  // block (1, 0), near the north edge
    const FogHeader h = g.h();
    CHECK(fog_pixel(h, 37, 3) == 255);
    CHECK(fog_pixel(h, 3, 37) == 0);
    CHECK(fog_explored(h, (37 + 0.5) * 8, 512 - (3 + 0.5) * 8) == doctest::Approx(1.0f));  // north: z near 512
    CHECK(fog_explored(h, (37 + 0.5) * 8, (3 + 0.5) * 8) == doctest::Approx(0.0f));
    CHECK(fog_explored(h, -10, 100) == 0.0f);
    CHECK(fog_explored(h, 100, 9999) == 0.0f);
}

TEST_CASE("fog store: copies the grid at most twice a second; answers from its copy; a bad grid clears it") {
    Grid g;
    FogStore s;
    float e = -1;
    CHECK_FALSE(s.explored(100, 100, &e));
    g.set(12, 50, 128);
    CHECK(s.update(g.h(), 1000));
    REQUIRE(s.explored((12 + 0.5) * 8, 512 - (50 + 0.5) * 8, &e));
    CHECK(e == doctest::Approx(128.0f / 255.0f));
    CHECK(s.world_size() == doctest::Approx(512));
    const uint32_t v = s.version();
    CHECK_FALSE(s.due(1200));
    CHECK(s.due(1600));
    g.set(12, 50, 255);
    CHECK_FALSE(s.update(g.h(), 1200));  // too soon: the old copy
    s.explored((12 + 0.5) * 8, 512 - (50 + 0.5) * 8, &e);
    CHECK(e == doctest::Approx(128.0f / 255.0f));
    CHECK(s.update(g.h(), 1600));
    CHECK(s.version() != v);
    CHECK_FALSE(s.update(g.h(), 2200));  // nothing changed: no new version
    FogHeader bad = g.h();
    bad.blocks = 1;
    s.update(bad, 3000);
    CHECK_FALSE(s.explored(100, 100, &e));
}

TEST_CASE("fog store: a picture at a quarter of the pixels (each picture pixel the mean of 2 x 2)") {
    Grid g;
    g.set(0, 0, 255);
    g.set(1, 0, 255);
    g.set(0, 1, 255);
    g.set(1, 1, 255);
    g.set(2, 0, 128);
    FogStore s;
    s.update(g.h(), 1000);
    std::vector<uint8_t> pic;
    uint32_t side = 0;
    REQUIRE(s.picture(&pic, &side));
    CHECK(side == 32u);
    CHECK(pic.size() == 32u * 32u);
    CHECK(pic[0] == 255);
    CHECK(pic[1] == 32);  // (128 + 0 + 0 + 0) / 4
    CHECK(pic[32] == 0);
}

TEST_CASE("api fog_at: answers from the core's copy; false before one; player_view's world size from it") {
    CoreState state;
    set_core_state(&state);
    float e = -1;
    CHECK_FALSE(api_table()->fog_at(100, 100, &e));
    Grid g;
    g.set(12, 50, 255);
    state.minimap.fog.update(g.h(), 1000);
    REQUIRE(api_table()->fog_at((12 + 0.5) * 8, 512 - (50 + 0.5) * 8, &e));
    CHECK(e == doctest::Approx(1.0f));
    CHECK_FALSE(api_table()->fog_at(100, 100, nullptr));
    marker_capture_reset_for_tests();
    marker_capture_set_player(true, 10, 20, 30);
    EmfPlayerView v{};
    v.size = sizeof v;
    REQUIRE(api_table()->player_view(&v));
    CHECK(v.world_size == doctest::Approx(512));
    marker_capture_reset_for_tests();
    set_core_state(nullptr);
}
