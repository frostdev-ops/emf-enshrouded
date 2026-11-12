// Lua 1.27 a minimap's parts (API v35): emf.player_view, emf.fog_at, emf.map_markers, emf.canvas, emf.canvas_draw and
// the marker key minimap, over the fake core API; a script's canvases leave with it.
#include <windows.h>

#include <doctest.h>

#include <string>

#include "lua_host/host.h"
#include "lua_test_fake.h"

using emf::lua::Host;

TEST_CASE("lua 1.27: emf.player_view, emf.fog_at and emf.map_markers; nil where the game has nothing yet") {
    Rig r(L"lua_view");
    add_script(r.dir, "pv",
               "emf.command('go', '', function()\n"
               "  local v = emf.player_view()\n"
               "  if v then emf.log('view ' .. v.x .. ' ' .. v.z .. ' ' .. v.heading .. ' ' .. v.world_size .. ' ' ..\n"
               "    tostring(v.camera_yaw)) else emf.log('view nil') end\n"
               "  emf.log('fog ' .. tostring(emf.fog_at(1, 2)))\n"
               "  local m = emf.map_markers()\n"
               "  emf.log('markers ' .. #m .. (m[1] and (' ' .. m[1].type .. ' ' .. m[1].x .. ' ' .. tostring(m[1].shown)) or ''))\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("pv.go");
    CHECK(logged("view nil"));
    CHECK(logged("fog nil"));
    CHECK(logged("markers 0"));
    r.fake.view_ok = true;
    r.fake.view.size = sizeof r.fake.view;
    r.fake.view.x = 1500;
    r.fake.view.z = 4740;
    r.fake.view.heading_deg = 90;
    r.fake.view.world_size = 10240;
    r.fake.fog_ok = true;
    r.fake.fog_value = 0.5f;
    EmfGameMarker gm{};
    gm.size = sizeof gm;
    gm.type = 0x11;
    gm.flags = EMF_GAME_MARKER_SHOWN;
    gm.x = 100;
    r.fake.game_markers.push_back(gm);
    r.run_command("pv.go");
    CHECK(logged("view 1500.0 4740.0 90.0 10240.0 nil"));  // no camera yet: camera_yaw nil
    CHECK(logged("fog 0.5"));
    CHECK(logged("markers 1 17 100.0 true"));
}

TEST_CASE("lua 1.27: emf.canvas builds the C canvas under '<mod>:<id>'; canvas_draw turns the list into commands") {
    Rig r(L"lua_canvas");
    add_script(r.dir, "mm",
               "emf.log('set ' .. tostring(emf.canvas('map', {corner = 'top_right', x = 16, y = 16, w = 220, h = 220,\n"
               "  shape = 'circle'})))\n"
               "emf.log('draw ' .. tostring(emf.canvas_draw('map', {\n"
               "  {'circle', 110, 110, 108, colour = 0x202020c0},\n"
               "  {'map', 1500, 4740, 2},\n"
               "  {'fog', 1500, 4740, 2, colour = 0x000000ff},\n"
               "  {'markers', 1500, 4740, 2},\n"
               "  {'tri', 110, 96, 102, 118, 118, 118},\n"
               "  {'text', 110, 10, 'N', size = 16},\n"
               "  {'line', 0, 0, 10, 10, width = 2},\n"
               "  {'rect', 0, 0, 220, 220, width = 3, colour = 0xffffffff},\n"
               "  {'image', 4, 10, 10, 32, 32, turn = 45},\n"
               "})))\n"
               "local bad = pcall(emf.canvas_draw, 'map', {{'star', 1, 2}})\n"
               "local bad2 = pcall(emf.canvas, 'map', {corner = 'middle'})\n"
               "emf.log('bad ' .. tostring(bad) .. tostring(bad2))\n");
    Host host(&r.api, r.dir);
    host.load_all();
    CHECK(logged("set true"));
    CHECK(logged("draw true"));
    CHECK(logged("bad falsefalse"));
    REQUIRE(r.fake.canvases.count("mm:map") == 1);
    const EmfCanvas& c = r.fake.canvases["mm:map"];
    CHECK(c.corner == EMF_CANVAS_TOP_RIGHT);
    CHECK(c.shape == EMF_CANVAS_CIRCLE);
    CHECK(c.w == 220);
    const auto& cmds = r.fake.canvas_cmds["mm:map"];
    REQUIRE(cmds.size() == 9u);
    CHECK(cmds[0].c.kind == EMF_CANVAS_CIRCLE_CMD);
    CHECK(cmds[0].c.rgba == 0x202020c0u);
    CHECK(cmds[0].c.a[2] == 108);
    CHECK(cmds[1].c.kind == EMF_CANVAS_MAP);
    CHECK(cmds[1].c.a[2] == 2);
    CHECK(cmds[2].c.kind == EMF_CANVAS_FOG);
    CHECK(cmds[3].c.kind == EMF_CANVAS_MARKERS);
    CHECK(cmds[4].c.kind == EMF_CANVAS_TRI);
    CHECK(cmds[4].c.rgba == 0xffffffffu);  // white by default
    CHECK(cmds[5].c.kind == EMF_CANVAS_TEXT);
    CHECK(cmds[5].s == "N");
    CHECK(cmds[5].c.a[2] == 16);
    CHECK(cmds[6].c.a[4] == 2);
    CHECK(cmds[7].c.a[4] == 3);
    CHECK(cmds[8].c.kind == EMF_CANVAS_IMAGE);
    CHECK(cmds[8].c.image == 4u);
    CHECK(cmds[8].c.a[2] == 32);
    CHECK(cmds[8].c.a[8] == 45);
}

TEST_CASE("lua 1.27: the marker key minimap; a reload takes the script's canvases away") {
    Rig r(L"lua_canvas_reload");
    add_script(r.dir, "mr", "emf.canvas('a', {x = 1, y = 1, w = 50, h = 50})\nemf.marker('m', {x = 1, minimap = true})\n");
    Host host(&r.api, r.dir);
    host.load_all();
    REQUIRE(r.fake.canvases.count("mr:a") == 1);
    REQUIRE(r.fake.markers.count("mr:m") == 1);
    CHECK((r.fake.markers["mr:m"].m.flags & EMF_MARKER_MINIMAP) != 0u);
    Sleep(20);
    write(r.dir + L"\\mr\\main.lua", "emf.log('reloaded without the canvas')\n");
    host.check_reload_now();
    CHECK(logged("reloaded without the canvas"));
    CHECK(r.fake.canvases.empty());
}


TEST_CASE("lua 1.27 (review M9): a colour that is not a number, or a list whose length is negative, is an error") {
    Rig r(L"lua_canvas_bad");
    add_script(r.dir, "mb",
               "emf.canvas('map', {w = 50, h = 50})\n"
               "local ok1 = pcall(emf.canvas_draw, 'map', {{'circle', 1, 1, 1, colour = 'red'}})\n"
               "local ok2 = pcall(emf.canvas_draw, 'map', {{'circle', 1, 1, 1, colour = 0/0}})\n"
               "local ok3 = pcall(emf.canvas_draw, 'map', setmetatable({}, {__len = function() return -5 end}))\n"
               "emf.log('bad ' .. tostring(ok1) .. tostring(ok2) .. tostring(ok3))\n");
    Host host(&r.api, r.dir);
    host.load_all();
    CHECK(logged("bad falsefalsefalse"));
}
