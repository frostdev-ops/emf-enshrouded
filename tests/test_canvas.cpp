// Markers step 3 (API v35): canvases, drawing panels mods own: the store (ids per mod, checked numbers, caps), the
// drawing lists (bad commands left out, long lists cut, strings copied) and the screen layout from a corner (game-free).
#include <windows.h>

#include <doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "core/api.h"
#include "core/canvas.h"
#include "core/fog_grid.h"
#include "core/minimap_draw.h"

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

TEST_CASE("canvas world map: the centre at the rect's centre, north up, metres per unit times the scale") {
    const float rect[4] = {100, 50, 440, 440};  // a 220-unit canvas at twice the size: 2 px per unit
    float px = 0, py = 0;
    canvas_world_to(rect, 220, 1500, 4740, 2.0, 1500, 4740, &px, &py);
    CHECK(px == doctest::Approx(320));
    CHECK(py == doctest::Approx(270));
    canvas_world_to(rect, 220, 1500, 4740, 2.0, 1520, 4760, &px, &py);  // 20 m east, 20 m north
    CHECK(px == doctest::Approx(340));  // 20 m / 2 m per unit = 10 units = 20 px
    CHECK(py == doctest::Approx(250));  // north is up
}

TEST_CASE("canvas fog cells: at most 72 across, cells of whole fog pixels, alpha where unexplored") {
    const float rect[4] = {0, 0, 220, 220};
    // explored east of x = 1500, unexplored west
    auto cells = fog_cells(rect, 220, 1500, 4740, 1.0, [](double x, double) { return x >= 1500 ? 1.0f : 0.0f; });
    REQUIRE_FALSE(cells.empty());
    CHECK(cells.size() <= 72u * 72u);
    bool west = false, east = false;
    for (const auto& c : cells) {
        CHECK(c.alpha >= 0.0f);
        CHECK(c.alpha <= 1.0f);
        if (c.x + c.w <= 100 && c.alpha == doctest::Approx(1.0f)) west = true;
        if (c.x >= 120) east = east || c.alpha > 0;
    }
    CHECK(west);
    CHECK_FALSE(east);  // explored cells are not drawn
    auto wide = fog_cells(rect, 220, 5000, 5000, 50.0, [](double, double) { return 0.0f; });  // 11 km across
    CHECK(wide.size() <= 72u * 72u);
}

TEST_CASE("canvas: an IMAGE command carries a picture handle (image_load); none: left out") {
    CanvasStore s;
    EmfCanvas c = spec(0, 0, 200, 200);
    REQUIRE(s.set("m", "map", &c) == CanvasStore::SetResult::Ok);
    EmfCanvasCmd img{};
    img.kind = EMF_CANVAS_IMAGE;
    img.rgba = 0xffffffffu;
    img.a[2] = img.a[3] = 32;
    img.a[6] = img.a[7] = 1;
    EmfCanvasCmd with = img;
    with.image = 3;
    const EmfCanvasCmd cmds[] = {img, with};
    uint32_t kept = 0;
    REQUIRE(s.draw("m", "map", cmds, 2, &kept));
    CHECK(kept == 1u);
    CHECK(s.view()->at(0).cmds[0].image == 3u);
}

TEST_CASE("map draw plan: the canvas in UI units, the whole world's rect so (cx, cz) lands at its centre, north up") {
    EmfCanvas c = spec(16, 16, 220, 220, EMF_CANVAS_TOP_LEFT);
    // the UI space is 1920 x 1080 units here; 2 m per canvas unit
    const MapDrawPlan p = plan_map_draw(c, 1920, 1080, 1500, 4740, 2.0, 10240, 10240);
    CHECK(p.ok);
    CHECK(p.vp[0] == doctest::Approx(16));
    CHECK(p.vp[2] == doctest::Approx(220));
    CHECK(p.s == doctest::Approx(0.5f));  // 1 unit per canvas unit / 2 m
    const float centre_x = 16 + 110, centre_y = 16 + 110;
    CHECK(p.cv[0] == doctest::Approx(centre_x - 0.5f * 1500));
    CHECK(p.cv[1] == doctest::Approx(centre_y - 0.5f * (10240 - 4740)));
    CHECK(p.cv[2] == doctest::Approx(0.5f * 10240));
    CHECK(p.base == doctest::Approx(0.2f));  // 2048 / 10240, as the game's map
    CHECK_FALSE(plan_map_draw(c, 1920, 1080, 1500, 4740, 2.0, 1, 1).ok);       // the world size not set yet
    CHECK_FALSE(plan_map_draw(c, 0, 1080, 1500, 4740, 2.0, 10240, 10240).ok);  // no UI space
}

