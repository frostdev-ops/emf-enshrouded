// Mod messages through the C API (v27): the table, a channel under the calling module's name, loopback through the
// dispatcher (after the next step of the world each side runs on), a faulting callback caught.
#include <windows.h>

#include <doctest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "common/offsets.h"
#include "core/api.h"
#include "core/api_sizes.h"
#include "core/net/net_service.h"
#include "fake_world.h"

using namespace emf;
using namespace emf::core;
using emf::test::FakeWorld;

namespace {
uint64_t g_now = 500000;
std::vector<std::string> g_lines;
struct Got {
    EmfWorld* world;
    uint32_t player;
    uint8_t local, admin;
    uint32_t version;
    std::string data;
    void* user;
};
std::vector<Got> g_server, g_player;
void on_server(EmfWorld* w, const EmfNetFrom* f, const void* d, uint32_t n, void* u) {
    g_server.push_back({w, f->player, f->local, f->admin, f->peer_version, std::string(static_cast<const char*>(d), n), u});
}
void on_player(EmfWorld* w, const EmfNetFrom* f, const void* d, uint32_t n, void* u) {
    g_player.push_back({w, f->player, f->local, f->admin, f->peer_version, std::string(static_cast<const char*>(d), n), u});
}
void faulting(EmfWorld*, const EmfNetFrom*, const void*, uint32_t, void*) {
    volatile int* p = nullptr;
    *p = 1;
}

struct Fixture {
    CoreState state;
    FakeWorld server, client;
    uint32_t local = 7;
    std::unique_ptr<net::NetService> svc;
    Fixture() {
        g_lines.clear();
        g_server.clear();
        g_player.clear();
        state.log_line = [](const std::string& s) { g_lines.push_back(s); };
        state.now_ms = [] { return g_now; };
        server.put<uint8_t>(off::kKind, EMF_WORLD_SERVER);
        client.put<uint8_t>(off::kKind, EMF_WORLD_CLIENT);
        svc = std::make_unique<net::NetService>(net_deps_for_test());
        state.net = svc.get();
        set_core_state(&state);
    }
    ~Fixture() { set_core_state(nullptr); }
    net::NetDeps net_deps_for_test() {
        net::NetDeps d = net_deps(&state);  // the core's own, with the host check replaced
        d.hosted = [this](uint32_t* id) {
            *id = local;
            return local != 0;
        };
        return d;
    }
    EmfWorld* ws() { return reinterpret_cast<EmfWorld*>(server.p()); }
    EmfWorld* wc() { return reinterpret_cast<EmfWorld*>(client.p()); }
    void step(uint8_t kind) {
        state.dispatcher.run(kind == EMF_WORLD_SERVER ? ws() : wc(), kind,
                             [](const std::string& s) { g_lines.push_back(s); });
    }
};
bool logged(const std::string& needle) {
    for (const auto& l : g_lines)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}
EmfNetChannel def(const char* name, uint32_t flags, EmfNetCallback srv, EmfNetCallback ply, void* user) {
    EmfNetChannel c{};
    c.size = sizeof c;
    c.version = 2;
    c.name = name;
    c.flags = flags;
    c.on_server = srv;
    c.on_player = ply;
    c.user = user;
    return c;
}
}  // namespace

TEST_CASE("api v27: the net calls follow image_load") {
    const EmfApi* api = api_table();
    CHECK(api->version >= 27u);
    CHECK(offsetof(EmfApi, net_channel) == kApiV26Size);
    CHECK(api->size >= kApiV27Size);
    CHECK(api->net_channel != nullptr);
    CHECK(api->net_to_server != nullptr);
    CHECK(api->net_to_player != nullptr);
    CHECK(api->net_to_players != nullptr);
    CHECK(api->net_peer != nullptr);
}

TEST_CASE("net_channel: under the calling module's name only; refusals are logged") {
    Fixture f;
    const EmfApi* api = api_table();
    EmfNetChannel c = def("emf_tests/pick", EMF_NET_TO_SERVER, on_server, nullptr, nullptr);
    CHECK(api->net_channel(&c));
    EmfNetChannel other = def("beastmaster/pick", EMF_NET_TO_SERVER, on_server, nullptr, nullptr);
    CHECK_FALSE(api->net_channel(&other));
    CHECK(logged("net_channel"));
    CHECK_FALSE(api->net_channel(nullptr));
    EmfNetChannel short_def = c;
    short_def.size = 8;
    CHECK_FALSE(api->net_channel(&short_def));
}

TEST_CASE("net messages in process: to the server after its step, back to the host after the client's step") {
    Fixture f;
    const EmfApi* api = api_table();
    int tag = 0;
    EmfNetChannel up = def("emf_tests/pick", EMF_NET_TO_SERVER, on_server, nullptr, &tag);
    EmfNetChannel down = def("emf_tests/show", EMF_NET_TO_PLAYERS, nullptr, on_player, &tag);
    REQUIRE(api->net_channel(&up));
    REQUIRE(api->net_channel(&down));
    CHECK(api->net_to_server("emf_tests/pick", "3", 1));
    f.step(EMF_WORLD_SERVER);
    REQUIRE(g_server.size() == 1u);
    CHECK(g_server[0].data == "3");
    CHECK(g_server[0].player == 7u);
    CHECK(g_server[0].local == 1);
    CHECK(g_server[0].admin == 1);
    CHECK(g_server[0].version == 2u);
    CHECK(g_server[0].user == &tag);
    CHECK(g_server[0].world == f.ws());
    CHECK(api->net_to_player(f.ws(), 7, "emf_tests/show", "hi", 2));
    CHECK_FALSE(api->net_to_player(f.ws(), 8, "emf_tests/show", "hi", 2));
    f.step(EMF_WORLD_CLIENT);
    REQUIRE(g_player.size() == 1u);
    CHECK(g_player[0].data == "hi");
    CHECK(g_player[0].world == f.wc());
    CHECK(api->net_to_players(f.ws(), "emf_tests/show", "all", 3) == 1u);
    CHECK(api->net_peer(f.ws(), 7, "emf_tests/show") == 2u);
    CHECK(api->net_peer(nullptr, 0, "emf_tests/pick") == 2u);
    CHECK_FALSE(api->net_to_server("emf_tests/pick", nullptr, 1));  // data NULL with a size
    CHECK(api->net_to_server("emf_tests/pick", nullptr, 0));        // an empty message is fine
}

TEST_CASE("net messages: a callback that faults is caught and logged") {
    Fixture f;
    const EmfApi* api = api_table();
    EmfNetChannel up = def("emf_tests/boom", EMF_NET_TO_SERVER, faulting, nullptr, nullptr);
    REQUIRE(api->net_channel(&up));
    CHECK(api->net_to_server("emf_tests/boom", "x", 1));
    f.step(EMF_WORLD_SERVER);
    CHECK(logged("faulted"));
}
