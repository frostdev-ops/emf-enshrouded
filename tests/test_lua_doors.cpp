// Lua 1.30 doors (API v38) over the fake core API: emf.door, emf.door_open, emf.door_close, emf.door_lock.
#include <windows.h>

#include <doctest.h>

#include <string>

#include "lua_host/host.h"
#include "lua_test_fake.h"

using emf::lua::Host;

TEST_CASE("lua 1.32: emf.enemy_order sends an enemy (seconds, damage, allies), emf.enemy_order_cancel, emf.enemy_target") {
    Rig r(L"lua_orders");
    r.fake.enemy_targets[70] = 900;
    add_script(r.dir, "eo",
               "emf.command('go', '', function(w)\n"
               "  emf.log('id ' .. tostring(emf.enemy_order(w, 70, 900)))\n"
               "  emf.log('id2 ' .. tostring(emf.enemy_order(w, 71, 900, {seconds = 20, damage = 40, allies = true})))\n"
               "  local id, why = emf.enemy_order(w, 13, 900)\n"
               "  emf.log('no ' .. tostring(id) .. ' ' .. tostring(why))\n"
               "  emf.log('bad ' .. tostring(pcall(emf.enemy_order, w, 70, 900, {seconds = 'soon'})))\n"
               "  emf.log('cancel ' .. tostring(emf.enemy_order_cancel(2)) .. ' ' .. tostring(emf.enemy_order_cancel(9)))\n"
               "  emf.log('target ' .. tostring(emf.enemy_target(w, 70)) .. ' ' .. tostring(emf.enemy_target(w, 71)))\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("eo.go");
    CHECK(logged("id 1"));
    CHECK(logged("id2 2"));
    CHECK(logged("no nil it has no enemy AI to send"));
    CHECK(logged("bad false"));
    CHECK(logged("cancel true false"));
    CHECK(logged("target 900 nil"));
    REQUIRE(r.fake.orders.size() == 2u);
    CHECK(r.fake.orders[0].size == sizeof(EmfEnemyOrder));
    CHECK(r.fake.orders[0].enemy == 70u);
    CHECK(r.fake.orders[0].target == 900u);
    CHECK(r.fake.orders[0].seconds == 0.0f);
    CHECK(r.fake.orders[0].damage == 0);
    CHECK(r.fake.orders[0].flags == 0u);
    CHECK(r.fake.orders[1].seconds == 20.0f);
    CHECK(r.fake.orders[1].damage == 40);
    CHECK(r.fake.orders[1].flags == EMF_ORDER_ALLIES);
}

TEST_CASE("lua 1.30: emf.door reads the state; door_open, door_close and door_lock call the core") {
    Rig r(L"lua_doors");
    r.fake.door_states[500] = EMF_DOOR_CLOSED | EMF_DOOR_LOCKED;
    r.fake.door_states[501] = EMF_DOOR_OPEN | EMF_DOOR_LOGIC;
    add_script(r.dir, "dr",
               "emf.command('go', '', function(w)\n"
               "  local s, f = emf.door(w, 500)\n"
               "  emf.log('d500 ' .. tostring(s) .. ' ' .. tostring(f.locked) .. ' ' .. tostring(f.logic))\n"
               "  s, f = emf.door(w, 501)\n"
               "  emf.log('d501 ' .. tostring(s) .. ' ' .. tostring(f.logic))\n"
               "  emf.log('none ' .. tostring(emf.door(w, 9)))\n"
               "  emf.log('open ' .. tostring(emf.door_open(w, 500, 'front')))\n"
               "  emf.log('close ' .. tostring(emf.door_close(w, 500)))\n"
               "  emf.log('lock ' .. tostring(emf.door_lock(w, 500, false)))\n"
               "  emf.log('no ' .. tostring(emf.door_open(w, 9)))\n"
               "  emf.log('bad ' .. tostring(pcall(emf.door_open, w, 500, 'sideways')))\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("dr.go");
    CHECK(logged("d500 closed true false"));
    CHECK(logged("d501 open true"));
    CHECK(logged("none nil"));
    CHECK(logged("open true"));
    CHECK(logged("close true"));
    CHECK(logged("lock true"));
    CHECK(logged("no false"));
    CHECK(logged("bad false"));
    REQUIRE(r.fake.door_calls.size() == 4u);
    CHECK(r.fake.door_calls[0] == "set 500 1 1");
    CHECK(r.fake.door_calls[1] == "set 500 0 0");
    CHECK(r.fake.door_calls[2] == "lock 500 0");
    CHECK(r.fake.door_calls[3] == "set 9 1 0");
}
