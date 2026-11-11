// Markers step 2 (API v34): the window messages of the map gesture (the glue between the window procedure and MapHold;
// fake messages, no game).
#include <windows.h>

#include <doctest.h>

#include <vector>

#include "core/map_input.h"

using namespace emf::core;

namespace {

MapInputEnv env(bool open = true, bool announced = true, bool shift = true) {
    MapInputEnv e;
    e.map_open = open;
    e.announced = announced;
    e.shift = shift;
    e.client_w = 1920;
    e.client_h = 1080;
    e.view.rect[0] = -1000;
    e.view.rect[1] = -5000;
    e.view.rect[2] = 10240;
    e.view.rect[3] = 10240;
    e.view.clip[0] = 100;  // the map's frame (MapCenteringFrame)
    e.view.clip[1] = 50;
    e.view.clip[2] = 1800;
    e.view.clip[3] = 1000;
    e.view.ui_w = 1920;
    e.view.ui_h = 1080;
    e.now_ms = 1000;
    return e;
}

LPARAM at(int x, int y) { return MAKELPARAM(x, y); }

}  // namespace

TEST_CASE("map input: Shift + hold on the open map with an action announced: swallowed, done after 1 s with the spot") {
    std::vector<std::pair<double, double>> done;
    MapInput in([&](double x, double z) { done.emplace_back(x, z); });
    MapInputEnv e = env();
    auto r = in.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, at(500, 500), e);
    CHECK(r.swallow);
    CHECK(in.ring(1500).holding);
    CHECK(in.ring(1500).progress == doctest::Approx(0.5f));
    e.now_ms = 2000;
    in.frame(e);
    REQUIRE(done.size() == 1);
    CHECK(done[0].first == doctest::Approx(1500));
    CHECK(done[0].second == doctest::Approx(4740));
    r = in.message(WM_LBUTTONUP, 0, at(500, 500), e);
    CHECK(r.swallow);
}

TEST_CASE("map input: the game gets the click without Shift, with the map closed, or with no action announced") {
    int done = 0;
    MapInput in([&](double, double) { ++done; });
    CHECK_FALSE(in.message(WM_LBUTTONDOWN, MK_LBUTTON, at(500, 500), env(true, true, false)).swallow);
    CHECK_FALSE(in.message(WM_LBUTTONUP, 0, at(500, 500), env(true, true, false)).swallow);
    CHECK_FALSE(in.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, at(500, 500), env(false)).swallow);
    CHECK_FALSE(in.message(WM_LBUTTONUP, 0, at(500, 500), env(false)).swallow);
    CHECK_FALSE(in.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, at(500, 500), env(true, false)).swallow);
    CHECK(done == 0);
}

TEST_CASE("map input: the map closing cancels; right-click and the wheel while holding are swallowed; nothing is sent") {
    int done = 0;
    MapInput in([&](double, double) { ++done; });
    MapInputEnv e = env();
    in.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, at(500, 500), e);
    CHECK(in.message(WM_RBUTTONDOWN, MK_RBUTTON, at(500, 500), e).swallow);
    MapInput w([&](double, double) { ++done; });
    w.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, at(500, 500), e);
    CHECK(w.message(WM_MOUSEWHEEL, 0, 0, e).swallow);
    MapInputEnv closed = e;
    closed.map_open = false;
    closed.now_ms = 3000;
    w.frame(closed);
    CHECK_FALSE(w.ring(3000).holding);
    CHECK(w.message(WM_LBUTTONUP, 0, at(500, 500), closed).swallow);  // the hold's button
    CHECK(done == 0);
}

TEST_CASE("map input: a quick plain click after a swallowed one: its double-click message becomes a press for the game") {
    MapInput in([](double, double) {});
    MapInputEnv e = env();
    in.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, at(500, 500), e);
    in.message(WM_LBUTTONUP, 0, at(500, 500), e);  // released early
    const auto r = in.message(WM_LBUTTONDBLCLK, MK_LBUTTON, at(500, 500), env(true, true, false));
    CHECK_FALSE(r.swallow);
    CHECK(r.rewrite_down);
}

TEST_CASE("map input: clicks the game gets on the open map are told to on_map_click with the spot and Shift/Ctrl") {
    MapInput in([](double, double) {});
    std::vector<EmfMapClick> clicks;
    in.set_click_listener([&](const EmfMapClick& c) { clicks.push_back(c); });
    in.message(WM_RBUTTONDOWN, MK_RBUTTON | MK_CONTROL, at(500, 500), env(true, false, false));
    REQUIRE(clicks.size() == 1);
    CHECK(clicks[0].button == 1u);
    CHECK(clicks[0].mods == EMF_MAP_CTRL);
    CHECK(clicks[0].x == doctest::Approx(1500));
    in.message(WM_LBUTTONDOWN, MK_LBUTTON, at(5, 5), env(true, false, false));  // off the map: nothing
    CHECK(clicks.size() == 1);
}

TEST_CASE("map input: the hold time the server announced: not done at 1 s with 1.5 s set, done at 1.5 s") {
    int done = 0;
    MapInput in([&](double, double) { ++done; });
    in.set_hold_ms(1500);
    MapInputEnv e = env();
    in.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, at(500, 500), e);
    e.now_ms = 2000;
    in.frame(e);
    CHECK(done == 0);
    CHECK(in.ring(2000).progress == doctest::Approx(1.0f / 1.5f));
    e.now_ms = 2500;
    in.frame(e);
    CHECK(done == 1);
    in.set_hold_ms(0);  // 0: the default 1 s
    e.now_ms = 3000;
    in.message(WM_LBUTTONUP, 0, at(500, 500), e);
    in.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, at(500, 500), e);
    e.now_ms = 4000;
    in.frame(e);
    CHECK(done == 2);
}
