// Picking a HUD part with the mouse and dragging it: the pure parts (screen rectangles, which part is under the pointer,
// what a drag adds in percent).
#include <doctest.h>

#include "core/overlay/hud_pick.h"

using namespace emf::core;
using namespace emf::core::hud;

TEST_CASE("to_screen: the UI space's units become display pixels") {
    const float rect[4] = {100, 50, 200, 80};
    const ScreenRect r = to_screen(rect, UiSpace{2084.2f, 1080.0f, true}, ImVec2(3840, 1990));
    CHECK(r.x == doctest::Approx(100 * 1.8424f).epsilon(0.001));
    CHECK(r.w == doctest::Approx(200 * 1.8424f).epsilon(0.001));
    CHECK(r.y == doctest::Approx(50 * 1990.0f / 1080.0f).epsilon(0.001));
    CHECK(r.h == doctest::Approx(80 * 1990.0f / 1080.0f).epsilon(0.001));
    const ScreenRect same = to_screen(rect, UiSpace{1920, 1080, true}, ImVec2(1920, 1080));
    CHECK(same.x == 100.0f);
    CHECK(same.h == 80.0f);
}

TEST_CASE("pick_part: the smallest rectangle under the point wins, so a child beats its parent") {
    std::array<std::optional<ScreenRect>, kHudElementCount> rects{};
    rects[0] = ScreenRect{20, 20, 340, 190};  // vitals
    rects[1] = ScreenRect{30, 150, 200, 40};  // buffs, inside vitals
    CHECK(pick_part(rects, ImVec2(100, 170)) == 1);
    CHECK(pick_part(rects, ImVec2(100, 60)) == 0);
    CHECK(pick_part(rects, ImVec2(900, 900)) == -1);
    CHECK(pick_part(rects, ImVec2(20, 20)) == 0);  // the corner belongs to it
    std::array<std::optional<ScreenRect>, kHudElementCount> none{};
    CHECK(pick_part(none, ImVec2(1, 1)) == -1);
}

TEST_CASE("dragged: a drag adds its pixels in percent, clamped") {
    ElementLayout start;
    start.x = 1;
    start.y = -2;
    start.scale = 1.5f;
    const ElementLayout d = dragged(start, ImVec2(104, 0), ImVec2(2084, 1080));
    CHECK(d.x == doctest::Approx(1 + 4.99f));
    CHECK(d.y == doctest::Approx(-2.0f));
    CHECK(d.scale == 1.5f);
    const ElementLayout far = dragged(start, ImVec2(100000, -100000), ImVec2(2084, 1080));
    CHECK(far.x == 100.0f);
    CHECK(far.y == -100.0f);
}
