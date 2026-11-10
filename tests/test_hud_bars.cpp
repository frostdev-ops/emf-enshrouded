// The HUD editor's bar geometry: which bar a call is, the order's offsets from the bars' own rectangles, and the upward
// status list's offset kept on screen (docs/engine_parts/re_ui_scopes.md).
#include <doctest.h>

#include <array>

#include "core/hud_editor/bars.h"

using namespace emf::core::hud;

namespace {
const std::array<BarRect, 3> kGame = {BarRect{24.4f, 33.4f, 320, 18, true}, BarRect{24.4f, 57.4f, 260, 12, true},
                                      BarRect{24.4f, 75.4f, 200, 12, true}};
}  // namespace

TEST_CASE("bar_index: the first call sets the base, then the stride") {
    const uintptr_t w = 0x10000, base = w;  // health's data is w + 0x18
    CHECK(bar_index(w + 0x18, base) == 0);
    CHECK(bar_index(w + 0xf0, base) == 1);
    CHECK(bar_index(w + 0x1c8, base) == 2);
    CHECK(bar_index(w + 0x2a0, base) == -1);  // a fourth bar (a game update): untouched (Review Focus 2)
    CHECK(bar_index(w + 0x20, base) == -1);
    CHECK(bar_index(w, base) == -1);
    CHECK(bar_index(w - 0xc0, base) == -1);
}

TEST_CASE("order_dy: the default order changes nothing") {
    const auto d = order_dy(kGame, {0, 1, 2}, {false, false, false});
    CHECK(d[0] == doctest::Approx(0));
    CHECK(d[1] == doctest::Approx(0));
    CHECK(d[2] == doctest::Approx(0));
}

TEST_CASE("order_dy: mana first, then health, then stamina, with the game's gap") {
    const auto d = order_dy(kGame, {1, 0, 2}, {false, false, false});
    CHECK(d[1] == doctest::Approx(33.4f - 57.4f));          // mana to the top
    CHECK(d[0] == doctest::Approx(33.4f + 12 + 6 - 33.4f));  // health below it
    CHECK(d[2] == doctest::Approx(0));                       // stamina where it was (33.4+12+6+18+6 = 75.4)
}

TEST_CASE("order_dy: stamina not seen (full) leaves no hole and gets no offset") {  // Review Focus 1
    auto g = kGame;
    g[2].seen = false;
    const auto d = order_dy(g, {2, 1, 0}, {false, false, false});
    CHECK(d[2] == doctest::Approx(0));
    // with only health and mana seen, the order stamina, mana, health is mana, health:
    CHECK(d[1] == doctest::Approx(33.4f - 57.4f));
    CHECK(d[0] == doctest::Approx(12 + 6));
}

TEST_CASE("order_dy: a hidden bar leaves no hole") {
    const auto d = order_dy(kGame, {0, 1, 2}, {false, true, false});
    CHECK(d[1] == doctest::Approx(0));
    CHECK(d[2] == doctest::Approx(33.4f + 18 + 6 - 75.4f));  // stamina moves up into mana's place
}

TEST_CASE("order_dy: one bar seen, or none") {
    std::array<BarRect, 3> g{};
    auto d = order_dy(g, {2, 1, 0}, {false, false, false});
    CHECK(d == std::array<float, 3>{0, 0, 0});
    g[0] = kGame[0];
    d = order_dy(g, {2, 1, 0}, {false, false, false});
    CHECK(d == std::array<float, 3>{0, 0, 0});
}

TEST_CASE("order_dy: the gap is the game's own (measured between the first two seen bars)") {
    auto g = kGame;
    g[1].y = 33.4f + 18 + 10;  // a 10-unit gap
    g[2].y = g[1].y + 12 + 10;
    const auto d = order_dy(g, {1, 0, 2}, {false, false, false});
    CHECK(d[0] == doctest::Approx(12 + 10));
}

TEST_CASE("grow_up_dy: the list sits on the column's bottom, grows up, stops at the screen's edges") {  // Review Focus 5
    const Xform id{1, 0, 0};
    CHECK(grow_up_dy(87.8f, 21.6f, 120, id, 1080) == doctest::Approx(98.4f));
    CHECK(grow_up_dy(87.8f, 200, 120, id, 1080) == doctest::Approx(-80));     // taller than the column: above it
    CHECK(grow_up_dy(87.8f, 300, 120, id, 1080) == doctest::Approx(-87.8f));  // would leave the screen: stops at 0
    CHECK(grow_up_dy(1000, 21.6f, 120, id, 1080) == doctest::Approx(1080 - 1000 - 21.6f));  // the bottom edge
    CHECK(grow_up_dy(10, 2000, 120, id, 1080) == doctest::Approx(-10));       // taller than the screen: top visible
    const Xform scaled{2, 100, 0};  // the group scaled and moved: screen = 2*p + t
    CHECK(grow_up_dy(87.8f, 300, 120, scaled, 1080) == doctest::Approx(-87.8f));
    const Xform lifted{1, 0, -50};  // the part moved up by 50 units
    CHECK(grow_up_dy(87.8f, 100, 120, lifted, 1080) == doctest::Approx(20));         // room: not clamped
    CHECK(grow_up_dy(87.8f, 200, 120, lifted, 1080) == doctest::Approx(50 - 87.8f));  // the top edge, 50 units lower
}

TEST_CASE("grow_up_dy: an unusable top or sizes give 0") {
    CHECK(grow_up_dy(87.8f, 21.6f, 120, Xform{0, 0, 0}, 1080) == doctest::Approx(0));
    CHECK(grow_up_dy(87.8f, -5, 120, Xform{1, 0, 0}, 1080) == doctest::Approx(0));
    CHECK(grow_up_dy(87.8f, 21.6f, 120, Xform{1, 0, 0}, 0) == doctest::Approx(0));
}
