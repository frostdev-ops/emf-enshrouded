// Giving and removing buffs (API v32): the internal emf_buffs program calling the game's applyBuffCore and cleansing
// a slot through its write accessor (fake engine functions).
#include <doctest.h>

#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "common/offsets.h"
#include "core/buff_service.h"
#include "core/system_programs.h"
#include "emf/generated/components.hpp"
#include "fake_world.h"

using namespace emf;
using namespace emf::core;
using emf::test::EventStore;
using emf::test::FakeProgramCtx;
using emf::test::FakeWorld;
using emf::test::SingletonStore;

namespace {
namespace gen = emf::gen::ecs;

template <typename T>
void put(uint8_t* p, size_t off, T v) {
    std::memcpy(p + off, &v, sizeof v);
}
template <typename T>
T get(const uint8_t* p, size_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}

constexpr uint32_t kWet = 0xAC13C6C3u;
constexpr uint32_t kPoisoned = 0x369C3618u;
constexpr uint32_t kElixir = 0xFF24634Fu;

// the registry: *(registry+0x20) = the collection blob, buffs array at +0x18
struct Registry {
    std::vector<uint8_t> blob = std::vector<uint8_t>(0x20 + 3 * kBuffTypeSize);
    alignas(8) uint8_t reg[0x40] = {};
    Registry() {
        put<int32_t>(blob.data(), 0x18, 8);
        put<uint32_t>(blob.data(), 0x1C, 3);
        uint8_t* r = rec(0);
        put<uint32_t>(r, 0, kWet);
        r[4] = EMF_BUFF_DEBUFF;
        r[5] = 1;
        put<int64_t>(r, 8, 900'000'000'000LL);
        r = rec(1);
        put<uint32_t>(r, 0, kPoisoned);
        r[4] = EMF_BUFF_DEBUFF;
        r[0x49] = 1;
        put<uint16_t>(r, 0x5C, 0xFF7F);  // players only
        r = rec(2);
        put<uint32_t>(r, 0, kElixir);
        r[4] = EMF_BUFF_BUFF;
        const uint8_t* b = blob.data();
        std::memcpy(reg + 0x20, &b, 8);
    }
    uint8_t* rec(int i) { return blob.data() + 0x20 + i * kBuffTypeSize; }
} g_reg;

// accessors in the iterator: other-read sorted by qualified name, then other-write
const char* const kRead[] = {"CritChance", "CritDamageMod",    "DamageMod",      "DamageModAgainst", "Dexterity",
                             "Faction",    "ImpactHierarchy",  "ImpactItemConfig", "Intelligence",   "Level",
                             "PlayerSkillsUi", "Strength",     "ToolDamageMod",  "WetDurationReduction"};
uint64_t acc(int index) { return 0xB0 + index; }  // 0..13 read, 14 BuffContainer, 15 LifeTime
constexpr int kFaction = 5, kImpactHierarchy = 6, kLevel = 9, kContainer = 14, kLifeTime = 15;

alignas(8) uint8_t g_balancing[8], g_skilltree[8], g_items[8], g_change_set[8];
std::map<std::pair<uint64_t, uint32_t>, void*> g_by_acc;
std::map<std::pair<uint32_t, uint32_t>, void*> g_by_hash;
int g_writes = 0;
void plant(uint64_t a, uint32_t hash, uint32_t e, void* p) {
    g_by_acc[{a, e}] = p;
    g_by_hash[{hash, e}] = p;
}
void* f_read_other(void** out, void*, uint64_t a, uint32_t e) {
    const auto it = g_by_acc.find({a, e});
    *out = it == g_by_acc.end() ? nullptr : it->second;
    return out;
}
void* f_write_other(void** out, void*, void*, uint64_t a, uint32_t e, uint8_t) {
    ++g_writes;
    return f_read_other(out, nullptr, a, e);
}
void* f_get_component(void* out, void*, uint32_t hash, uint32_t e) {
    auto o = static_cast<uint64_t*>(out);
    o[0] = o[1] = 0;
    const auto it = g_by_hash.find({hash, e});
    if (it != g_by_hash.end()) {
        o[0] = reinterpret_cast<uint64_t>(it->second);
        o[1] = 8;
    }
    return out;
}

bool g_bad_events = false;
void f_iter_begin(void* ctx, void* iter, uint32_t) {
    auto* it = static_cast<uint64_t*>(iter);
    void* job = nullptr;
    std::memcpy(&job, ctx, 8);
    it[0] = reinterpret_cast<uint64_t>(ctx);
    // singletons in name order: BalancingRegistry, BuffRegistry, g38_items::ItemRegistry, g38_skilltree::SkillTree
    it[1] = reinterpret_cast<uint64_t>(g_balancing);
    it[2] = reinterpret_cast<uint64_t>(g_reg.reg);
    it[3] = reinterpret_cast<uint64_t>(g_items);
    it[4] = reinterpret_cast<uint64_t>(g_skilltree);
    for (int i = 0; i < 16; ++i) it[5 + i] = acc(i);
    it[21] = it[22] = g_bad_events ? 0x1234 : reinterpret_cast<uint64_t>(job);
}
uint64_t f_register(void*, const void*, char) { return 1; }

// containers per entity; player 1 and a wolf 2000 (faction 6)
struct Box {
    alignas(8) uint8_t b[kBuffContainerSize] = {};
};
std::map<uint32_t, Box> g_box;
uint8_t g_faction_player = 7, g_faction_wolf = 6;

// the fake applyBuffCore: puts the type in the first free slot of its group unless told otherwise
struct Core {
    int calls = 0;
    uint32_t target = 0, source = 0;
    const void* type = nullptr;
    uint8_t ctx[kApplyBuffCtxSize] = {};
    uint8_t err = 0;
    bool silent = false;  // "ok" without applying (as for an excluded faction)
    bool crash = false;
} g_core;
uint8_t* f_apply(uint8_t* out, uint32_t target, uint32_t source, const void* type, void* ctx) {
    ++g_core.calls;
    g_core.target = target;
    g_core.source = source;
    g_core.type = type;
    std::memcpy(g_core.ctx, ctx, kApplyBuffCtxSize);
    if (g_core.crash) *static_cast<volatile int*>(nullptr) = 1;
    std::memset(out, 0, 8);
    out[0] = g_core.err;
    if (!g_core.err && !g_core.silent) {
        const uint8_t* t = static_cast<const uint8_t*>(type);
        const size_t group = t[4] == EMF_BUFF_BUFF ? 0x30 : 0xF0;
        uint8_t* c = g_box[target].b;
        for (int i = 0; i < 16; ++i)
            if (!get<uint32_t>(c, group + i * 12)) {
                put<uint32_t>(c, group + i * 12, 7000 + g_core.calls);
                put<uint32_t>(c, group + i * 12 + 4, get<uint32_t>(t, 0));
                break;
            }
    }
    return out;
}

uint64_t g_now = 1000;
std::vector<std::string> g_log;
struct Got {
    uint32_t kind, target, type, buff_entity;
    std::string why;
};
std::vector<Got> g_got;
void on_done(EmfWorld*, const EmfBuffEvent* e, void* user) {
    g_got.push_back({e->kind, e->target, e->type, e->buff_entity, e->why ? e->why : ""});
    ++*static_cast<int*>(user);
}

struct Fixture {
    FakeWorld w;
    SingletonStore singles;
    EventStore events;
    SystemPrograms programs;
    std::vector<std::function<void(EmfWorld*)>> after;
    BuffService service;
    FakeProgramCtx ctx;
    int user = 0;
    explicit Fixture(bool with_core = true) : service(deps(with_core)), ctx(w.p()) {
        programs.reset();
        g_core = {};
        g_log.clear();
        g_got.clear();
        g_by_acc.clear();
        g_by_hash.clear();
        g_box.clear();
        g_writes = 0;
        g_bad_events = false;
        g_now = 1000;
        plant(acc(kContainer), gen::BuffContainer::kHash, 1, g_box[1].b);
        plant(acc(kContainer), gen::BuffContainer::kHash, 2000, g_box[2000].b);
        plant(acc(kFaction), gen::Faction::kHash, 1, &g_faction_player);
        plant(acc(kFaction), gen::Faction::kHash, 2000, &g_faction_wolf);
        singles.plant(w, "BuffRegistry", g_reg.reg);
        events.attach(w);
        events.plant(w, "AppliedBuffEvent", 4, 24);
        events.plant(w, "NotifyEvent", 7, 224);
        w.put<uint8_t>(off::kKind, 0);
        void* wctx = &user;
        std::memcpy(ctx.job + off::kJobWorldCtx, &wctx, 8);
        void* cs = g_change_set;
        std::memcpy(ctx.job + off::kJobChangeSet, &cs, 8);
        REQUIRE(programs.add(service.spec(), /*internal=*/true));
        REQUIRE(programs.register_into(w.p(), f_register) == 1);
        set_program_iter_begin(f_iter_begin);
    }
    ~Fixture() {
        set_program_iter_begin(nullptr);
        programs.reset();
    }
    BuffService::Deps deps(bool with_core) {
        BuffService::Deps d;
        d.apply = with_core ? f_apply : nullptr;
        d.read_other = f_read_other;
        d.write_other = f_write_other;
        d.get_component = f_get_component;
        d.log = [](const std::string& s) { g_log.push_back(s); };
        d.after_step = [this](std::function<void(EmfWorld*)> fn) { after.push_back(std::move(fn)); };
        d.now_ms = [] { return g_now; };
        return d;
    }
    EmfWorld* world() { return reinterpret_cast<EmfWorld*>(w.p()); }
    bool give(uint32_t target, uint32_t type, int64_t ns = 0, uint32_t source = 0, std::string* why = nullptr) {
        BuffRequest r;
        r.world = world();
        r.target = target;
        r.type = type;
        r.duration_ns = ns;
        r.source = source;
        r.done = on_done;
        r.user = &user;
        std::string w2;
        return service.request(r, why ? why : &w2);
    }
    bool take(uint32_t target, uint32_t type) {
        BuffRequest r;
        r.world = world();
        r.remove = true;
        r.target = target;
        r.type = type;
        r.done = on_done;
        r.user = &user;
        std::string why;
        return service.request(r, &why);
    }
    void tick() {
        system_program_desc(0).update(ctx.p());
        auto jobs = std::move(after);
        after.clear();
        for (auto& j : jobs) j(world());
    }
};

}  // namespace

TEST_CASE("buff service: emf_buffs declares the buff programs' access, singletons in name order") {
    Fixture f;
    const ProgramSpec s = f.service.spec();
    CHECK(s.name == "emf_buffs");
    CHECK(s.world_kind == EMF_WORLD_SERVER);
    CHECK(s.singletons_read ==
          std::vector<std::string>{"BalancingRegistry", "BuffRegistry", "g38_items::ItemRegistry", "g38_skilltree::SkillTree"});
    CHECK(s.other_write == std::vector<std::string>{"BuffContainer", "LifeTime"});
    CHECK(s.other_read.size() == 14);
    CHECK(s.events_out == std::vector<std::string>{"AppliedBuffEvent", "NotifyEvent"});
    const IterLayout l = f.service.layout();
    CHECK(l.other_base == 5);
    CHECK(l.events_in_base == 21);
}

TEST_CASE("buff service: a give calls applyBuffCore with the record, the target as source and the ctx from the iterator") {
    Fixture f;
    REQUIRE(f.give(1, kWet, 30'000'000'000LL));
    f.tick();
    REQUIRE(g_core.calls == 1);
    CHECK(g_core.target == 1);
    CHECK(g_core.source == 1);  // no source: the target (what the game's own programs pass)
    CHECK(g_core.type == g_reg.rec(0));
    CHECK(get<uint64_t>(g_core.ctx, 0x00) == reinterpret_cast<uint64_t>(f.ctx.p()));
    CHECK(get<uint64_t>(g_core.ctx, 0x28) == acc(kContainer));
    CHECK(get<uint64_t>(g_core.ctx, 0x30) == acc(kLifeTime));
    CHECK(get<uint64_t>(g_core.ctx, 0x40) == reinterpret_cast<uint64_t>(g_skilltree));
    CHECK(get<int64_t>(g_core.ctx, 0x60) == 30'000'000'000LL);
    CHECK(get<uint64_t>(g_core.ctx, 0x68) == 0);  // no source stats
    CHECK(get<uint64_t>(g_core.ctx, 0xC0) == acc(kFaction));
    CHECK(get<uint64_t>(g_core.ctx, 0xD0) == reinterpret_cast<uint64_t>(g_balancing));
    void* job = nullptr;
    std::memcpy(&job, f.ctx.p(), 8);
    CHECK(get<uint64_t>(g_core.ctx, 0xD8) == reinterpret_cast<uint64_t>(job));
    CHECK(get<uint64_t>(g_core.ctx, 0xE0) == reinterpret_cast<uint64_t>(job));
    REQUIRE(g_got.size() == 1);
    CHECK(g_got[0].kind == EMF_BUFF_APPLIED);
    CHECK(g_got[0].target == 1);
    CHECK(g_got[0].type == kWet);
    CHECK(g_got[0].buff_entity == 7001);
    CHECK(f.user == 1);
}

TEST_CASE("buff service: a source copies its stats onto the buff (ImpactHierarchy .. Level accessors)") {
    Fixture f;
    REQUIRE(f.give(1, kElixir, 0, 2000));
    f.tick();
    CHECK(g_core.source == 2000);
    CHECK(get<uint64_t>(g_core.ctx, 0x68) == acc(kImpactHierarchy));
    CHECK(get<uint64_t>(g_core.ctx, 0xB8) == acc(kLevel));
}

TEST_CASE("buff service: refusals: an unknown type, no container, an excluded faction, a full group") {
    Fixture f;
    REQUIRE(f.give(1, 0x12345678u));
    REQUIRE(f.give(555, kWet));  // no container
    g_core.silent = true;
    REQUIRE(f.give(2000, kPoisoned));  // players only: the core says ok and does nothing
    f.tick();
    REQUIRE(g_got.size() == 3);
    CHECK(g_got[0].kind == EMF_BUFF_REFUSED);
    CHECK(g_got[0].why.find("unknown buff type") != std::string::npos);
    CHECK(g_got[1].why.find("no buff container") != std::string::npos);
    CHECK(g_got[2].why.find("faction") != std::string::npos);
    CHECK(g_core.calls == 1);  // only the wolf reached the game
    g_core.silent = false;
    g_core.err = 0x14;
    REQUIRE(f.give(1, kWet));
    f.tick();
    CHECK(g_got.back().why.find("no free debuff slot") != std::string::npos);
}

TEST_CASE("buff service: a remove cleanses the first instance through the write accessor; not there: nothing written") {
    Fixture f;
    put<uint32_t>(g_box[1].b, 0xF0 + 2 * 12, 6001);
    put<uint32_t>(g_box[1].b, 0xF0 + 2 * 12 + 4, kWet);
    REQUIRE(f.take(1, kPoisoned));
    f.tick();
    CHECK(g_writes == 0);
    REQUIRE(g_got.size() == 1);
    CHECK(g_got[0].kind == EMF_BUFF_REFUSED);
    CHECK(g_got[0].why.find("does not have") != std::string::npos);
    REQUIRE(f.take(1, kWet));
    f.tick();
    CHECK(g_writes == 1);
    CHECK(g_box[1].b[0xF0 + 2 * 12 + 8] == 1);
    REQUIRE(g_got.size() == 2);
    CHECK(g_got[1].kind == EMF_BUFF_REMOVED);
    CHECK(g_got[1].buff_entity == 6001);
    CHECK(g_core.calls == 0);  // removing never calls applyBuffCore
}

TEST_CASE("buff service: request checks, the limit, and giving refused without the signature (removing still works)") {
    Fixture f(/*with_core=*/false);
    std::string why;
    CHECK_FALSE(f.give(1, kWet, 0, 0, &why));
    CHECK(why.find("not available") != std::string::npos);
    CHECK(f.take(1, kWet));
    BuffRequest r;
    r.world = f.world();
    r.remove = true;
    CHECK_FALSE(f.service.request(r, &why));  // entity 0
    r.target = 1;
    CHECK_FALSE(f.service.request(r, &why));  // type 0
    r.type = kWet;
    for (int i = 1; i < 64; ++i) CHECK(f.service.request(r, &why));
    CHECK_FALSE(f.service.request(r, &why));
    CHECK(why.find("64") != std::string::npos);
    BuffRequest none;
    none.target = 1;
    none.type = kWet;
    CHECK_FALSE(f.service.request(none, &why));  // no world
}

TEST_CASE("buff service: 16 per step; a new world refuses what waits; a request for another world is refused") {
    Fixture f;
    for (int i = 0; i < 20; ++i) REQUIRE(f.give(1, kElixir));
    f.tick();
    CHECK(g_core.calls == 16);
    CHECK(f.service.pending() == 4);
    f.service.world_reset();
    for (auto& j : f.after) j(f.world());
    f.after.clear();
    CHECK(f.service.pending() == 0);
    CHECK(g_got.back().why.find("new world") != std::string::npos);
    FakeWorld other;
    BuffRequest r;
    r.world = reinterpret_cast<EmfWorld*>(other.p());
    r.target = 1;
    r.type = kWet;
    r.done = on_done;
    r.user = &f.user;
    std::string why;
    REQUIRE(f.service.request(r, &why));
    const int before = g_core.calls;
    f.tick();
    CHECK(g_core.calls == before);
    CHECK(g_got.back().why.find("world") != std::string::npos);
}

TEST_CASE("buff service: a faulting applyBuffCore turns giving off for the session; events-out slots are checked") {
    Fixture f;
    g_core.crash = true;
    REQUIRE(f.give(1, kWet));
    f.tick();
    CHECK(g_got.back().kind == EMF_BUFF_REFUSED);
    CHECK(g_got.back().why.find("fault") != std::string::npos);
    std::string why;
    CHECK_FALSE(f.give(1, kWet, 0, 0, &why));
    CHECK(why.find("off") != std::string::npos);
    Fixture g;
    g_bad_events = true;
    REQUIRE(g.give(1, kWet));
    g.tick();
    CHECK(g_core.calls == 0);
    CHECK(g_got.back().why.find("job") != std::string::npos);
}

TEST_CASE("buff service: requests end refused when emf_buffs has not run in the server world for 2 s") {
    Fixture f;
    REQUIRE(f.give(1, kWet));
    f.service.on_server_tick(f.world());
    g_now += 2500;
    f.service.on_server_tick(f.world());
    for (auto& j : f.after) j(f.world());
    CHECK(f.service.pending() == 0);
    REQUIRE(g_got.size() == 1);
    CHECK(g_got[0].why.find("not running") != std::string::npos);
}

TEST_CASE("buff service: every singleton slot must be the world's own (polish)") {
    Fixture f;
    alignas(8) static uint8_t other_balancing[8];
    f.singles.plant(f.w, "BalancingRegistry", other_balancing);  // the slot holds g_balancing: a shifted layout
    REQUIRE(f.give(1, kWet));
    f.tick();
    CHECK(g_core.calls == 0);
    REQUIRE(!g_got.empty());
    CHECK(g_got.back().why.find("iterator layout") != std::string::npos);
}
