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

// Review I4: a script's orders end when it reloads or pauses (as its hit rules do).
TEST_CASE("lua 1.32: a reload or a pause cancels the script's enemy orders") {
    Rig r(L"lua_orders_drop");
    add_script(r.dir, "od",
               "emf.command('go', '', function(w) emf.enemy_order(w, 70, 900) emf.enemy_order(w, 71, 900) end)\n"
               "emf.command('boom', '', function() error('boom') end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("od.go");
    REQUIRE(r.fake.orders.size() == 2u);
    r.run_command("od.boom");
    REQUIRE(host.status("od").rfind("paused", 0) == 0);
    CHECK(r.fake.order_cancels.size() == 2u);
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

TEST_CASE("lua 1.33: emf.enemy_move walks an enemy through points (one point or a list; arrive, seconds, peaceful, allies)") {
    Rig r(L"lua_moves");
    r.fake.moving[70] = 1;
    add_script(r.dir, "mv",
               "emf.command('go', '', function(w)\n"
               "  emf.log('id ' .. tostring(emf.enemy_move(w, 70, {10, 20, 30}, function(world, id, status)\n"
               "    emf.log('done ' .. id .. ' ' .. status .. ' ' .. tostring(world ~= nil))\n"
               "  end)))\n"
               "  emf.log('id2 ' .. tostring(emf.enemy_move(w, 71, {{x = 1, y = 2, z = 3}, {4, 5, 6}},\n"
               "    {arrive = 3, seconds = 40, peaceful = true, allies = true})))\n"
               "  local id, why = emf.enemy_move(w, 13, {1, 2, 3})\n"
               "  emf.log('no ' .. tostring(id) .. ' ' .. tostring(why))\n"
               "  emf.log('bad ' .. tostring(pcall(emf.enemy_move, w, 70, {1, 2})))\n"
               "  emf.log('bad2 ' .. tostring(pcall(emf.enemy_move, w, 70, {1, 2, 3}, {arrive = 'near'})))\n"
               "  emf.log('moving ' .. tostring(emf.enemy_moving(w, 70)) .. ' ' .. tostring(emf.enemy_moving(w, 71)))\n"
               "  emf.log('cancel ' .. tostring(emf.enemy_move_cancel(2)) .. ' ' .. tostring(emf.enemy_move_cancel(9)))\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("mv.go");
    CHECK(logged("id 1"));
    CHECK(logged("id2 2"));
    CHECK(logged("no nil it has no enemy AI to move"));
    CHECK(logged("bad false"));
    CHECK(logged("bad2 false"));
    CHECK(logged("moving 1 nil"));
    CHECK(logged("cancel true false"));
    REQUIRE(r.fake.moves.size() == 2u);
    const auto& a = r.fake.moves[0];
    CHECK(a.o.size == sizeof(EmfMoveOrder));
    CHECK(a.o.enemy == 70u);
    CHECK(a.o.flags == 0u);
    CHECK(a.o.arrive == 0.0f);
    CHECK(a.o.seconds == 0.0f);
    REQUIRE(a.points.size() == 1u);
    CHECK(a.points[0].x == 10.0f);
    CHECK(a.points[0].z == 30.0f);
    const auto& b = r.fake.moves[1];
    REQUIRE(b.points.size() == 2u);
    CHECK(b.points[0].y == 2.0f);
    CHECK(b.points[1].x == 4.0f);
    CHECK(b.o.arrive == 3.0f);
    CHECK(b.o.seconds == 40.0f);
    CHECK(b.o.flags == (EMF_MOVE_PEACEFUL | EMF_MOVE_ALLIES));
    REQUIRE(a.cb != nullptr);
    a.cb(kServer, 1, EMF_MOVE_ARRIVED, a.user);  // after a server step
    CHECK(logged("done 1 arrived true"));
    b.cb(nullptr, 2, EMF_MOVE_CANCELLED, b.user);  // the world closed: no function given, nothing called
}

TEST_CASE("lua 1.33: a reload or a pause cancels the script's moves; their functions are not called") {
    Rig r(L"lua_moves_drop");
    add_script(r.dir, "md",
               "emf.command('go', '', function(w)\n"
               "  emf.enemy_move(w, 70, {1, 2, 3}, function() emf.log('called') end)\n"
               "  emf.enemy_move(w, 71, {1, 2, 3})\n"
               "end)\n"
               "emf.command('boom', '', function() error('boom') end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("md.go");
    REQUIRE(r.fake.moves.size() == 2u);
    r.run_command("md.boom");
    REQUIRE(host.status("md").rfind("paused", 0) == 0);
    CHECK(r.fake.move_cancels.size() == 2u);
    r.fake.moves[0].cb(kServer, 1, EMF_MOVE_CANCELLED, r.fake.moves[0].user);
    CHECK_FALSE(logged("called"));
}

TEST_CASE("lua 1.33 (review M4): emf.enemy_move with nil options still takes the function after them") {
    Rig r(L"lua_moves_nil");
    add_script(r.dir, "mn",
               "emf.command('go', '', function(w)\n"
               "  emf.enemy_move(w, 70, {1, 2, 3}, nil, function(world, id, status) emf.log('end ' .. status) end)\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("mn.go");
    REQUIRE(r.fake.moves.size() == 1u);
    REQUIRE(r.fake.moves[0].cb != nullptr);
    r.fake.moves[0].cb(kServer, 1, EMF_MOVE_STUCK, r.fake.moves[0].user);
    CHECK(logged("end stuck"));
}
