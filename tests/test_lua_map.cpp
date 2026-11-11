// Lua 1.26 the map (API v34): emf.map_cursor, emf.on_map_click (delivered on the next client world step, never on the
// render thread), emf.map_action / emf.remove_map_action under the script's own id, emf.ground_at; a script's actions
// leave with it (over the fake core API).
#include <windows.h>

#include <doctest.h>

#include <string>

#include "lua_host/host.h"
#include "lua_test_fake.h"

using emf::lua::Host;

TEST_CASE("lua 1.26: emf.map_cursor gives x, z while the map is open, nil otherwise") {
    Rig r(L"map_cursor");
    add_script(r.dir, "mc",
               "emf.command('where', '', function()\n"
               "  local x, z = emf.map_cursor()\n"
               "  emf.log('cursor ' .. tostring(x) .. ' ' .. tostring(z))\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("mc.where");
    CHECK(logged("cursor nil nil"));
    r.fake.cursor_ok = true;
    r.fake.cursor_x = 1500;
    r.fake.cursor_z = 4740.5;
    r.run_command("mc.where");
    CHECK(logged("cursor 1500.0 4740.5"));
}

TEST_CASE("lua 1.26: emf.map_action registers '<mod>:<id>' with its label and hold time; a player's gesture calls fn") {
    Rig r(L"map_action");
    add_script(r.dir, "tp",
               "emf.log('add ' .. tostring(emf.map_action('teleport', {label = 'Teleport here', hold_ms = 1500},\n"
               "  function(world, player, x, z) emf.log('did ' .. player .. ' ' .. x .. ' ' .. z .. ' ' ..\n"
               "    tostring(world ~= nil)) end)))\n"
               "emf.map_action('ping', nil, function() end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    CHECK(logged("add true"));
    REQUIRE(r.fake.map_actions.count("tp:teleport") == 1);
    CHECK(r.fake.map_actions["tp:teleport"].label == "Teleport here");
    CHECK(r.fake.map_actions["tp:teleport"].hold_ms == 1500u);
    REQUIRE(r.fake.map_actions.count("tp:ping") == 1);
    CHECK(r.fake.map_actions["tp:ping"].label == "ping");  // no label: the id
    fire_map_action(kServer, "tp:teleport", 7, 1500, 4740);
    CHECK(logged("did 7 1500.0 4740.0 true"));
}

TEST_CASE("lua 1.26: emf.map_action raises on an unknown option, a bad id or no function; remove_map_action") {
    Rig r(L"map_action_bad");
    add_script(r.dir, "mb",
               "local ok1 = pcall(emf.map_action, 'a', {lable = 'x'}, function() end)\n"
               "local ok2 = pcall(emf.map_action, 'has space', nil, function() end)\n"
               "local ok3 = pcall(emf.map_action, 'a', nil, nil)\n"
               "local ok4 = pcall(emf.map_action, 'a', {hold_ms = 'long'}, function() end)\n"
               "emf.log('bad ' .. tostring(ok1) .. tostring(ok2) .. tostring(ok3) .. tostring(ok4))\n"
               "emf.map_action('keep', nil, function() end)\n"
               "emf.command('rm', '', function() emf.log('rm ' .. tostring(emf.remove_map_action('keep')) .. ' ' ..\n"
               "  tostring(emf.remove_map_action('keep'))) end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    CHECK(logged("bad falsefalsefalsefalse"));
    REQUIRE(r.fake.map_actions.size() == 1);
    r.run_command("mb.rm");
    CHECK(logged("rm true false"));
    CHECK(r.fake.map_actions.empty());
}

TEST_CASE("lua 1.26: emf.ground_at answers fn(world, ok, x, y, z, why) once; refused at once gives false") {
    Rig r(L"ground_at");
    add_script(r.dir, "gr",
               "emf.command('go', '', function(world)\n"
               "  emf.log('asked ' .. tostring(emf.ground_at(world, 100, 200, function(w, ok, x, y, z, why)\n"
               "    emf.log('ground ' .. tostring(ok) .. ' ' .. x .. ' ' .. y .. ' ' .. z .. ' [' .. why .. ']') end)))\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("gr.go");
    CHECK(logged("asked true"));
    REQUIRE(r.fake.ground_asks.size() == 1);
    CHECK(r.fake.ground_asks[0].x == 100);
    const auto ask = r.fake.ground_asks[0];
    ask.cb(kServer, true, 100, 321.5, 200, "", ask.user);
    CHECK(logged("ground true 100.0 321.5 200.0 []"));
    r.fake.ground_ok = false;
    r.run_command("gr.go");
    CHECK(logged("asked false"));
}

TEST_CASE("lua 1.26: emf.on_map_click: a click waits for the next client world step, then fn(world, click)") {
    Rig r(L"map_click");
    add_script(r.dir, "ck",
               "emf.on_map_click(function(world, c)\n"
               "  emf.log('click ' .. c.button .. ' ' .. tostring(c.shift) .. ' ' .. tostring(c.ctrl) .. ' ' ..\n"
               "    tostring(c.alt) .. ' ' .. c.x .. ' ' .. c.z)\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    REQUIRE(r.fake.click_cbs.size() == 1);  // the host listens once for every script
    fire_map_click(1, EMF_MAP_SHIFT, 10, 20);
    CHECK_FALSE(logged("click"));  // not on the render thread
    host.tick(kClient);
    CHECK(logged("click right true false false 10.0 20.0"));
}

TEST_CASE("lua 1.26: a reload takes the script's map actions away; its click functions stop") {
    Rig r(L"map_reload");
    add_script(r.dir, "mr",
               "emf.map_action('teleport', nil, function() end)\n"
               "emf.on_map_click(function() emf.log('old click') end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    REQUIRE(r.fake.map_actions.count("mr:teleport") == 1);
    Sleep(20);
    write(r.dir + L"\\mr\\main.lua", "emf.log('reloaded without the map')\n");
    host.check_reload_now();
    CHECK(logged("reloaded without the map"));
    CHECK(r.fake.map_actions.empty());
    fire_map_click(0, 0, 1, 2);
    host.tick(kClient);
    CHECK_FALSE(logged("old click"));
}
