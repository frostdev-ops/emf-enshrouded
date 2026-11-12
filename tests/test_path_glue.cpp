// path_find (API v37): the emf_path program over fake game calls (create, result, destroy, defaults).
#include <doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "common/offsets.h"
#include "core/api.h"
#include "core/api_sizes.h"
#include "core/path_glue.h"
#include "fake_world.h"

using namespace emf;
using namespace emf::core;
using emf::test::FakeProgramCtx;
using emf::test::FakeWorld;

namespace {
struct GamePoint {
    float x, y, z;
    uint8_t move, pad[3];
};
uint32_t g_next_id = 0;
std::vector<uint32_t> g_owners, g_destroyed;
std::vector<float> g_targets;  // target x of each created query
std::map<uint32_t, std::vector<GamePoint>> g_found;
bool g_get_faults = false;

uint32_t* f_create(void*, uint32_t* id, const void* q) {
    uint32_t owner = 0;
    float tx = 0;
    std::memcpy(&owner, q, 4);
    std::memcpy(&tx, static_cast<const uint8_t*>(q) + 0x1c, 4);
    g_owners.push_back(owner);
    g_targets.push_back(tx);
    *id = ++g_next_id;
    return id;
}
bool f_destroy(void*, uint32_t, uint32_t id) {
    g_destroyed.push_back(id);
    return true;
}
bool f_get(void*, void* out, uint32_t id, bool dirty) {
    if (g_get_faults) *static_cast<volatile int*>(nullptr) = 1;
    CHECK_FALSE(dirty);
    auto it = g_found.find(id);
    if (it == g_found.end()) return false;
    auto* o = static_cast<uint8_t*>(out);
    const GamePoint* p = it->second.data();
    const uint64_t n = it->second.size();
    std::memcpy(o, &p, 8);
    std::memcpy(o + 8, &n, 8);
    o[0x24] = 1;  // PathFound
    return true;
}
void* f_defaults(void* q) {
    std::memset(q, 0, kPathQueryBytes);
    return q;
}

std::vector<std::string> g_answers;
void on_path(EmfWorld*, uint32_t id, const EmfPathResult* r, const EmfPathPoint* pts, void*) {
    g_answers.push_back(std::to_string(id) + ":" + std::to_string(r->status) + ":" + std::to_string(r->count) + ":" +
                        std::to_string(r->reaches) + (pts && r->count ? ":" + std::to_string(pts[r->count - 1].move) : ""));
}

struct Rig {
    FakeWorld world;
    FakeProgramCtx ctx;
    alignas(8) uint8_t cmdbuf[0x40] = {};
    ProgramSpec spec;
    Rig() : ctx((world.put<uint8_t>(off::kKind, 0), world.p())) {
        void* cb = cmdbuf;
        std::memcpy(ctx.job + 0x08, &cb, 8);
        g_next_id = 0;
        g_owners.clear();
        g_destroyed.clear();
        g_targets.clear();
        g_found.clear();
        g_answers.clear();
        g_get_faults = false;
        PathGlueDeps d;
        d.fns.create = f_create;
        d.fns.destroy = f_destroy;
        d.fns.get = f_get;
        d.fns.defaults = f_defaults;
        d.component = [](uint8_t*, uint32_t hash, uint32_t entity) -> const void* {
            static uint8_t xf[56];
            return hash == off::kHashCurrentTransform && entity == 7 ? xf : nullptr;
        };
        d.after_step = [](std::function<void(EmfWorld*)> fn) { fn(nullptr); };
        d.log = [](const std::string&) {};
        d.now_ms = [] { return uint64_t(1000); };
        spec = path_setup(d);
    }
    void step() { spec.fn(ctx.p(), spec.user); }
};

EmfPathQuery query(float tx) {
    EmfPathQuery q{};
    q.size = sizeof q;
    q.from[1] = 50;
    q.to[0] = tx;
    q.to[1] = 50;
    return q;
}
}  // namespace