TEST_CASE("canvas draw order: what comes before the last MAP command is under the map (drawn only without the map)") {
    std::vector<CanvasCmd> cmds(5);
    cmds[0].kind = EMF_CANVAS_CIRCLE_CMD;  // a background
    cmds[1].kind = EMF_CANVAS_MAP;
    cmds[2].kind = EMF_CANVAS_MARKERS;
    cmds[3].kind = EMF_CANVAS_TRI;
    cmds[4].kind = EMF_CANVAS_TEXT;
    CHECK(canvas_first_drawn(cmds, true) == 2u);   // the game draws the map: the background is not drawn over it
    CHECK(canvas_first_drawn(cmds, false) == 0u);  // no map on this build: the background shows
    std::vector<CanvasCmd> plain(2);
    plain[0].kind = EMF_CANVAS_RECT_CMD;
    CHECK(canvas_first_drawn(plain, true) == 0u);
}


TEST_CASE("minimap gate (review I2): the game's map is drawn only while the overlay showed the canvases in the last 250 ms") {
    minimap_shown_reset_for_tests();
    CHECK_FALSE(minimap_shown_recently(1000));
    minimap_note_shown(1000);
    CHECK(minimap_shown_recently(1100));
    CHECK(minimap_shown_recently(1250));
    CHECK_FALSE(minimap_shown_recently(1251));  // the console opened: the overlay stopped noting
}

TEST_CASE("canvas (review I3): one MAP, one FOG and one MARKERS per list (the last of each); the rest left out") {
    CanvasStore s;
    EmfCanvas c = spec(0, 0, 200, 200);
    REQUIRE(s.set("m", "map", &c) == CanvasStore::SetResult::Ok);
    std::vector<EmfCanvasCmd> cmds;
    for (int i = 0; i < 5; ++i) {
        EmfCanvasCmd map{};
        map.kind = EMF_CANVAS_MAP;
        map.a[0] = 100.0f + i;
        map.a[2] = 2;
        cmds.push_back(map);
        EmfCanvasCmd fog = map;
        fog.kind = EMF_CANVAS_FOG;
        cmds.push_back(fog);
        EmfCanvasCmd mk = map;
        mk.kind = EMF_CANVAS_MARKERS;
        cmds.push_back(mk);
        cmds.push_back(circle(1, 1, 1));
    }
    uint32_t kept = 0;
    REQUIRE(s.draw("m", "map", cmds.data(), static_cast<uint32_t>(cmds.size()), &kept));
    CHECK(kept == 8u);  // 5 circles + one of each heavy kind
    int maps = 0;
    for (const auto& k : s.view()->at(0).cmds)
        if (k.kind == EMF_CANVAS_MAP) {
            ++maps;
            CHECK(k.a[0] == doctest::Approx(104));  // the last one
        }
    CHECK(maps == 1);
}

TEST_CASE("canvas (review M2): metres per unit under 0.01 is left out; a plan with an endless rect is not ok") {
    EmfCanvasCmd map{};
    map.kind = EMF_CANVAS_MAP;
    map.a[2] = 0.001f;
    CHECK_FALSE(canvas_cmd_ok(map));
    map.a[2] = 0.01f;
    CHECK(canvas_cmd_ok(map));
    EmfCanvas c = spec(16, 16, 220, 220, EMF_CANVAS_TOP_LEFT);
    CHECK_FALSE(plan_map_draw(c, 1920, 1080, 1500, 4740, 1e-30, 10240, 10240).ok);
}

TEST_CASE("fog store (review I3): a snapshot answers without the lock and outlives a later copy") {
    FogStore st;
    std::vector<uint8_t> data(4 * 1024, 0);
    data[0] = 255;  // pixel (0, 0): the north-west corner
    FogHeader h;
    h.w = h.h = 512;
    h.cols = h.rows = 2;
    h.data = data.data();
    h.blocks = 4;
    REQUIRE(st.update(h, 1000));
    const auto snap = st.snapshot();
    REQUIRE(static_cast<bool>(snap));
    CHECK(fog_explored(snap->header, 4, 508) == doctest::Approx(1.0f));
    data[0] = 0;
    REQUIRE(st.update(h, 2000));  // a new copy
    CHECK(fog_explored(snap->header, 4, 508) == doctest::Approx(1.0f));  // the old snapshot is unchanged
}
