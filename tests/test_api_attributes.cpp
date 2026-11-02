// API v18: attribute_get, attribute_add, damage and the impact_hit_conclusion pre-hook, through the core state with fake
// game functions (attrFind, attrAdd, allocPendingHit, getComponent) and a fake world.
#include <windows.h>

#include <doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/api.h"
#include "core/api_sizes.h"
#include "core/attributes.h"
#include "core/program_registry.h"
#include "emf/emf.hpp"
#include "emf/generated/components.hpp"
#include "fake_world.h"

using namespace emf;
using namespace emf::core;
using emf::test::FakeProgramCtx;
using emf::test::FakeWorld;
using emf::test::SingletonStore;
namespace ecs = emf::gen::ecs;

namespace {

uint64_t g_now = 50000;
std::vector<std::string> g_lines;
std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> g_comps;  // (hash, entity) -> data

void* fake_gc(void* out, void*, uint32_t hash, uint32_t entity) {
    auto o = static_cast<uint64_t*>(out);
    o[0] = o[1] = 0;
    auto it = g_comps.find({hash, entity});
    if (it != g_comps.end()) {
        o[0] = reinterpret_cast<uint64_t>(it->second.data());
        o[1] = it->second.size();
    }
    return out;
}

struct Info {
    uint32_t root;
    uint8_t pad[8];
    uint16_t slot;
    uint8_t rest[18];
};
constexpr uint32_t kHealthRoot = 0xAAAA0001u, kStaminaRoot = 0xAAAA0002u, kXpRoot = 0xAAAA0003u, kLevelRoot = 0xAAAA0004u;
Info g_info_health{kHealthRoot, {}, 0, {}}, g_info_max{kHealthRoot, {}, 7, {}}, g_info_stamina{kStaminaRoot, {}, 0, {}},
    g_info_max_stamina{kStaminaRoot, {}, 7, {}};
// the Experience component's slots in these fakes: pending XP 0, XP 1, XP required 2; the Level component's level 0
Info g_info_xp_pending{kXpRoot, {}, 0, {}}, g_info_xp{kXpRoot, {}, 1, {}}, g_info_xp_required{kXpRoot, {}, 2, {}},
    g_info_level{kLevelRoot, {}, 0, {}};
bool g_max_stamina_known = true;
AttrFindResult* fake_find(AttrFindResult* out, uint32_t id, const void*) {
    std::memset(out, 0, sizeof *out);
    const Info* i = id == kAttrHealth ? &g_info_health : id == kAttrMaxHealth ? &g_info_max
                    : id == kAttrStamina ? &g_info_stamina
                    : id == kAttrMaxStamina && g_max_stamina_known ? &g_info_max_stamina
                    : id == kAttrXpPending ? &g_info_xp_pending : id == kAttrXp ? &g_info_xp
                    : id == kAttrXpRequired ? &g_info_xp_required : id == kAttrLevel ? &g_info_level : nullptr;
    if (!i) out->err = 0x1F;
    else out->info = reinterpret_cast<const uint8_t*>(i);
    return out;
}
int g_add_calls = 0;
void fake_add(void* comp, const void*, int32_t delta, uint32_t id) {
    ++g_add_calls;
    if (id == kAttrXpPending || id == kAttrXp || id == kAttrXpRequired) {
        static_cast<ecs::Experience*>(comp)->dataStorage[id == kAttrXpPending ? 0 : id == kAttrXp ? 1 : 2] += delta;
        return;
    }
    if (id == kAttrLevel) {
        static_cast<ecs::Level*>(comp)->dataStorage[0] += delta;
        return;
    }
    auto* h = static_cast<ecs::Health*>(comp);
    h->dataStorage[id == kAttrMaxHealth || id == kAttrMaxStamina ? 7 : 0] += delta;
}
// A player's Experience (pending, xp, required) and Level components, as the game keeps them.
void put_xp(uint32_t entity, int32_t pending, int32_t xp, int32_t required, int32_t level) {
    ecs::Experience e{};
    e.rootId.value = kXpRoot;
    e.storageOffset = 8;
    e.flags = 1;
    e.storageCount = 3;
    e.dataStorage[0] = pending;
    e.dataStorage[1] = xp;
    e.dataStorage[2] = required;
    g_comps[{ecs::Experience::kHash, entity}] = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&e),
                                                                     reinterpret_cast<uint8_t*>(&e) + sizeof e);
    ecs::Level l{};
    l.rootId.value = kLevelRoot;
    l.storageOffset = 8;
    l.flags = 1;
    l.storageCount = 2;
    l.dataStorage[0] = level;
    g_comps[{ecs::Level::kHash, entity}] = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&l),
                                                                reinterpret_cast<uint8_t*>(&l) + sizeof l);
}
int32_t xp_slot(uint32_t entity, int slot) {
    ecs::Experience e{};
    std::memcpy(&e, g_comps[{ecs::Experience::kHash, entity}].data(), sizeof e);
    return e.dataStorage[slot];
}
std::vector<std::vector<uint8_t>> g_pending;
void* fake_alloc(void*, uint8_t) {
    g_pending.push_back(std::vector<uint8_t>(kHitCandidateSize, uint8_t{0}));
    return g_pending.back().data();
}

