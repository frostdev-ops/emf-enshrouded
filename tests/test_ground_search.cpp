// Markers step 2 (API v34): finding the ground at a map spot from the game's voxel ray, tick by tick (game-free).
#include <doctest.h>

#include "core/ground_search.h"

using namespace emf::core;

namespace {

GroundLimits limits() {
    GroundLimits l;
    l.box_top = 1200;     // the scene's reset box: above it the game sends a player to their altar
    l.grid_top = 1100;    // the voxel grid's top and bottom
    l.grid_bottom = -100;
    return l;
}

RayResult hit(double y) {
    RayResult r;
    r.loaded = true;
    r.hit = true;
    r.y = y;
    return r;
}

RayResult not_loaded() { return RayResult{}; }

}  // namespace

TEST_CASE("ground: two ticks that agree within 0.5 m give the ground") {
    GroundSearch g(100, 200, limits(), 1000);
    CHECK(g.ray_start() == doctest::Approx(1100.25));
    auto o = g.on_ray(hit(300.0), 1050);
    CHECK(o.step == GroundSearch::Step::Wait);
    o = g.on_ray(hit(300.3), 1100);
    REQUIRE(o.step == GroundSearch::Step::Accept);
    CHECK(o.y == doctest::Approx(300.3));
}

TEST_CASE("ground: a jump between ticks, a hit at the ray's start or above the reset box is not accepted") {
    GroundSearch g(100, 200, limits(), 1000);
    g.on_ray(hit(300), 1050);
    CHECK(g.on_ray(hit(310), 1100).step == GroundSearch::Step::Wait);  // moved 10 m: again
    CHECK(g.on_ray(hit(310.2), 1150).step == GroundSearch::Step::Accept);
    GroundSearch s(100, 200, limits(), 1000);
    s.on_ray(hit(1099.8), 1050);  // within 1 m of the ray's start: the ray began inside something
    CHECK(s.on_ray(hit(1099.8), 1100).step == GroundSearch::Step::Wait);
    GroundLimits low = limits();
    low.box_top = 302;  // ground at 300 is less than 4 m under the box's top
    GroundSearch b(100, 200, low, 1000);
    b.on_ray(hit(300), 1050);
    CHECK(b.on_ray(hit(300), 1100).step == GroundSearch::Step::Wait);
}

TEST_CASE("ground: not loaded for 5 s -> one hop high over the spot; then the ground, or back after 2 s") {
    GroundSearch g(100, 200, limits(), 1000);
    CHECK(g.on_ray(not_loaded(), 3000).step == GroundSearch::Step::Wait);
    auto o = g.on_ray(not_loaded(), 6000);
    REQUIRE(o.step == GroundSearch::Step::Hop);
    CHECK(o.y == doctest::Approx(1100));  // min(box top - 8, grid top)
    CHECK(g.on_ray(not_loaded(), 6100).step == GroundSearch::Step::Wait);  // waits for the hop
    g.on_hopped(true, 6200);
    g.on_ray(hit(500), 6300);
    CHECK(g.on_ray(hit(500.1), 6350).step == GroundSearch::Step::Accept);
    GroundSearch back(100, 200, limits(), 1000);
    back.on_ray(not_loaded(), 6000);
    back.on_hopped(true, 6100);
    CHECK(back.on_ray(not_loaded(), 7000).step == GroundSearch::Step::Wait);
    CHECK(back.on_ray(not_loaded(), 8200).step == GroundSearch::Step::Back);
    GroundSearch fail(100, 200, limits(), 1000);
    fail.on_ray(not_loaded(), 6000);
    const auto f = fail.on_hopped(false, 6100);
    CHECK(f.step == GroundSearch::Step::Fail);
    CHECK_FALSE(f.why.empty());
}

TEST_CASE("ground: loaded without a hit (nothing under the spot) fails with a reason; spots outside the world are refused") {
    GroundSearch g(100, 200, limits(), 1000);
    RayResult miss;
    miss.loaded = true;
    const auto o = g.on_ray(miss, 1050);
    CHECK(o.step == GroundSearch::Step::Fail);
    CHECK_FALSE(o.why.empty());
    std::string why;
    CHECK(ground_spot_ok(100, 200, &why));
    CHECK_FALSE(ground_spot_ok(-1, 200, &why));
    CHECK_FALSE(ground_spot_ok(100, 10241, &why));
    CHECK_FALSE(why.empty());
}
