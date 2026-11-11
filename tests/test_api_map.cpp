// API v34 through the core state: map actions registered by a mod and done by a player, ground_at on the server world
// only (a fake ground service), on_map_click listeners, map_cursor without an open map.
#include <windows.h>

#include <doctest.h>

#include <string>
#include <vector>

#include "common/offsets.h"
#include "core/api.h"
#include "core/api_sizes.h"
#include "core/map_actions.h"
#include "core/map_input.h"
#include "core/net/net_service.h"
#include "fake_world.h"

using namespace emf;
using namespace emf::core;
using emf::test::FakeWorld;

namespace {

uint64_t g_now = 10000;
std::vector<std::string> g_lines;
struct Did {
    uint32_t player;
    double x, z;
};
std::vector<Did> g_did;
struct Got {
    bool ok;
    double y;
    uint32_t size;
};
std::vector<Got> g_got;

void on_action(EmfWorld*, const EmfMapDone* d, void*) { g_did.push_back({d->player, d->x, d->z}); }
void on_ground(EmfWorld*, const EmfGround* g, void*) { g_got.push_back({g->status == EMF_GROUND_OK, g->y, g->size}); }
void on_click(const EmfMapClick*, void*) {}

struct Fixture {
    CoreState state;
    FakeWorld server, client;
    Fixture() {
        g_lines.clear();
        g_did.clear();
        g_got.clear();
        g_now = 10000;
        state.log_line = [](const std::string& s) { g_lines.push_back(s); };
        state.now_ms = [] { return g_now; };
        server.put<uint8_t>(off::kKind, EMF_WORLD_SERVER);
        client.put<uint8_t>(off::kKind, EMF_WORLD_CLIENT);
        GroundService::Deps d;
        d.limits = [](EmfWorld*, GroundLimits* l, std::string*) {
            l->box_top = 1200;
            l->grid_top = 1100;
            l->grid_bottom = -100;
            return true;
        };
        d.ray = [](EmfWorld*, double, double, double, double, RayResult* r) {
            r->loaded = r->hit = true;
            r->y = 321;
            return true;
        };
        d.now_ms = [] { return g_now; };
        state.map.ground = std::make_unique<GroundService>(std::move(d));
        set_core_state(&state);
    }
    ~Fixture() { set_core_state(nullptr); }
    EmfWorld* sw() { return reinterpret_cast<EmfWorld*>(server.p()); }
    EmfWorld* cw() { return reinterpret_cast<EmfWorld*>(client.p()); }
};

}  // namespace

TEST_CASE("api v34: version 34, size 952, the four map functions set") {
    const EmfApi* api = api_table();
    CHECK(api->version == 34u);
    CHECK(api->size == kApiV34Size);
    CHECK(kApiV34Size == 952u);
    CHECK(api->map_cursor);
    CHECK(api->on_map_click);
    CHECK(api->map_action);
    CHECK(api->ground_at);
}

TEST_CASE("map_action: refused without mod messages (players' games could not send the spot)") {
    Fixture f;
    CHECK_FALSE(api_table()->map_action("teleport", "Teleport here", 0, on_action, nullptr));
    CHECK(f.state.map.actions.empty());
}

TEST_CASE("map_action: a mod's action is announced and a player's finished gesture reaches it") {
    Fixture f;
    net::NetService svc(net_deps(&f.state));
    f.state.net = &svc;
    std::string why;
    REQUIRE(map_net_setup(&svc, &why));
    CHECK(svc.channel_versions().count(kMapActionsChannel) == 1);
    CHECK(api_table()->map_action("teleport", "Teleport here", 0, on_action, nullptr));
    CHECK_FALSE(api_table()->map_action("bad id", "x", 0, on_action, nullptr));
    MapActionsClient c;
    REQUIRE(c.on_announce(f.state.map.actions.announce_message()));
    CHECK(c.first_id() == "teleport");
    CHECK(f.state.map.actions.on_done(f.sw(), 3, false, c.done(1500, 4740), g_now));
    REQUIRE(g_did.size() == 1);
    CHECK(g_did[0].player == 3u);
    CHECK(g_did[0].x == 1500);
    CHECK(api_table()->map_action("teleport", nullptr, 0, nullptr, nullptr));  // a null cb removes it
    CHECK(f.state.map.actions.empty());
    REQUIRE(c.on_announce(f.state.map.actions.announce_message()));
    CHECK_FALSE(c.announced());
}