void put_health(uint32_t entity, uint32_t root, int32_t cur, int32_t max) {
    ecs::Health h{};
    h.rootId.value = root;
    h.storageOffset = 8;
    h.flags = 1;
    h.storageCount = 8;
    h.dataStorage[0] = cur;
    h.dataStorage[7] = max;
    g_comps[{ecs::Health::kHash, entity}] = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&h),
                                                                 reinterpret_cast<uint8_t*>(&h) + sizeof h);
}
int32_t health_of(uint32_t entity) {
    ecs::Health h{};
    std::memcpy(&h, g_comps[{ecs::Health::kHash, entity}].data(), sizeof h);
    return h.dataStorage[0];
}
void put_combat(uint32_t entity, bool handle_hits, bool immune = false) {
    ecs::CombatFlags f{};
    f.handleHits = handle_hits;
    f.immune = immune;
    g_comps[{ecs::CombatFlags::kHash, entity}] = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&f),
                                                                      reinterpret_cast<uint8_t*>(&f) + sizeof f);
}
void put_position(uint32_t entity, double x, double y, double z) {
    std::vector<uint8_t> t(56, 0);
    const int64_t p[3] = {static_cast<int64_t>(x * 4294967296.0), static_cast<int64_t>(y * 4294967296.0),
                          static_cast<int64_t>(z * 4294967296.0)};
    std::memcpy(t.data(), p, sizeof p);
    const float scale[3] = {1, 1, 1};
    std::memcpy(t.data() + 40, scale, sizeof scale);
    g_comps[{ecs::CurrentTransform::kHash, entity}] = t;
}

// the game's xpRequired and maxPlayerLevel over the BalancingRegistry singleton (planted as g_balancing): level L needs
// 1000 * L XP, the cap is 25
int g_balancing = 0;
uint32_t fake_xp_required(uint32_t level, const void* bal) { return bal == &g_balancing ? 1000u * level : 0u; }
uint32_t fake_level_cap(const void* bal) { return bal == &g_balancing ? 25u : 0u; }

struct Fixture {
    CoreState state;
    FakeWorld server;
    SingletonStore ss;
    int registry = 0;
    Fixture(bool with_functions = true) {
        g_lines.clear();
        g_comps.clear();
        g_pending.clear();
        g_add_calls = 0;
        g_max_stamina_known = true;
        state.get_component = fake_gc;
        state.log_line = [](const std::string& s) { g_lines.push_back(s); };
        state.now_ms = [] { return g_now; };
        state.known_build = true;
        if (with_functions) {
            state.attr_find = fake_find;
            state.attr_add = fake_add;
            state.alloc_pending_hit = fake_alloc;
            state.damage_hooked = true;
            state.xp_required = fake_xp_required;
            state.level_cap = fake_level_cap;
        }
        ss.plant(server, "impact::AttributeRegistry", &registry);
        ss.plant(server, "BalancingRegistry", &g_balancing);
        set_core_state(&state);
    }
    ~Fixture() { set_core_state(nullptr); }
    EmfWorld* w() { return reinterpret_cast<EmfWorld*>(server.p()); }
    // runs f(world) as a callback after the server world's step (where world calls are allowed)
    template <class F>
    void in_callback(F f) {
        auto* p = new F(std::move(f));
        api_table()->run_on_world(EMF_WORLD_SERVER, [](EmfWorld* w, void* u) {
            auto* fn = static_cast<F*>(u);
            (*fn)(w);
            delete fn;
        }, p);
        step();
    }
    void step() { state.dispatcher.run(w(), EMF_WORLD_SERVER, [](const std::string&) {}); }
};

