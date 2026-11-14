// The sample minimap mod (mods/minimap), run by the Lua host from a copy of the shipped folder over the fake core: it
// sets its canvas and draws the game's map, the markers, the places found, the arrow and the frame on client steps.
#include <windows.h>

#include <doctest.h>

#include <map>

#include "lua_mod_rig.h"

namespace {

void world_with_player(Fake& f) {
    f.view_ok = true;
    f.view.size = sizeof f.view;
    f.view.x = 1500;
    f.view.z = 4740;
    f.view.heading_deg = 90;
    f.view.world_size = 10240;
    EmfGameMarker close_by{};
    close_by.size = sizeof close_by;
    close_by.type = 0x11;
    close_by.flags = EMF_GAME_MARKER_SHOWN;
    close_by.x = 1520;
    close_by.z = 4760;
    EmfGameMarker away = close_by;
    away.x = 9000;  // off the panel
    f.game_markers = {close_by, away};
}

std::map<uint32_t, bool> g_minimap_keys;  // the fake keyboard (the host's key_down)

// Holds `vk` for one client step and lets it go in the next (a press, as on_key sees it).
void press(ModRig& m, uint32_t vk) {
    g_minimap_keys[vk] = true;
    m.host->tick(kClient);
    g_minimap_keys[vk] = false;
    m.host->tick(kClient);
}

double map_metres_per_unit(ModRig& m) {
    const auto& cmds = m.r.fake.canvas_cmds["minimap:map"];
    REQUIRE(cmds.size() > 1u);
    REQUIRE(cmds[1].c.kind == EMF_CANVAS_MAP);
    return cmds[1].c.a[2];
}

}  // namespace

TEST_CASE("minimap mod: a square canvas at the bottom right (above the clock); the map, markers, a dot, the arrow, frame, N") {
    ModRig m("minimap", L"minimap_mod", {}, world_with_player);
    m.host->tick(kClient);
    REQUIRE(m.r.fake.canvases.count("minimap:map") == 1);
    const EmfCanvas& c = m.r.fake.canvases["minimap:map"];
    CHECK(c.corner == EMF_CANVAS_BOTTOM_RIGHT);
    CHECK(c.shape == EMF_CANVAS_RECT);  // the game clips its map to a rectangle
    CHECK(c.y >= 60);  // clear of the game's clock (seen live)
    CHECK(c.w == 220);
    CHECK(c.hidden == 0);
    const auto& cmds = m.r.fake.canvas_cmds["minimap:map"];
    REQUIRE(cmds.size() == 7u);  // background (under the map), map, markers, one dot, arrow, frame, N
    CHECK(cmds[0].c.kind == EMF_CANVAS_RECT_CMD);
    CHECK(cmds[1].c.kind == EMF_CANVAS_MAP);
    CHECK(cmds[1].c.a[0] == 1500);
    CHECK(cmds[1].c.a[2] == 2);  // 2 m per unit at the start
    CHECK(cmds[2].c.kind == EMF_CANVAS_MARKERS);
    CHECK(cmds[3].c.kind == EMF_CANVAS_CIRCLE_CMD);
    CHECK(cmds[3].c.a[0] == doctest::Approx(110 + 10));  // 20 m east at 2 m per unit
    CHECK(cmds[4].c.kind == EMF_CANVAS_TRI);
    CHECK(cmds[4].c.a[0] == doctest::Approx(110 + 9));  // facing east: the tip to the right
    CHECK(cmds[6].s == "N");
}

TEST_CASE("minimap mod: round when set so: the map's square corners are painted over; nothing on a server step") {
    ModRig m("minimap", L"minimap_mod_round", {{"shape", "circle"}});
    m.host->tick(kServer);
    m.host->tick(kClient);  // no player view yet
    CHECK(m.r.fake.canvas_cmds.empty());
    world_with_player(m.r.fake);
    m.host->tick(kClient);
    REQUIRE(m.r.fake.canvases.count("minimap:map") == 1);
    CHECK(m.r.fake.canvases["minimap:map"].shape == EMF_CANVAS_CIRCLE);
    const auto& cmds = m.r.fake.canvas_cmds["minimap:map"];
    REQUIRE(cmds.size() == 8u);  // and a wide ring over the corners
    CHECK(cmds[0].c.kind == EMF_CANVAS_CIRCLE_CMD);
    bool ring = false;
    for (const auto& c : cmds)
        if (c.c.kind == EMF_CANVAS_CIRCLE_CMD && c.c.a[3] > 20) ring = true;  // a thick outline past the circle
    CHECK(ring);
}

TEST_CASE("minimap mod: the zoom is the setting \"zoom\" (it stays from one game to the next)") {
    ModRig m("minimap", L"minimap_mod_zoom", {{"zoom", "4"}}, world_with_player);
    m.host->tick(kClient);
    CHECK(map_metres_per_unit(m) == 4);
    m.r.fake.settings["minimap.zoom"] = "0.5";  // changed in the Mods tab
    m.host->tick(kClient);
    CHECK(map_metres_per_unit(m) == 0.5);
}

TEST_CASE("minimap mod: Numpad + and - change the zoom setting one step, from 0.5 to 8 m per unit") {
    ModRig m("minimap", L"minimap_mod_zoom_keys", {}, world_with_player);
    m.host->set_key_reader([](uint32_t vk) { return g_minimap_keys[vk]; });
    m.host->tick(kClient);
    CHECK(m.r.fake.settings["minimap.zoom"] == "2");  // mod.json's default
    press(m, VK_ADD);
    CHECK(m.r.fake.settings["minimap.zoom"] == "1");
    CHECK(map_metres_per_unit(m) == 1);
    press(m, VK_ADD);
    press(m, VK_ADD);  // already the closest
    CHECK(m.r.fake.settings["minimap.zoom"] == "0.5");
    for (int i = 0; i < 5; ++i) press(m, VK_SUBTRACT);
    CHECK(m.r.fake.settings["minimap.zoom"] == "8");
    CHECK(map_metres_per_unit(m) == 8);
    g_minimap_keys.clear();
}