TEST_CASE("ground_at: the server world only; answered after the steps that agree") {
    Fixture f;
    CHECK_FALSE(api_table()->ground_at(f.cw(), 100, 200, on_ground, nullptr));
    CHECK_FALSE(api_table()->ground_at(f.sw(), -5, 200, on_ground, nullptr));
    CHECK_FALSE(api_table()->ground_at(f.sw(), 100, 200, nullptr, nullptr));
    REQUIRE(api_table()->ground_at(f.sw(), 100, 200, on_ground, nullptr));
    map_server_tick(f.sw());
    map_server_tick(f.sw());
    REQUIRE(g_got.size() == 1);
    CHECK(g_got[0].ok);
    CHECK(g_got[0].y == doctest::Approx(321));
    CHECK(g_got[0].size == sizeof(EmfGround));
}

TEST_CASE("on_map_click: up to 16 listeners; map_cursor is false with the map closed") {
    Fixture f;
    for (int i = 0; i < 16; ++i) CHECK(api_table()->on_map_click(on_click, nullptr));
    CHECK_FALSE(api_table()->on_map_click(on_click, nullptr));
    CHECK_FALSE(api_table()->on_map_click(nullptr, nullptr));
    double x = 0, z = 0;
    CHECK_FALSE(api_table()->map_cursor(&x, &z));
}

TEST_CASE("map actions: the host's own game hears the list after its world comes up (it says no hello)") {
    Fixture f;
    net::NetDeps d = net_deps(&f.state);
    static bool hosting = false;  // no world yet when the mod registers
    hosting = false;
    d.hosted = [](uint32_t* id) {
        *id = 7;
        return hosting;
    };
    net::NetService svc(std::move(d));
    f.state.net = &svc;
    std::string why;
    REQUIRE(map_net_setup(&svc, &why));
    REQUIRE(api_table()->map_action("teleport", "Teleport here", 1500, on_action, nullptr));
    hosting = true;
    map_world_setup(EMF_WORLD_CLIENT);  // the world loads after the mod registered
    f.state.dispatcher.run(f.cw(), EMF_WORLD_CLIENT, [](const std::string& s) { g_lines.push_back(s); });
    CHECK_FALSE(map_input_announced());
    map_server_tick(f.sw());
    f.state.dispatcher.run(f.cw(), EMF_WORLD_CLIENT, [](const std::string& s) { g_lines.push_back(s); });
    CHECK(map_input_announced());
    CHECK(f.state.map.client.first_id() == "teleport");
    CHECK(f.state.map.client.hold_ms() == 1500u);
    map_world_setup(EMF_WORLD_CLIENT);
    map_input_set_announced(false);
}

TEST_CASE("map_send_done: the console's map.act sends the spot like a finished gesture; refused with no action offered") {
    Fixture f;
    net::NetDeps d = net_deps(&f.state);
    d.hosted = [](uint32_t* id) {
        *id = 7;
        return true;
    };
    net::NetService svc(std::move(d));
    f.state.net = &svc;
    std::string why;
    REQUIRE(map_net_setup(&svc, &why));
    map_world_setup(EMF_WORLD_CLIENT);
    CHECK_FALSE(map_send_done(1500, 4740, &why));
    CHECK(why.find("no map action") != std::string::npos);
    REQUIRE(api_table()->map_action("teleport", "Teleport here", 0, on_action, nullptr));
    f.state.dispatcher.run(f.cw(), EMF_WORLD_CLIENT, [](const std::string& s) { g_lines.push_back(s); });
    REQUIRE(map_input_announced());
    CHECK_FALSE(map_send_done(-1, 4740, &why));  // outside the world: not sent
    CHECK(map_send_done(1500, 4740, &why));
    f.state.dispatcher.run(f.sw(), EMF_WORLD_SERVER, [](const std::string& s) { g_lines.push_back(s); });
    REQUIRE(g_did.size() == 1);
    CHECK(g_did[0].player == 7u);
    CHECK(g_did[0].x == 1500);
    map_world_setup(EMF_WORLD_CLIENT);
    map_input_set_announced(false);
}