bool logged(const std::string& needle) {
    for (auto& l : g_lines)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST_CASE("api v18: the table has attribute_get, attribute_add and damage; EMF_ATTR_* are the game's ids") {
    const EmfApi* api = api_table();
    CHECK(api->version >= 18u);
    CHECK(EMF_API_VERSION >= 18);
    CHECK(api->attribute_get);
    CHECK(api->attribute_add);
    CHECK(api->damage);
    CHECK(EMF_ATTR_HEALTH == kAttrHealth);
    CHECK(EMF_ATTR_MAX_HEALTH == kAttrMaxHealth);
    CHECK(EMF_ATTR_STAMINA == kAttrStamina);
    CHECK(EMF_ATTR_MANA == kAttrMana);
    CHECK(kApiV17Size == offsetof(EmfApi, attribute_get));  // v18 appends after register_command_ex
    CHECK(kApiV18Size == offsetof(EmfApi, chat_tell));  // v19 appends after damage
}

TEST_CASE("attribute_get: current and max health of a creature and of the player, from the registry") {
    Fixture f;
    put_health(500, kHealthRoot, 700, 1498);
    put_health(1, kHealthRoot, 180, 250);
    const EmfApi* api = api_table();
    int32_t v = 0;
    CHECK(api->attribute_get(f.w(), 500, EMF_ATTR_HEALTH, &v));
    CHECK(v == 700);
    CHECK(api->attribute_get(f.w(), 500, EMF_ATTR_MAX_HEALTH, &v));
    CHECK(v == 1498);
    CHECK(api->attribute_get(f.w(), 1, EMF_ATTR_HEALTH, &v));
    CHECK(v == 180);
    v = -1;
    CHECK_FALSE(api->attribute_get(f.w(), 777, EMF_ATTR_HEALTH, &v));  // no Health
    CHECK_FALSE(api->attribute_get(f.w(), 500, EMF_ATTR_STAMINA, &v));  // no Stamina component
    CHECK_FALSE(api->attribute_get(f.w(), 500, 0x123u, &v));            // an id the registry does not know
    CHECK(v == -1);
    CHECK_FALSE(api->attribute_get(f.w(), 500, EMF_ATTR_HEALTH, nullptr));
}

TEST_CASE("attribute_get: Stamina is found by its root id among the attribute components") {
    Fixture f;
    put_health(500, kHealthRoot, 700, 1498);
    put_health(501, kStaminaRoot, 60, 0);
    g_comps[{ecs::Stamina::kHash, 500}] = g_comps[{ecs::Health::kHash, 501}];  // 500's Stamina: root kStaminaRoot
    int32_t v = 0;
    CHECK(api_table()->attribute_get(f.w(), 500, EMF_ATTR_STAMINA, &v));
    CHECK(v == 60);
}

TEST_CASE("attribute_get: server world only; off without the signature (logged once); no registry: false") {
    Fixture f;
    put_health(500, kHealthRoot, 700, 1498);
    FakeWorld client;
    client.put<uint8_t>(off::kKind, 2);
    int32_t v = 0;
    CHECK_FALSE(api_table()->attribute_get(reinterpret_cast<EmfWorld*>(client.p()), 500, EMF_ATTR_HEALTH, &v));
    CHECK_FALSE(logged("server world only"));  // a read on another world is just false, like get_component's NULL
    CHECK_FALSE(api_table()->attribute_add(reinterpret_cast<EmfWorld*>(client.p()), 500, EMF_ATTR_HEALTH, 1));
    CHECK(logged("server world only"));        // a change says why
    FakeWorld bare;  // a server world without the singleton
    CHECK_FALSE(api_table()->attribute_get(reinterpret_cast<EmfWorld*>(bare.p()), 500, EMF_ATTR_HEALTH, &v));
    Fixture off(false);
    put_health(500, kHealthRoot, 700, 1498);
    CHECK_FALSE(api_table()->attribute_get(off.w(), 500, EMF_ATTR_HEALTH, &v));
    CHECK_FALSE(api_table()->attribute_get(off.w(), 500, EMF_ATTR_HEALTH, &v));
    size_t n = 0;
    for (const auto& l : g_lines) n += l.find("AttrFind") != std::string::npos;
    CHECK(n == 1);
}

TEST_CASE("attribute_add: inside a world callback it runs at once, clamped to 0..max for health") {
    Fixture f;
    put_health(500, kHealthRoot, 700, 1498);
    const EmfApi* api = api_table();
    bool heal = false, hurt = false, overheal = false;
    f.in_callback([&](EmfWorld* w) {
        heal = api->attribute_add(w, 500, EMF_ATTR_HEALTH, 100);
        CHECK(health_of(500) == 800);
        overheal = api->attribute_add(w, 500, EMF_ATTR_HEALTH, 100000);
        CHECK(health_of(500) == 1498);
        hurt = api->attribute_add(w, 500, EMF_ATTR_HEALTH, -100000);
        CHECK(health_of(500) == 0);
    });
    CHECK(heal);
    CHECK(overheal);
    CHECK(hurt);
    CHECK(g_add_calls == 3);
}

TEST_CASE("api v22: EMF_ATTR_XP_PENDING, _XP, _XP_REQUIRED and _LEVEL are the game's ids (Experience, Level)") {
    CHECK(api_table()->version >= 22u);
    CHECK(EMF_API_VERSION >= 22);
    CHECK(EMF_ATTR_XP_PENDING == 0x1D7DD102u);
    CHECK(EMF_ATTR_XP == 0x8DCDDAF4u);
    CHECK(EMF_ATTR_XP_REQUIRED == 0x04723F62u);
    CHECK(EMF_ATTR_LEVEL == 0x724BF568u);
    CHECK(EMF_ATTR_XP_PENDING == kAttrXpPending);
    CHECK(EMF_ATTR_LEVEL == kAttrLevel);
}

TEST_CASE("attribute_get: a player's XP, pending XP, XP for the next level and level, from Experience and Level") {
    Fixture f;
    put_xp(1, 40, 1200, 3000, 7);
    const EmfApi* api = api_table();
    int32_t pending = 0, xp = 0, required = 0, level = 0;
    CHECK(api->attribute_get(f.w(), 1, EMF_ATTR_XP_PENDING, &pending));
    CHECK(api->attribute_get(f.w(), 1, EMF_ATTR_XP, &xp));
    CHECK(api->attribute_get(f.w(), 1, EMF_ATTR_XP_REQUIRED, &required));
    CHECK(api->attribute_get(f.w(), 1, EMF_ATTR_LEVEL, &level));
    CHECK(pending == 40);
    CHECK(xp == 1200);
    CHECK(required == 3000);
    CHECK(level == 7);
}

TEST_CASE("attribute_add: XP is given as pending XP (no maximum; the game levels up); XP, required and level are read-only") {
    Fixture f;
    put_xp(1, 0, 1200, 3000, 7);
    const EmfApi* api = api_table();
    bool give = false, take = false, xp = false, required = false, level = false;
    f.in_callback([&](EmfWorld* w) {
        give = api->attribute_add(w, 1, EMF_ATTR_XP_PENDING, 2500);
        take = api->attribute_add(w, 1, EMF_ATTR_XP_PENDING, -100);  // XP is only ever given
        xp = api->attribute_add(w, 1, EMF_ATTR_XP, 10);
        required = api->attribute_add(w, 1, EMF_ATTR_XP_REQUIRED, -10);
        level = api->attribute_add(w, 1, EMF_ATTR_LEVEL, 1);
    });
    CHECK(give);
    CHECK(xp_slot(1, 0) == 2500);  // the game's player_level moves it into XP and levels up on its next steps
    CHECK_FALSE(take);
    CHECK_FALSE(xp);
    CHECK_FALSE(required);
    CHECK_FALSE(level);
    CHECK(xp_slot(1, 1) == 1200);
    CHECK(xp_slot(1, 2) == 3000);
    CHECK(g_add_calls == 1);
}

int32_t level_of(uint32_t entity) {
    ecs::Level l{};
    std::memcpy(&l, g_comps[{ecs::Level::kHash, entity}].data(), sizeof l);
    return l.dataStorage[0];
}

TEST_CASE("api v22: player_xp_required and player_level_cap read the game's balancing table") {
    Fixture f;
    const EmfApi* api = api_table();
    CHECK(api->size >= offsetof(EmfApi, player_level_set) + sizeof(api->player_level_set));
    CHECK(api->player_xp_required(f.w(), 5) == 5000u);
    CHECK(api->player_level_cap(f.w()) == 25u);
    CHECK(api->player_xp_required(f.w(), 0) == 0u);   // no level 0
    CHECK(api->player_xp_required(f.w(), 26) == 0u);  // past the cap
    Fixture none(false);                              // the signatures not found on this build
    CHECK(api->player_xp_required(none.w(), 5) == 0u);
    CHECK(api->player_level_cap(none.w()) == 0u);
}

TEST_CASE("player_level_set: the level, XP, pending XP and requirement go together; 1..cap only") {
    Fixture f;
    put_xp(1, 300, 1200, 3000, 7);  // level 7, 1200 of 3000 XP, 300 XP given and not counted yet
    const EmfApi* api = api_table();
    bool lower = false, clamp = false, zero = false, over = false;
    f.in_callback([&](EmfWorld* w) {
        lower = api->player_level_set(w, 1, 4, 150);
        CHECK(level_of(1) == 4);
        CHECK(xp_slot(1, 1) == 150);   // XP into level 4
        CHECK(xp_slot(1, 0) == 0);     // the pending XP is gone: it would level the player up again
        CHECK(xp_slot(1, 2) == 4000);  // what player_level keeps for level 4: xpRequired(4), never 0
        clamp = api->player_level_set(w, 1, 4, 99999);
        CHECK(xp_slot(1, 1) == 3999);  // always below the requirement, or the next step levels up
        zero = api->player_level_set(w, 1, 0, 0);
        over = api->player_level_set(w, 1, 26, 0);
    });
    CHECK(lower);
    CHECK(clamp);
    CHECK_FALSE(zero);
    CHECK_FALSE(over);
    CHECK(level_of(1) == 4);
}

TEST_CASE("player_level_set: refused without the game's xpRequired, on a client world, for an entity without levels") {
    Fixture none(false);
    put_xp(1, 0, 10, 3000, 7);
    const EmfApi* api = api_table();
    bool no_fn = true, no_entity = true;
    none.in_callback([&](EmfWorld* w) { no_fn = api->player_level_set(w, 1, 4, 0); });
    CHECK_FALSE(no_fn);
    CHECK(level_of(1) == 7);
    Fixture f;
    put_xp(1, 0, 10, 3000, 7);
    f.in_callback([&](EmfWorld* w) { no_entity = api->player_level_set(w, 999, 4, 0); });
    CHECK_FALSE(no_entity);
}

TEST_CASE("player_level_set: with the level cap unknown (MaxPlayerLevel not found) nothing is set and no XP is read") {
    Fixture f;
    f.state.level_cap = nullptr;  // xpRequired found, the cap not: "1..cap" cannot be checked
    put_xp(1, 0, 10, 3000, 7);
    const EmfApi* api = api_table();
    CHECK(api->player_xp_required(f.w(), 5) == 0u);
    bool high = true, normal = true;
    f.in_callback([&](EmfWorld* w) {
        high = api->player_level_set(w, 1, 500, 0);
        normal = api->player_level_set(w, 1, 4, 0);
    });
    CHECK_FALSE(high);
    CHECK_FALSE(normal);
    CHECK(level_of(1) == 7);
    CHECK(logged("level cap"));
}

TEST_CASE("player_level_set: outside a world callback a level outside 1..cap is refused at once, not queued") {
    Fixture f;
    put_xp(1, 0, 10, 3000, 7);
    const EmfApi* api = api_table();
    CHECK_FALSE(api->player_level_set(f.w(), 1, 0, 0));
    CHECK_FALSE(api->player_level_set(f.w(), 1, 26, 0));
    CHECK_FALSE(api->player_level_set(f.w(), 1, 3000000000u, 0));
    CHECK(api->player_level_set(f.w(), 1, 4, 0));  // queued
    CHECK(level_of(1) == 7);
    f.step();
    CHECK(level_of(1) == 4);
    Fixture none(false);
    put_xp(1, 0, 10, 3000, 7);
    CHECK_FALSE(api->player_level_set(none.w(), 1, 4, 0));  // without xpRequired: refused at once
}

TEST_CASE("player_level_set: setting the level a player already has writes and logs nothing") {
    Fixture f;
    put_xp(1, 0, 150, 4000, 4);  // level 4, 150 of 4000 XP: what player_level_set(4, 150) makes
    const EmfApi* api = api_table();
    const int adds = g_add_calls;
    g_lines.clear();
    bool same = false;
    f.in_callback([&](EmfWorld* w) { same = api->player_level_set(w, 1, 4, 150); });
    CHECK(same);
    CHECK(g_add_calls == adds);
    CHECK_FALSE(logged("player_level_set"));
}

TEST_CASE("attribute_add: outside a world callback it waits for the server world's next step") {
    Fixture f;
    put_health(500, kHealthRoot, 700, 1498);
    CHECK(api_table()->attribute_add(f.w(), 500, EMF_ATTR_HEALTH, -200));  // queued
    CHECK(health_of(500) == 700);
    f.step();
    CHECK(health_of(500) == 500);
    // a later refusal (here: the entity has no Health) is logged
    CHECK(api_table()->attribute_add(f.w(), 999, EMF_ATTR_HEALTH, -1));
    f.step();
    CHECK(logged("attribute_add"));
    CHECK(logged("999"));
}

TEST_CASE("attribute_add: refused on a client world, without the signatures, for another component's id") {
    Fixture f;
    put_health(500, kHealthRoot, 700, 1498);
    FakeWorld client;
    client.put<uint8_t>(off::kKind, 2);
    CHECK_FALSE(api_table()->attribute_add(reinterpret_cast<EmfWorld*>(client.p()), 500, EMF_ATTR_HEALTH, 5));
    bool other = true;
    f.in_callback([&](EmfWorld* w) { other = api_table()->attribute_add(w, 500, EMF_ATTR_STAMINA, 5); });
    CHECK_FALSE(other);
    CHECK(g_add_calls == 0);
    Fixture off(false);
    put_health(500, kHealthRoot, 700, 1498);
    CHECK_FALSE(api_table()->attribute_add(off.w(), 500, EMF_ATTR_HEALTH, 5));
}

TEST_CASE("damage: queues a pending hit with credit for the attacker; the target needs CombatFlags.handleHits") {
    Fixture f;
    put_health(6808, kHealthRoot, 900, 900);
    put_combat(6808, true);
    put_position(6808, 10, 0, 0);
    put_position(1, 0, 0, 0);
    put_health(42, kHealthRoot, 900, 900);
    put_combat(42, false);  // a prop that takes no hits
    const EmfApi* api = api_table();
    bool ok = false, prop = true, zero = true;
    f.in_callback([&](EmfWorld* w) {
        ok = api->damage(w, 1, 6808, 250);
        prop = api->damage(w, 1, 42, 250);
        zero = api->damage(w, 1, 6808, 0);
    });
    CHECK(ok);
    CHECK_FALSE(prop);
    CHECK(logged("handleHits"));
    CHECK_FALSE(zero);
    CHECK(f.state.damage_queue.waiting() == 1);
    CHECK(health_of(6808) == 900);  // the game applies it, not EMF
}

TEST_CASE("damage: the impact_hit_conclusion pre-hook hands the hit to the game in the server world") {
    Fixture f;
    put_health(6808, kHealthRoot, 900, 900);
    put_combat(6808, true);
    put_position(6808, 10, 2, 0);
    put_position(1, 0, 2, 0);
    g_comps[{ecs::Faction::kHash, 6808}] = std::vector<uint8_t>(1, uint8_t{4});
    f.in_callback([&](EmfWorld* w) { CHECK(api_table()->damage(w, 1, 6808, 250)); });
    REQUIRE(f.state.damage_queue.waiting() == 1);
    // the program runs in another world (the client's copy of the same function): nothing happens there
    FakeWorld client;
    client.put<uint8_t>(off::kKind, 2);
    FakeProgramCtx cctx(client.p());
    damage_pre_conclusion(cctx.p());
    CHECK(g_pending.empty());
    // the server world: the ImpactSystem singleton gets the hit
    SingletonStore impact;  // (one singleton per fake store: the registry is not needed any more)
    int impact_system = 0;
    impact.plant(f.server, "impact::ImpactSystem", &impact_system);
    FakeProgramCtx ctx(f.server.p());
    damage_pre_conclusion(ctx.p());
    REQUIRE(g_pending.size() == 1);
    ecs::HitCandidate c;
    std::memcpy(&c, g_pending[0].data(), sizeof c);
    CHECK(c.targetId.id == 6808u);
    CHECK(c.rootSourceId.id == 1u);
    CHECK(c.healthChange == -250);
    CHECK(c.hitDirection.x == doctest::Approx(1.0f));
    CHECK(c.position.x == doctest::Approx(10.0f));
    CHECK(c.position.y == doctest::Approx(2.0f));
    CHECK(static_cast<uint8_t>(c.targetFaction) == 4u);
    CHECK(c.impactId.id != 0u);
    CHECK(f.state.damage_queue.waiting() == 0);
}

TEST_CASE("damage: from outside a world callback it is checked and queued at the server world's next step") {
    Fixture f;
    put_health(6808, kHealthRoot, 900, 900);
    put_combat(6808, true);
    CHECK(api_table()->damage(f.w(), 0, 6808, 10));  // accepted, not checked yet
    CHECK(f.state.damage_queue.waiting() == 0);
    f.step();
    CHECK(f.state.damage_queue.waiting() == 1);
    CHECK(api_table()->damage(f.w(), 0, 31337, 10));  // no such target: refused at the step, logged
    f.step();
    CHECK(f.state.damage_queue.waiting() == 1);
    CHECK(logged("31337"));
}

TEST_CASE("damage: off without allocPendingHit or the pre-hook (false, logged); client world refused") {
    Fixture f;
    put_health(6808, kHealthRoot, 900, 900);
    put_combat(6808, true);
    f.state.damage_hooked = false;
    bool ok = true;
    f.in_callback([&](EmfWorld* w) { ok = api_table()->damage(w, 1, 6808, 10); });
    CHECK_FALSE(ok);
    CHECK(logged("damage"));
    f.state.damage_hooked = true;
    FakeWorld client;
    client.put<uint8_t>(off::kKind, 2);
    CHECK_FALSE(api_table()->damage(reinterpret_cast<EmfWorld*>(client.p()), 1, 6808, 10));
}

TEST_CASE("the clear_hit_candidates and impact_hit_conclusion pre-hooks log their order once") {
    Fixture f;
    f.server.put<uint32_t>(off::kTick, 777);
    FakeProgramCtx ctx(f.server.p());
    damage_pre_clear(ctx.p());
    damage_pre_conclusion(ctx.p());
    damage_pre_conclusion(ctx.p());
    size_t n = 0;
    for (const auto& l : g_lines) n += l.find("same step (tick 777)") != std::string::npos;
    CHECK(n == 1);
}

// ---- edge cases ----

TEST_CASE("attribute_add: a stamina heal stops at max stamina; without a readable max it is refused (logged)") {
    Fixture f;
    put_health(500, kHealthRoot, 700, 1498);
    put_health(501, kStaminaRoot, 60, 100);
    g_comps[{ecs::Stamina::kHash, 500}] = g_comps[{ecs::Health::kHash, 501}];
    auto stamina = [] {
        ecs::Health h{};
        std::memcpy(&h, g_comps[{ecs::Stamina::kHash, 500}].data(), sizeof h);
        return h.dataStorage[0];
    };
    bool up = false, down = false, blind = true;
    f.in_callback([&](EmfWorld* w) {
        up = api_table()->attribute_add(w, 500, EMF_ATTR_STAMINA, 500);
        down = api_table()->attribute_add(w, 500, EMF_ATTR_STAMINA, -30);
        g_max_stamina_known = false;
        blind = api_table()->attribute_add(w, 500, EMF_ATTR_STAMINA, 5);
    });
    CHECK(up);
    CHECK(down);
    CHECK(stamina() == 70);
    CHECK_FALSE(blind);
    CHECK(logged("maximum"));
}

TEST_CASE("damage: refuses a target with CombatFlags.immune") {
    Fixture f;
    put_health(42, kHealthRoot, 900, 900);
    put_combat(42, true, true);
    bool ok = true;
    f.in_callback([&](EmfWorld* w) { ok = api_table()->damage(w, 1, 42, 10); });
    CHECK_FALSE(ok);
    CHECK(logged("immune"));
    CHECK(f.state.damage_queue.waiting() == 0);
}

TEST_CASE("damage: a queued hit records the server step it was queued in") {
    Fixture f;
    f.server.put<uint32_t>(off::kTick, 4321);
    put_health(6808, kHealthRoot, 900, 900);
    put_combat(6808, true);
    f.in_callback([&](EmfWorld* w) { CHECK(api_table()->damage(w, 1, 6808, 10)); });
    REQUIRE(f.state.damage_queue.waiting() == 1);
    CHECK(f.state.damage_queue.peek_queued_tick(0) == 4321u);
}

TEST_CASE("defer_to_server: a call made outside callbacks runs only on the world and session it was meant for") {
    Fixture f;
    put_health(500, kHealthRoot, 700, 1498);
    CHECK(api_table()->attribute_add(f.w(), 500, EMF_ATTR_HEALTH, -100));  // queued for f's world
    FakeWorld other;  // another server world steps first (the session's world object changed)
    f.state.dispatcher.run(reinterpret_cast<EmfWorld*>(other.p()), EMF_WORLD_SERVER, [](const std::string&) {});
    CHECK(health_of(500) == 700);
    CHECK(logged("gone"));
    g_lines.clear();
    CHECK(api_table()->attribute_add(f.w(), 500, EMF_ATTR_HEALTH, -100));
    ++f.state.server_session;  // a reload that reused the world's address
    f.step();
    CHECK(health_of(500) == 700);
    CHECK(logged("session"));
}

TEST_CASE("hook_program: a program EMF's core holds is refused with the reason") {
    Fixture f;
    f.state.programs = std::make_unique<ProgramRegistry>([](uintptr_t, void*, void**) { return true; });
    f.state.reserved_programs = {"impact_hit_conclusion", "clear_hit_candidates"};
    CHECK_FALSE(api_table()->hook_program("impact_hit_conclusion", [](void*, void*) {}, nullptr, nullptr));
    CHECK(logged("impact_hit_conclusion"));
    CHECK(logged("damage"));
    CHECK(api_table()->hook_program("some_other_program", [](void*, void*) {}, nullptr, nullptr));  // pending: fine
}

TEST_CASE("damage: on only once impact_hit_conclusion's pre-hook is really installed") {
    Fixture f(false);
    f.state.alloc_pending_hit = fake_alloc;
    f.state.attr_find = fake_find;
    f.state.attr_add = fake_add;
    CHECK_FALSE(f.state.damage_hooked);
    note_program_hook("clear_hit_candidates", true);
    CHECK_FALSE(f.state.damage_hooked);
    note_program_hook("impact_hit_conclusion", false);
    CHECK_FALSE(f.state.damage_hooked);
    CHECK(logged("impact_hit_conclusion"));  // a failed install is logged
    note_program_hook("impact_hit_conclusion", true);
    CHECK(f.state.damage_hooked);
}
