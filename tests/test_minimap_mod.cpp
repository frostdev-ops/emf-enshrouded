// The sample minimap mod (mods/minimap), run by the Lua host from a copy of the shipped folder over the fake core: it
// sets its canvas and draws the game's map, the markers, the places found, the arrow and the frame on client steps.
#include <windows.h>

#include <doctest.h>

#include "lua_mod_rig.h"

TEST_CASE("minimap mod: a round canvas at the top right; on a client step: the map, markers, dots, arrow, frame, N") {
    ModRig m("minimap", L"minimap_mod", {}, [](Fake& f) {
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
    });
    m.host->tick(kClient);
    REQUIRE(m.r.fake.canvases.count("minimap:map") == 1);
    const EmfCanvas& c = m.r.fake.canvases["minimap:map"];
    CHECK(c.corner == EMF_CANVAS_TOP_RIGHT);
    CHECK(c.shape == EMF_CANVAS_CIRCLE);
    CHECK(c.w == 220);
    CHECK(c.hidden == 0);
    const auto& cmds = m.r.fake.canvas_cmds["minimap:map"];
    REQUIRE(cmds.size() == 7u);  // background, map, markers, one dot, arrow, frame, N
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

TEST_CASE("minimap mod: square when set so; nothing drawn on a server step or without a player") {
    ModRig m("minimap", L"minimap_mod_sq", {{"shape", "rect"}});
    m.host->tick(kServer);
    m.host->tick(kClient);  // no player view yet
    CHECK(m.r.fake.canvas_cmds.empty());
    m.r.fake.view_ok = true;
    m.r.fake.view.size = sizeof m.r.fake.view;
    m.host->tick(kClient);
    REQUIRE(m.r.fake.canvases.count("minimap:map") == 1);
    CHECK(m.r.fake.canvases["minimap:map"].shape == EMF_CANVAS_RECT);
    REQUIRE_FALSE(m.r.fake.canvas_cmds["minimap:map"].empty());
    CHECK(m.r.fake.canvas_cmds["minimap:map"][0].c.kind == EMF_CANVAS_RECT_CMD);  // a square background
}
