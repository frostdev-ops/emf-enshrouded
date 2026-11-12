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
    for (uint64_t t = 0; t + 2 < kPathWatchdogSteps; ++t) path_server_tick(2000 + kPathWatchdogMs + t);
    CHECK(g_answers.empty());  // 5 s, but not yet 150 steps
    path_server_tick(2000 + kPathWatchdogMs + kPathWatchdogSteps);
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

namespace {
void on_path_faults(EmfWorld*, uint32_t, const EmfPathResult*, const EmfPathPoint*, void*) {
    *static_cast<volatile int*>(nullptr) = 1;  // a mod's bad callback
}
bool g_create_faults = false;
int g_create_calls = 0;
uint32_t* f_create_maybe_fault(void* cb, uint32_t* id, const void* q) {
    ++g_create_calls;
    if (g_create_faults) *static_cast<volatile int*>(nullptr) = 1;
    return f_create(cb, id, q);
}
}  // namespace

TEST_CASE("path glue (review I1): a callback that faults does not cost the other answers of that step") {
    Rig r;
    path_service()->request(query(12), 7, on_path_faults, nullptr, 1000, nullptr);
    const uint32_t b = path_service()->request(query(14), 7, on_path, nullptr, 1000, nullptr);
    r.step();
    g_found[1] = {{12, 50, 0, 0, {}}};
    g_found[2] = {{14, 50, 0, 0, {}}};
    r.step();
    REQUIRE(g_answers.size() == 1u);
    CHECK(g_answers[0].rfind(std::to_string(b) + ":1:", 0) == 0);
}

TEST_CASE("path glue (review I2): a fault in a game call stops that step; the open queries are answered at once") {
    Rig r;
    PathGlueDeps d;  // the Rig's deps with a create that faults
    d.fns.create = f_create_maybe_fault;
    d.fns.destroy = f_destroy;
    d.fns.get = f_get;
    d.fns.defaults = f_defaults;
    d.component = [](uint8_t*, uint32_t, uint32_t) -> const void* { static uint8_t xf[56]; return xf; };
    d.after_step = [](std::function<void(EmfWorld*)> fn) { fn(nullptr); };
    d.now_ms = [] { return uint64_t(1000); };
    r.spec = path_setup(d);
    for (int i = 0; i < 3; ++i) path_service()->request(query(12), 7, on_path, nullptr, 1000, nullptr);
    g_create_faults = true;
    g_create_calls = 0;
    r.step();
    g_create_faults = false;
    CHECK(g_create_calls == 1);       // no second create after the fault
    CHECK(g_answers.size() == 3u);    // all three answered in that step
    CHECK(path_service() == nullptr);
}

TEST_CASE("path glue (review): one long step is not a stalled program; steps without a run are") {
    Rig r;
    path_service()->request(query(12), 7, on_path, nullptr, 1000, nullptr);
    r.step();                                  // ran (the clock says 1000)
    path_server_tick(1000 + kPathWatchdogMs + 5000);  // one step that took long
    CHECK(g_answers.empty());
    for (uint64_t t = 0; t < kPathWatchdogSteps; ++t) path_server_tick(30000 + t);  // the program no longer runs
    CHECK(g_answers.size() == 1u);
}

TEST_CASE("path glue (review M1): no command buffer: the program does not count as running") {
    Rig r;
    std::memset(r.ctx.job + 0x08, 0, 8);
    path_service()->request(query(12), 7, on_path, nullptr, 1000, nullptr);
    for (uint64_t t = 0; t <= kPathWatchdogSteps; ++t) {  // the program is called every step, but cannot work
        r.step();
        path_server_tick(t ? 1000 + kPathWatchdogMs + t : 1000);
    }
    CHECK(g_answers.size() == 1u);
}

TEST_CASE("api path_find (review M5): a larger query (a later version's) is read up to what v37 knows") {
    Rig r;
    CoreState state;
    state.log_line = [](const std::string&) {};
    set_core_state(&state);
    struct Bigger {
        EmfPathQuery q;
        uint32_t later_field;
    } big{};
    big.q = query(12);
    big.q.owner = 7;
    big.q.size = sizeof big;
    big.later_field = 99;
    CHECK(api_table()->path_find(reinterpret_cast<EmfWorld*>(r.world.p()), &big.q, on_path, nullptr) != 0u);
    set_core_state(nullptr);
}

namespace {
int g_destroy_refusals = 0;  // how many destroys the fake game refuses (a full command buffer)
bool f_destroy_maybe_full(void* cb, uint32_t entity, uint32_t id) {
    if (g_destroy_refusals > 0) {
        --g_destroy_refusals;
        return false;
    }
    return f_destroy(cb, entity, id);
}
}  // namespace

TEST_CASE("path glue (review M3): a destroy the game refused is tried again next step") {
    Rig r;
    PathGlueDeps d;
    d.fns.create = f_create;
    d.fns.destroy = f_destroy_maybe_full;
    d.fns.get = f_get;
    d.fns.defaults = f_defaults;
    d.component = [](uint8_t*, uint32_t, uint32_t) -> const void* { static uint8_t xf[56]; return xf; };
    d.after_step = [](std::function<void(EmfWorld*)> fn) { fn(nullptr); };
    d.now_ms = [] { return uint64_t(1000); };
    r.spec = path_setup(d);
    path_service()->request(query(12), 7, on_path, nullptr, 1000, nullptr);
    r.step();
    g_found[1] = {{12, 50, 0, 0, {}}};
    g_destroy_refusals = 1;
    r.step();
    CHECK(g_answers.size() == 1u);
    CHECK(g_destroyed.empty());  // refused
    r.step();
    CHECK(g_destroyed == std::vector<uint32_t>{1});
}

TEST_CASE("path glue (review M3): queries the watchdog ends are destroyed in the game when the program runs again") {
    Rig r;
    path_service()->request(query(12), 7, on_path, nullptr, 1000, nullptr);
    r.step();  // created (game id 1)
    REQUIRE(g_owners.size() == 1u);
    path_server_tick(1000);
    for (uint64_t t = 0; t < kPathWatchdogSteps; ++t) path_server_tick(1000 + kPathWatchdogMs + t);
    CHECK(g_answers.size() == 1u);
    CHECK(g_destroyed.empty());
    r.step();  // running again
    CHECK(g_destroyed == std::vector<uint32_t>{1});
}

TEST_CASE("path glue (review M3): a new world forgets the old world's game records (nothing to destroy there)") {
    Rig r;
    path_service()->request(query(12), 7, on_path, nullptr, 1000, nullptr);
    r.step();
    path_world_reset();
    r.step();
    CHECK(g_destroyed.empty());
}