TEST_CASE("path glue: the emf_path program sends the query, reads the game's answer, destroys it, calls back") {
    Rig r;
    CHECK(r.spec.name == std::string(kPathProgramName));
    CHECK(r.spec.world_kind == EMF_WORLD_SERVER);
    REQUIRE(path_service() != nullptr);
    const uint32_t id = path_service()->request(query(12), 7, on_path, nullptr, 1000, nullptr);
    REQUIRE(id != 0);
    r.step();
    REQUIRE(g_owners.size() == 1u);
    CHECK(g_owners[0] == 7u);
    CHECK(g_targets[0] == 12.0f);
    r.step();
    CHECK(g_answers.empty());
    g_found[1] = {{4, 50, 0, 0, {}}, {8, 51, 0, 2, {}}, {12, 51, 0, 5, {}}};
    r.step();
    REQUIRE(g_answers.size() == 1u);
    CHECK(g_answers[0] == std::to_string(id) + ":1:3:1:5");  // found, 3 points, reaches, the last one a door
    CHECK(g_destroyed == std::vector<uint32_t>{1});
}

TEST_CASE("path glue: an owner that is gone ends the query cancelled; a fault in a game call turns the feature off") {
    Rig r;
    const uint32_t a = path_service()->request(query(12), 9, on_path, nullptr, 1000, nullptr);  // 9 has no transform
    r.step();
    REQUIRE(g_answers.size() == 1u);
    CHECK(g_answers[0] == std::to_string(a) + ":7:0:0");  // EMF_PATH_CANCELLED
    path_service()->request(query(12), 7, on_path, nullptr, 1000, nullptr);
    r.step();
    g_get_faults = true;
    r.step();
    CHECK(path_service() == nullptr);
}

TEST_CASE("api path_find (v37): queued on a server world; refused on a client world, with no owner, or no query") {
    Rig r;
    CoreState state;
    state.log_line = [](const std::string&) {};
    set_core_state(&state);
    FakeWorld cw;
    cw.put<uint8_t>(off::kKind, 2);
    EmfWorld* server = reinterpret_cast<EmfWorld*>(r.world.p());
    EmfWorld* client = reinterpret_cast<EmfWorld*>(cw.p());
    EmfPathQuery q = query(12);
    q.owner = 7;
    const uint32_t id = api_table()->path_find(server, &q, on_path, nullptr);
    CHECK(id != 0u);
    CHECK(api_table()->path_find(client, &q, on_path, nullptr) == 0u);  // joined games have no path grid
    CHECK(api_table()->path_find(server, nullptr, on_path, nullptr) == 0u);
    q.owner = 0;  // the first player: none in this fake world
    CHECK(api_table()->path_find(server, &q, on_path, nullptr) == 0u);
    CHECK(api_table()->path_cancel(id));
    CHECK_FALSE(api_table()->path_cancel(id + 1000));
    set_core_state(nullptr);
}

TEST_CASE("api table (v37): version 37, path_find and path_cancel at the end") {
    const EmfApi* api = api_table();
    CHECK(api->version == 37u);
    CHECK(api->size == kApiV37Size);
    CHECK(api->path_find != nullptr);
    CHECK(api->path_cancel != nullptr);
}

TEST_CASE("path glue: when the emf_path program has not run for 5 s, open queries are answered (cancelled)") {
    Rig r;
    const uint32_t id = path_service()->request(query(12), 7, on_path, nullptr, 1000, nullptr);
    path_server_tick(2000);  // the program never ran in this world: give it time
    CHECK(g_answers.empty());
    path_server_tick(2000 + kPathWatchdogMs - 1);
    CHECK(g_answers.empty());
    path_server_tick(2000 + kPathWatchdogMs);
    REQUIRE(g_answers.size() == 1u);
    CHECK(g_answers[0] == std::to_string(id) + ":7:0:0");
    r.step();  // running again: nothing left
    CHECK(g_answers.size() == 1u);
}

TEST_CASE("path glue: path_stop turns the feature off (the program could not be added)") {
    Rig r;
    REQUIRE(path_service() != nullptr);
    path_stop();
    CHECK(path_service() == nullptr);
}
