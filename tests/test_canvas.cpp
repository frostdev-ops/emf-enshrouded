// Markers step 3 (API v35): canvases, drawing panels mods own: the store (ids per mod, checked numbers, caps), the
// drawing lists (bad commands left out, long lists cut, strings copied) and the screen layout from a corner (game-free).
#include <windows.h>

#include <doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "core/api.h"
#include "core/canvas.h"

using namespace emf::core;

namespace {

EmfCanvas spec(float x, float y, float w, float h, uint8_t corner = EMF_CANVAS_TOP_RIGHT, uint8_t shape = EMF_CANVAS_CIRCLE) {
    EmfCanvas c{};
    c.size = sizeof c;
    c.corner = corner;
    c.shape = shape;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    return c;
}

EmfCanvasCmd circle(float cx, float cy, float r) {
    EmfCanvasCmd c{};
    c.kind = EMF_CANVAS_CIRCLE_CMD;
    c.rgba = 0xffffffffu;
    c.a[0] = cx;
    c.a[1] = cy;
    c.a[2] = r;
    return c;
}

}  // namespace

TEST_CASE("canvas: set, change, remove; a bad id or bad numbers are refused; another mod's canvas is out of reach") {
    CanvasStore s;
    EmfCanvas c = spec(16, 16, 220, 220);
    CHECK(s.set("minimap", "map", &c) == CanvasStore::SetResult::Ok);
    c.w = c.h = 300;
    CHECK(s.set("minimap", "map", &c) == CanvasStore::SetResult::Replaced);
    CHECK(s.set("minimap", "has space", &c) == CanvasStore::SetResult::BadId);
    EmfCanvas bad = c;
    bad.w = std::nanf("");
    CHECK(s.set("minimap", "b", &bad) == CanvasStore::SetResult::BadNumbers);
    bad = c;
    bad.w = 2000;  // more than a screen
    CHECK(s.set("minimap", "b", &bad) == CanvasStore::SetResult::BadNumbers);
    bad = c;
    bad.corner = 7;
    CHECK(s.set("minimap", "b", &bad) == CanvasStore::SetResult::BadNumbers);
    bad = c;
    bad.size = 8;  // an older or broken struct
    CHECK(s.set("minimap", "b", &bad) == CanvasStore::SetResult::BadNumbers);
    CHECK(s.set("other", "map", &c) == CanvasStore::SetResult::Taken);  // the id is minimap's
    CHECK(s.set("other", "map", nullptr) == CanvasStore::SetResult::NotFound);
    REQUIRE(s.view()->size() == 1u);
    CHECK(s.view()->at(0).spec.w == 300);
    CHECK(s.set("minimap", "map", nullptr) == CanvasStore::SetResult::Removed);
    CHECK(s.view()->empty());
}

TEST_CASE("canvas: 32 at most; remove_owner takes a mod's canvases away") {
    CanvasStore s;
    EmfCanvas c = spec(0, 0, 100, 100);
    for (int i = 0; i < 32; ++i) REQUIRE(s.set(i < 20 ? "a" : "b", ("c" + std::to_string(i)).c_str(), &c) != CanvasStore::SetResult::Full);
    CHECK(s.set("a", "one_more", &c) == CanvasStore::SetResult::Full);
    CHECK(s.remove_owner("a") == 20u);
    CHECK(s.view()->size() == 12u);
}

TEST_CASE("canvas: a drawing replaces the last; bad commands are left out; more than 2048 are cut; strings are copied") {
    CanvasStore s;
    EmfCanvas c = spec(0, 0, 200, 200);
    CHECK_FALSE(s.draw("m", "map", nullptr, 0, nullptr));  // no such canvas
    REQUIRE(s.set("m", "map", &c) == CanvasStore::SetResult::Ok);
    std::vector<EmfCanvasCmd> cmds{circle(100, 100, 90)};
    EmfCanvasCmd nan = circle(std::nanf(""), 1, 1);
    EmfCanvasCmd unknown = circle(1, 1, 1);
    unknown.kind = 42;
    EmfCanvasCmd text{};
    text.kind = EMF_CANVAS_TEXT;
    text.a[2] = 18;  // no s: left out
    std::string label = "N";
    EmfCanvasCmd ntext = text;
    ntext.s = label.c_str();
    EmfCanvasCmd fog{};
    fog.kind = EMF_CANVAS_FOG;
    fog.a[0] = 1500;
    fog.a[1] = 4740;
    fog.a[2] = 0;  // metres per unit must be > 0
    cmds.push_back(nan);
    cmds.push_back(unknown);
    cmds.push_back(text);
    cmds.push_back(ntext);
    cmds.push_back(fog);
    uint32_t kept = 0;
    REQUIRE(s.draw("m", "map", cmds.data(), static_cast<uint32_t>(cmds.size()), &kept));
    CHECK(kept == 2u);
    label = "changed";  // the store has its own copy
    auto v = s.view();
    REQUIRE(v->at(0).cmds.size() == 2u);
    CHECK(v->at(0).cmds[1].s == "N");
    std::vector<EmfCanvasCmd> many(3000, circle(1, 1, 1));
    REQUIRE(s.draw("m", "map", many.data(), 3000, &kept));
    CHECK(kept == 2048u);
    CHECK(s.view()->at(0).cmds.size() == 2048u);
    CHECK_FALSE(s.draw("other", "map", many.data(), 1, &kept));  // not other's canvas
}

TEST_CASE("canvas layout: from each corner, in 1080-pixel units scaled to the display") {
    EmfCanvas c = spec(16, 32, 200, 100, EMF_CANVAS_TOP_LEFT);
    float r[4];
    canvas_layout(c, 1920, 1080, r);
    CHECK(r[0] == doctest::Approx(16));
    CHECK(r[1] == doctest::Approx(32));
    CHECK(r[2] == doctest::Approx(200));
    CHECK(r[3] == doctest::Approx(100));
    c.corner = EMF_CANVAS_TOP_RIGHT;
    canvas_layout(c, 1920, 1080, r);
    CHECK(r[0] == doctest::Approx(1920 - 16 - 200));
    c.corner = EMF_CANVAS_BOTTOM_RIGHT;
    canvas_layout(c, 3840, 2160, r);  // twice the size
    CHECK(r[0] == doctest::Approx(3840 - 32 - 400));
    CHECK(r[1] == doctest::Approx(2160 - 64 - 200));
    CHECK(r[2] == doctest::Approx(400));
    c.corner = EMF_CANVAS_BOTTOM_LEFT;
    canvas_layout(c, 1920, 1080, r);
    CHECK(r[0] == doctest::Approx(16));
    CHECK(r[1] == doctest::Approx(1080 - 32 - 100));
}

TEST_CASE("api canvas_set / canvas_draw: the calling mod's canvas in the core's store") {
    CoreState state;
    set_core_state(&state);
    EmfCanvas c = spec(16, 16, 220, 220);
    CHECK(api_table()->canvas_set("map", &c));
    CHECK_FALSE(api_table()->canvas_set("bad id", &c));
    EmfCanvasCmd cmd = circle(110, 110, 100);
    CHECK(api_table()->canvas_draw("map", &cmd, 1));
    CHECK_FALSE(api_table()->canvas_draw("nope", &cmd, 1));
    REQUIRE(state.minimap.canvases.view()->size() == 1u);
    CHECK(state.minimap.canvases.view()->at(0).cmds.size() == 1u);
    CHECK(api_table()->canvas_set("map", nullptr));
    CHECK(state.minimap.canvases.view()->empty());
    set_core_state(nullptr);
}
