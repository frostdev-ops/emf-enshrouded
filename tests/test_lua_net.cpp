// Lua 1.19: mod messages (API v27) from Lua: emf.net.channel (the script's id in front, registered with the core once;
// a reload binds the new functions), emf.net.send / send_to / send_all / peer, the handlers called with the world, who
// sent it and the data.
#include <doctest.h>

#include <windows.h>

#include <string>
#include <vector>

#include "lua_host/host.h"
#include "lua_test_fake.h"

using emf::lua::Host;

namespace {
const char* kNet =
    "emf.net.channel('pick', {version = 2, to = 'server', max = 1024, on_server = function(w, from, data)\n"
    "  emf.log('server got ' .. data .. ' from ' .. from.player .. ' local ' .. tostring(from.is_local) ..\n"
    "    ' admin ' .. tostring(from.admin) .. ' v' .. from.version)\n"
    "end})\n"
    "emf.net.channel('show', {to = 'players', on_player = function(w, from, data) emf.log('player got ' .. data) end})\n"
    "emf.command('send', '', function(w, args) emf.reply(tostring(emf.net.send('pick', args[1]))) end)\n"
    "emf.command('to', '', function(w, args)\n"
    "  emf.reply(tostring(emf.net.send_to(w, tonumber(args[1]), 'show', 'hi')) .. ' ' ..\n"
    "    tostring(emf.net.send_all(w, 'show', 'all')) .. ' ' .. tostring(emf.net.peer(w, 7, 'show')))\n"
    "end)\n";

EmfNetFrom from(uint32_t player, uint8_t local) {
    EmfNetFrom f{};
    f.size = sizeof f;
    f.player = player;
    f.local = local;
    f.admin = local;
    f.peer_version = 2;
    return f;
}
}  // namespace

TEST_CASE("lua 1.19: channels get the script's id; sends reach the core; the handler gets world, sender and data") {
    Rig r(L"luanet");
    with_net(r.api);
    add_script(r.dir, "nm", kNet);
    Host host(&r.api, r.dir);
    host.load_all();
    REQUIRE(r.fake.net_channels.size() == 2u);
    const auto& pick = r.fake.net_channels[0];
    CHECK(pick.name == "nm/pick");
    CHECK(pick.def.version == 2u);
    CHECK(pick.def.flags == EMF_NET_TO_SERVER);
    CHECK(pick.def.max_size == 1024u);
    CHECK(pick.def.on_server != nullptr);
    CHECK(r.fake.net_channels[1].name == "nm/show");
    CHECK(r.fake.net_channels[1].def.flags == EMF_NET_TO_PLAYERS);
    CHECK(r.fake.net_channels[1].def.version == 1u);  // the default

    r.run_command("nm.send 3");
    REQUIRE(r.fake.net_sent.size() == 1u);
    CHECK(r.fake.net_sent[0].channel == "nm/pick");
    CHECK(r.fake.net_sent[0].data == "3");
    CHECK(r.fake.replies.back() == "true");
    r.run_command("nm.to 7");
    REQUIRE(r.fake.net_sent.size() == 3u);
    CHECK(r.fake.net_sent[1].player == 7u);
    CHECK(r.fake.net_sent[1].channel == "nm/show");
    CHECK(r.fake.replies.back() == "true 2 4");

    const EmfNetFrom f = from(7, 1);
    pick.def.on_server(kServer, &f, "abc", 3, pick.def.user);
    CHECK(logged("server got abc from 7 local true admin true v2"));
}

TEST_CASE("lua 1.19: a reload binds the new handler to the channel the core already has; a changed version is an error") {
    Rig r(L"luanet2");
    with_net(r.api);
    add_script(r.dir, "nr", "emf.net.channel('pick', {to = 'server', on_server = function(w, f, d) emf.log('old ' .. d) end})\n");
    Host host(&r.api, r.dir);
    host.load_all();
    REQUIRE(r.fake.net_channels.size() == 1u);
    Sleep(20);  // a newer modification time than the load saw
    write(r.dir + L"\\nr\\main.lua",
          "emf.net.channel('pick', {to = 'server', on_server = function(w, f, d) emf.log('new ' .. d) end})\n");
    host.check_reload_now();
    CHECK(r.fake.net_channels.size() == 1u);  // registered with the core once
    const auto& pick = r.fake.net_channels[0];
    const EmfNetFrom f = from(7, 0);
    pick.def.on_server(kServer, &f, "x", 1, pick.def.user);
    CHECK(logged("new x"));
    CHECK_FALSE(logged("old x"));
    Sleep(20);
    write(r.dir + L"\\nr\\main.lua", "emf.net.channel('pick', {version = 5, to = 'server', on_server = function() end})\n");
    host.check_reload_now();
    CHECK(logged("restart"));  // the core keeps a channel's version until the game restarts
}

TEST_CASE("lua 1.19: on a core before v27 emf.net.channel and send answer false") {
    Rig r(L"luanet3");
    add_script(r.dir, "no", kNet);
    Host host(&r.api, r.dir);
    host.load_all();
    CHECK(r.fake.net_channels.empty());
    r.run_command("no.send 3");
    REQUIRE_FALSE(r.fake.replies.empty());
    CHECK(r.fake.replies.back() == "false");
}
