// Lua 1.29 emf.path_find / emf.path_cancel (API v37) over the fake core API: the options become the query, the answer
// reaches the script's function as a result table and a list of points; a reloaded script hears nothing.
#include <windows.h>

#include <doctest.h>

#include <string>

#include "lua_host/host.h"
#include "lua_test_fake.h"

using emf::lua::Host;

TEST_CASE("lua 1.29: emf.path_find builds the query from its options; the answer comes as tables") {
    Rig r(L"lua_path");
    add_script(r.dir, "pf",
               "emf.command('go', '', function(w)\n"
               "  id = emf.path_find(w, {1, 50, 2}, {x = 30, y = 51, z = 4}, {jumps = true, doors = true, closest = true,\n"
               "    width = 1.2, height = 2, max_jump_up = 3, nodes = 9000, owner = 7}, function(w2, res, pts)\n"
               "    emf.log('got ' .. res.status .. ' ' .. tostring(res.reaches) .. ' ' .. #pts .. ' ' ..\n"
               "      string.format('%.1f', res.length) .. ' ' .. pts[2].move .. ' ' .. pts[2].x)\n"
               "  end)\n"
               "  emf.log('id ' .. tostring(id))\n"
               "  local bad = pcall(emf.path_find, w, {1, 2}, {3, 4, 5}, function() end)\n"
               "  emf.log('bad ' .. tostring(bad))\n"
               "end)\n"
               "emf.command('stop', '', function(w) emf.log('cancel ' .. tostring(emf.path_cancel(id))) end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("pf.go");
    CHECK(logged("id 1"));
    CHECK(logged("bad false"));  // a position needs three numbers
    REQUIRE(r.fake.paths.size() == 1u);
    const EmfPathQuery& q = r.fake.paths[0].q;
    CHECK(q.size == sizeof(EmfPathQuery));
    CHECK(q.from[0] == 1.0f);
    CHECK(q.from[2] == 2.0f);
    CHECK(q.to[0] == 30.0f);
    CHECK(q.to[1] == 51.0f);
    CHECK(q.moves == (EMF_PATH_JUMP_UP | EMF_PATH_JUMP_DOWN | EMF_PATH_JUMP_ACROSS | EMF_PATH_OPEN_DOOR));
    CHECK(q.closest == 1);
    CHECK(q.width == doctest::Approx(1.2f));
    CHECK(q.height == 2.0f);
    CHECK(q.max_jump_up == 3);
    CHECK(q.max_nodes == 9000u);
    CHECK(q.owner == 7u);
    EmfPathResult res{};
    res.size = sizeof res;
    res.status = EMF_PATH_FOUND;
    res.reaches = 1;
    res.count = 2;
    res.length = 29.04f;
    const EmfPathPoint pts[2] = {{10, 50, 2, 0}, {30, 51, 4, 2}};
    r.fake.paths[0].cb(kServer, 1, &res, pts, r.fake.paths[0].user);
    CHECK(logged("got found true 2 29.0 2 30.0"));
    r.run_command("pf.stop");
    CHECK(logged("cancel true"));
}

TEST_CASE("lua 1.29: a refused path_find gives nil; an answer after a reload is not heard") {
    Rig r(L"lua_path_no");
    add_script(r.dir, "pn",
               "emf.command('go', '', function(w)\n"
               "  emf.log('id ' .. tostring(emf.path_find(w, {1, 50, 2}, {30, 51, 4}, function() emf.log('heard') end)))\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.fake.path_ok = false;
    r.run_command("pn.go");
    CHECK(logged("id nil"));
    r.fake.path_ok = true;
    r.run_command("pn.go");
    CHECK(logged("id 1"));
    REQUIRE(r.fake.paths.size() == 1u);
    Sleep(20);
    add_script(r.dir, "pn", "emf.log('reloaded')\n");  // a new main.lua: the script loads again
    host.check_reload_now();
    EmfPathResult res{};
    res.size = sizeof res;
    res.status = EMF_PATH_NO_PATH;
    r.fake.paths[0].cb(kServer, 1, &res, nullptr, r.fake.paths[0].user);
    CHECK_FALSE(logged("heard"));
}
