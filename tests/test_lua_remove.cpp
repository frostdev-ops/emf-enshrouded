// Lua 1.28 emf.remove_building (API v36) over the fake core API: the options table becomes the request; the answer is
// true plus the item given back, or false plus the reason.
#include <windows.h>

#include <doctest.h>

#include <string>

#include "lua_host/host.h"
#include "lua_test_fake.h"

using emf::lua::Host;

TEST_CASE("lua 1.28: emf.remove_building: refund false / 'drop' / a player id and force go into the request") {
    Rig r(L"lua_remove");
    add_script(r.dir, "rb",
               "emf.command('go', '', function(w)\n"
               "  local ok, item = emf.remove_building(w, 60)\n"
               "  emf.log('plain ' .. tostring(ok) .. ' ' .. tostring(item))\n"
               "  ok, item = emf.remove_building(w, 61, {refund = 'drop'})\n"
               "  emf.log('drop ' .. tostring(ok) .. ' ' .. tostring(item))\n"
               "  ok, item = emf.remove_building(w, 62, {refund = 7, force = true})\n"
               "  emf.log('player ' .. tostring(ok) .. ' ' .. tostring(item))\n"
               "  local bad = pcall(emf.remove_building, w, 63, {refund = 'chest'})\n"
               "  emf.log('bad ' .. tostring(bad))\n"
               "end)\n");
    r.fake.remove_refunded = 0x11;
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("rb.go");
    CHECK(logged("plain true nil"));
    CHECK(logged("drop true 17"));
    CHECK(logged("player true 17"));
    CHECK(logged("bad false"));  // an unknown refund is an error
    REQUIRE(r.fake.removes.size() == 3u);
    CHECK(r.fake.removes[0].entity == 60u);
    CHECK(r.fake.removes[0].refund == EMF_REFUND_NONE);
    CHECK(r.fake.removes[0].flags == 0u);
    CHECK(r.fake.removes[0].size == sizeof(EmfRemoveBuilding));
    CHECK(r.fake.removes[1].refund == EMF_REFUND_DROP);
    CHECK(r.fake.removes[2].refund == EMF_REFUND_PLAYER);
    CHECK(r.fake.removes[2].player == 7u);
    CHECK(r.fake.removes[2].flags == EMF_REMOVE_FORCE);
}

TEST_CASE("lua 1.28: emf.remove_building refused: false plus the core's reason") {
    Rig r(L"lua_remove_no");
    add_script(r.dir, "rn",
               "emf.command('go', '', function(w)\n"
               "  local ok, why = emf.remove_building(w, 61, {refund = false})\n"
               "  emf.log('no ' .. tostring(ok) .. ' ' .. tostring(why))\n"
               "end)\n");
    r.fake.remove_ok = false;
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("rn.go");
    CHECK(logged("no false it holds items (2 stacks)"));
}
