// Markers step 2 (API v34): the map screen's mouse in world coordinates and the Shift + hold gesture (game-free).
#include <doctest.h>

#include "core/map_click.h"

using namespace emf::core;

namespace {

MapView view() {
    MapView v{};
    v.rect[0] = -1000;  // the whole 10 240 m map is 10 240 UI units: 1 unit per metre
    v.rect[1] = -5000;
    v.rect[2] = 10240;
    v.rect[3] = 10240;
    v.clip[0] = 100;
    v.clip[1] = 50;
    v.clip[2] = 1800;
    v.clip[3] = 1000;
    v.ui_w = 1920;
    v.ui_h = 1080;
    v.ms = 1000;
    return v;
}

}  // namespace

TEST_CASE("map mouse: the inverse of the map markers' mapping; nothing outside the map's frame") {
    double x = 0, z = 0;
    REQUIRE(map_world_at(view(), 500, 500, &x, &z));
    CHECK(x == doctest::Approx(1500));
    CHECK(z == doctest::Approx(4740));
    CHECK_FALSE(map_world_at(view(), 50, 500, &x, &z));   // left of the frame
    CHECK_FALSE(map_world_at(view(), 500, 1200, &x, &z));  // below it
    MapView empty{};
    CHECK_FALSE(map_world_at(empty, 500, 500, &x, &z));
    // outside the world (beyond 0..10240) is not a spot either
    MapView far = view();
    far.rect[0] = 1000;  // the map's west edge is right of the frame's left
    CHECK_FALSE(map_world_at(far, 500, 500, &x, &z));
}

TEST_CASE("map hold: Shift + down starts it, held 1 s it is done, and the up after is the hold's (swallowed)") {
    MapHold h;
    auto o = h.down(true, true, 100, 200, 1000);
    CHECK(o.swallow);
    CHECK(o.started);
    CHECK(h.holding());
    CHECK(h.progress(1500) == doctest::Approx(0.5f));
    o = h.tick(1999);
    CHECK_FALSE(o.done);
    o = h.tick(2000);
    CHECK(o.done);
    CHECK(o.x == 100);
    CHECK(o.z == 200);
    CHECK_FALSE(h.holding());
    o = h.up(2100);
    CHECK(o.swallow);  // the game never saw the down
    CHECK_FALSE(o.done);
}

TEST_CASE("map hold: released early, right-click, Esc or the map closing cancel it; the up is swallowed") {
    MapHold h;
    h.down(true, true, 1, 2, 1000);
    auto o = h.up(1500);
    CHECK(o.swallow);
    CHECK(o.cancelled);
    CHECK_FALSE(o.done);
    for (int how = 0; how < 3; ++how) {
        MapHold k;
        k.down(true, true, 1, 2, 1000);
        o = how == 0 ? k.right() : how == 1 ? k.esc() : k.closed();
        CHECK(o.cancelled);
        CHECK(o.swallow == (how != 2));  // right-click and Esc are the hold's; the map closing is not an input
        o = k.tick(3000);
        CHECK_FALSE(o.done);
        o = k.up(3100);
        CHECK(o.swallow);  // still the hold's button
    }
}

TEST_CASE("map hold: without Shift, off the map, or switched off, the click is the game's") {
    MapHold h;
    auto o = h.down(false, true, 1, 2, 1000);
    CHECK_FALSE(o.swallow);
    CHECK_FALSE(h.holding());
    o = h.up(1100);
    CHECK_FALSE(o.swallow);
    o = h.down(true, false, 1, 2, 1200);  // Shift, but not on the map
    CHECK_FALSE(o.swallow);
    h.set_enabled(false);  // no map action announced
    o = h.down(true, true, 1, 2, 1300);
    CHECK_FALSE(o.swallow);
}

TEST_CASE("map hold: the wheel while holding is swallowed; a quick plain click after a swallowed one becomes a plain down") {
    MapHold h;
    h.down(true, true, 1, 2, 1000);
    CHECK(h.wheel().swallow);
    h.up(1100);  // cancelled early
    CHECK_FALSE(h.wheel().swallow);
    // Windows sends a double-click for the second press: the game never saw the first, so it gets a down
    auto o = h.dblclk(false, true, 1, 2, 1300);
    CHECK_FALSE(o.swallow);
    CHECK(o.rewrite_down);
    // a Shift double-click starts a hold as a down would
    MapHold k;
    o = k.dblclk(true, true, 5, 6, 2000);
    CHECK(o.swallow);
    CHECK(o.started);
}

#include "core/api_sizes.h"

TEST_CASE("map api: v34 layouts") {
    CHECK(EMF_API_VERSION == 34);
    CHECK(sizeof(EmfMapClick) == 32);
    CHECK(offsetof(EmfMapClick, x) == 16);
    CHECK(kApiV34Size == kApiV33Size + 4 * 8);
}
