// Mod messages (API v27): channels (named <mod>/<name>, only under the caller's own mod id; the script host may name
// its scripts'), and loopback: in a game this process hosts (or an offline one) a message from the local game reaches
// on_server after the server world's next step, and the server's message to the host's own player reaches on_player
// after the client world's next step, once. Remote peers come with the chat transport.
#include <doctest.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/net/net_service.h"

using namespace emf::core::net;

namespace {

struct Rig {
    uint32_t local = 7;  // 0: a joined client (this process hosts nothing)
    uint64_t now = 1000;
    std::vector<std::pair<uint8_t, std::function<void(EmfWorld*)>>> posted;
    std::vector<std::string> logs;
    NetService s;
    Rig()
        : s(NetDeps{[this](uint32_t* id) {
                        *id = local;
                        return local != 0;
                    },
                    [this](uint8_t kind, std::function<void(EmfWorld*)> fn) {
                        posted.emplace_back(kind, std::move(fn));
                    },
                    [this] { return now; }, [this](const std::string& l) { logs.push_back(l); }}) {}
    void step(uint8_t kind) {  // the world of `kind` finished a step: its jobs run
        auto jobs = std::move(posted);
        posted.clear();
        for (auto& [k, fn] : jobs) {
            if (k == kind) fn(reinterpret_cast<EmfWorld*>(0x1000 + kind));
            else posted.emplace_back(k, std::move(fn));
        }
    }
};

struct Seen {
    EmfWorld* world;
    NetFrom from;
    std::string data;
};

ChannelDef channel(const char* name, uint32_t flags, std::vector<Seen>* server, std::vector<Seen>* player) {
    ChannelDef d;
    d.name = name;
    d.version = 3;
    d.flags = flags;
    if (server)
        d.on_server = [server](EmfWorld* w, const NetFrom& f, const std::string& data) { server->push_back({w, f, data}); };
    if (player)
        d.on_player = [player](EmfWorld* w, const NetFrom& f, const std::string& data) { player->push_back({w, f, data}); };
    return d;
}

}  // namespace

TEST_CASE("net service: channels are <mod>/<name> under the caller's own mod; the script host names its scripts'") {
    Rig r;
    std::string why;
    CHECK(r.s.add_channel("bm", channel("bm/pick", kToServer, nullptr, nullptr), &why));
    CHECK_FALSE(r.s.add_channel("bm", channel("bm/pick", kToServer, nullptr, nullptr), &why));  // taken
    CHECK(why.find("taken") != std::string::npos);
    CHECK_FALSE(r.s.add_channel("bm", channel("other/pick", kToServer, nullptr, nullptr), &why));  // another mod's id
    CHECK_FALSE(r.s.add_channel("bm", channel("bmpick", kToServer, nullptr, nullptr), &why));
    CHECK_FALSE(r.s.add_channel("bm", channel("bm/Pick!", kToServer, nullptr, nullptr), &why));
    CHECK(r.s.add_channel("lua", channel("beastmaster/state", kToPlayers, nullptr, nullptr), &why));  // the Lua host
    ChannelDef bad = channel("bm/v", kToServer, nullptr, nullptr);
    bad.version = 0;
    CHECK_FALSE(r.s.add_channel("bm", bad, &why));
    bad = channel("bm/f", 0, nullptr, nullptr);
    CHECK_FALSE(r.s.add_channel("bm", bad, &why));
    bad = channel("bm/big", kToServer, nullptr, nullptr);
    bad.max_size = kMaxMessage + 1;
    CHECK_FALSE(r.s.add_channel("bm", bad, &why));
}

TEST_CASE("net service: in a hosted game the local game's message reaches on_server after the server's next step") {
    Rig r;
    std::vector<Seen> server;
    std::string why;
    REQUIRE(r.s.add_channel("bm", channel("bm/pick", kToServer, &server, nullptr), &why));
    CHECK(r.s.to_server("bm", "bm/pick", "3", &why));
    CHECK(server.empty());  // not before the server world's step
    r.step(EMF_WORLD_CLIENT);
    CHECK(server.empty());
    r.step(EMF_WORLD_SERVER);
    REQUIRE(server.size() == 1u);
    CHECK(server[0].data == "3");
    CHECK(server[0].from.player == 7u);
    CHECK(server[0].from.local);
    CHECK(server[0].from.admin);
    CHECK(server[0].from.peer_version == 3u);
    CHECK(server[0].world == reinterpret_cast<EmfWorld*>(0x1000 + EMF_WORLD_SERVER));
}

TEST_CASE("net service: refusals: no server to talk to, the wrong direction, too large, over the rate, unknown") {
    Rig r;
    std::vector<Seen> server;
    std::string why;
    ChannelDef d = channel("bm/pick", kToServer, &server, nullptr);
    d.max_size = 8;
    d.per_10s = 2;
    REQUIRE(r.s.add_channel("bm", d, &why));
    REQUIRE(r.s.add_channel("bm", channel("bm/show", kToPlayers, nullptr, nullptr), &why));
    CHECK_FALSE(r.s.to_server("bm", "bm/show", "x", &why));  // a server-to-players channel
    CHECK_FALSE(r.s.to_server("bm", "bm/pick", "123456789", &why));  // over max_size
    CHECK(why.find("large") != std::string::npos);
    CHECK_FALSE(r.s.to_server("bm", "bm/nope", "x", &why));
    CHECK_FALSE(r.s.to_server("other", "bm/pick", "x", &why));  // only the channel's own mod sends on it
    CHECK(r.s.to_server("bm", "bm/pick", "a", &why));
    CHECK(r.s.to_server("bm", "bm/pick", "b", &why));
    CHECK_FALSE(r.s.to_server("bm", "bm/pick", "c", &why));  // per_10s = 2
    CHECK(why.find("rate") != std::string::npos);
    r.now += 10000;
    CHECK(r.s.to_server("bm", "bm/pick", "d", &why));
    r.local = 0;  // a joined client: no loopback, and no server peer yet (the chat transport is not there)
    CHECK_FALSE(r.s.to_server("bm", "bm/pick", "e", &why));
    CHECK(why.find("server") != std::string::npos);
}

TEST_CASE("net service: the server's message to the host's own player reaches on_player once; others are not peers") {
    Rig r;
    std::vector<Seen> player;
    std::string why;
    REQUIRE(r.s.add_channel("bm", channel("bm/show", kToPlayers, nullptr, &player), &why));
    CHECK(r.s.to_player("bm", 7, "bm/show", "hello", &why));
    CHECK_FALSE(r.s.to_player("bm", 8, "bm/show", "hello", &why));  // not a peer (no hello came)
    r.step(EMF_WORLD_SERVER);
    CHECK(player.empty());
    r.step(EMF_WORLD_CLIENT);
    REQUIRE(player.size() == 1u);
    CHECK(player[0].data == "hello");
    CHECK(player[0].from.local);
    CHECK(r.s.to_players("bm", "bm/show", "all") == 1u);  // the host only
    r.step(EMF_WORLD_CLIENT);
    CHECK(player.size() == 2u);
    CHECK(r.s.peer(7, "bm/show") == 3u);
    CHECK(r.s.peer(8, "bm/show") == 0u);
    CHECK(r.s.peer(0, "bm/show") == 3u);  // a player's game asking about the server: in process, itself
    CHECK(r.s.peer(7, "bm/nope") == 0u);
}
