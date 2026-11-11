// Hits before they land (API v31): what the detour on the game's applyHitToEntity does with one call, game-free
// (private research hit_hooks_2026-11-11 section 2.1): server world and entity hits only, once per hit over the
// propagated levels, the rules first, then the mods' before-callbacks (a faulting one switched off), the hit written
// back only when something changed.
#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "core/hit_hook.h"
#include "emf/generated/components.hpp"

using namespace emf::core;

namespace {

using HC = emf::gen::ecs::HitCandidate;

template <class T>
void put_at(uint8_t* mem, size_t off, T v) {
    std::memcpy(mem + off, &v, sizeof v);
}
template <class T>
T get_at(const uint8_t* mem, size_t off) {
    T v{};
    std::memcpy(&v, mem + off, sizeof v);
    return v;
}

std::array<uint8_t, 0x150> candidate(int32_t health_change, uint32_t target = 31) {
    std::array<uint8_t, 0x150> b{};
    put_at<uint32_t>(b.data(), offsetof(HC, rootSourceId), 2000);
    put_at<uint32_t>(b.data(), offsetof(HC, sourceId), 2000);
    put_at<uint32_t>(b.data(), offsetof(HC, targetId), target);
    put_at<int32_t>(b.data(), offsetof(HC, healthChange), health_change);
    put_at<uint32_t>(b.data(), offsetof(HC, impactId), 55);
    put_at<uint8_t>(b.data(), offsetof(HC, sendHitEvent), 1);
    put_at<uint16_t>(b.data(), offsetof(HC, damageTypes), 1);
    return b;
}

struct Fake {
    int32_t health = 100;
    bool applies = true;
    uint32_t no_health = 0;  // an entity without Health (a part that passes hits up)
};
bool f_applies(uint32_t, void* c) { return static_cast<Fake*>(c)->applies; }
int32_t f_health(uint32_t e, void* c) {
    auto* f = static_cast<Fake*>(c);
    return e == f->no_health ? -1 : f->health;
}
uint32_t f_weapon(uint32_t, void*) { return 0; }
HitClass f_classify(uint32_t id, void*) {
    return id >= 1 && id <= 0x3FE ? HitClass::Player : id ? HitClass::Creature : HitClass::None;
}

PreHitEnv env_for(Fake& f, uint8_t kind = EMF_WORLD_SERVER) {
    PreHitEnv e;
    e.world_kind = kind;
    e.ctx = &f;
    e.classify = &f_classify;
    e.applies = &f_applies;
    e.health = &f_health;
    e.weapon_item = &f_weapon;
    return e;
}

EmfHitRule rule() {
    EmfHitRule r{};
    r.size = sizeof r;
    r.scale = 1.0f;
    return r;
}

struct Rig {
    Fake fake;
    HitOnce once;
    HitRules rules;
    HitCallbacks cbs;
    HitStats st;
    std::vector<std::string> log;
    Rig() {
        cbs.set_log([this](const std::string& s) { log.push_back(s); });
    }
    bool run(uint8_t* hc, uint8_t kind = EMF_WORLD_SERVER) { return pre_hit(hc, env_for(fake, kind), once, rules, cbs, st); }
};

int g_seen_amount = 0;
int g_second_calls = 0;
void cb_set3(EmfWorld*, EmfHit* h, void*) {
    g_seen_amount = h->amount;
    h->amount = 3;
}
void cb_fault(EmfWorld*, EmfHit*, void*) {
    volatile int* p = nullptr;
    *p = 1;
}
void cb_count(EmfWorld*, EmfHit*, void*) { ++g_second_calls; }
void cb_after(EmfWorld*, const EmfHit*, void*) {}

}  // namespace

TEST_CASE("pre_hit does nothing on a client world, for a terrain hit, or with no rules and no callbacks") {
    Rig r;
    auto b = candidate(-40);
    const auto orig = b;
    CHECK_FALSE(r.run(b.data()));  // no rules, no callbacks
    EmfHitRule half = rule();
    half.scale = 0.5f;
    std::string why;
    REQUIRE(r.rules.add(half, &why) == 1);
    CHECK_FALSE(r.run(b.data(), EMF_WORLD_CLIENT));
    auto terrain = candidate(-40);
    put_at<uint8_t>(terrain.data(), offsetof(HC, candidateType), 1);
    const auto terrain_orig = terrain;
    CHECK_FALSE(r.run(terrain.data()));
    CHECK(b == orig);
    CHECK(terrain == terrain_orig);
    CHECK(r.st.seen.load() == 0);
}

TEST_CASE("pre_hit applies a rule and writes the hit back") {
    Rig r;
    EmfHitRule half = rule();
    half.scale = 0.5f;
    std::string why;
    REQUIRE(r.rules.add(half, &why) == 1);
    auto b = candidate(-40);
    CHECK(r.run(b.data()));
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, healthChange)) == -20);
    CHECK(r.st.seen.load() == 1);
    CHECK(r.st.changed.load() == 1);
    CHECK(r.st.cancelled.load() == 0);
}

TEST_CASE("pre_hit reads health and keeps min_health") {
    Rig r;
    r.fake.health = 30;
    EmfHitRule keep = rule();
    keep.min_health = 1;
    std::string why;
    REQUIRE(r.rules.add(keep, &why) == 1);
    auto b = candidate(-50);
    CHECK(r.run(b.data()));
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, healthChange)) == -29);
}

TEST_CASE("pre_hit fires once over the propagated levels") {
    Rig r;
    EmfHitRule half = rule();
    half.scale = 0.5f;
    std::string why;
    REQUIRE(r.rules.add(half, &why) == 1);
    auto b = candidate(-40, 31);
    CHECK(r.run(b.data()));
    r.once.note_propagate(true);  // the game said: pass it up
    put_at<uint32_t>(b.data(), offsetof(HC, targetId), 30);  // the conclusion's next level: the parent
    put_at<uint32_t>(b.data(), offsetof(HC, propagatedFromEntityId), 31);
    CHECK_FALSE(r.run(b.data()));
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, healthChange)) == -20);
    CHECK(r.st.seen.load() == 1);
}

TEST_CASE("a before-callback sees the rules' result and may change it") {
    Rig r;
    EmfHitRule plus = rule();
    plus.add = 5;
    std::string why;
    REQUIRE(r.rules.add(plus, &why) == 1);
    REQUIRE(r.cbs.add_before(&cb_set3, nullptr, "test"));
    auto b = candidate(-10);
    CHECK(r.run(b.data()));
    CHECK(g_seen_amount == 15);
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, healthChange)) == -3);
}

TEST_CASE("a faulting before-callback is switched off, the next one still runs") {
    Rig r;
    g_second_calls = 0;
    REQUIRE(r.cbs.add_before(&cb_fault, nullptr, "bad_mod.dll"));
    REQUIRE(r.cbs.add_before(&cb_count, nullptr, "good_mod.dll"));
    auto b = candidate(-10);
    r.run(b.data());
    auto c = candidate(-10);
    put_at<uint32_t>(c.data(), offsetof(HC, impactId), 56);
    r.run(c.data());
    CHECK(g_second_calls == 2);
    CHECK(r.st.faults.load() == 1);
    CHECK(r.cbs.before_count() == 2);
    REQUIRE(r.log.size() == 1);
    CHECK(r.log[0].find("bad_mod.dll") != std::string::npos);
    CHECK(r.log[0].find("switched off") != std::string::npos);
}

TEST_CASE("cancel through a rule silences the hit") {
    Rig r;
    EmfHitRule off = rule();
    off.cancel = 1;
    std::string why;
    REQUIRE(r.rules.add(off, &why) == 1);
    auto b = candidate(-10);
    put_at<int32_t>(b.data(), offsetof(HC, staminaChange), -4);
    CHECK(r.run(b.data()));
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, healthChange)) == 0);
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, staminaChange)) == 0);
    CHECK(get_at<uint8_t>(b.data(), offsetof(HC, sendHitEvent)) == 0);
    CHECK(r.st.cancelled.load() == 1);
}

TEST_CASE("HitCallbacks: 16 at most each") {
    HitCallbacks cbs;
    for (int i = 0; i < 16; ++i) {
        CHECK(cbs.add_before(&cb_count, nullptr, "m"));
        CHECK(cbs.add_after(&cb_after, nullptr, "m"));
    }
    CHECK_FALSE(cbs.add_before(&cb_count, nullptr, "m"));
    CHECK_FALSE(cbs.add_after(&cb_after, nullptr, "m"));
    CHECK_FALSE(cbs.add_before(nullptr, nullptr, "m"));
    CHECK(cbs.before_count() == 16);
    CHECK(cbs.after_count() == 16);
}

TEST_CASE("pre_hit reads the actor's health when the part hit has none") {  // review I2
    Rig r;
    r.fake.health = 30;
    r.fake.no_health = 31;
    EmfHitRule keep = rule();
    keep.min_health = 1;
    std::string why;
    REQUIRE(r.rules.add(keep, &why) == 1);
    auto b = candidate(-50, 31);
    put_at<uint32_t>(b.data(), offsetof(HC, rootTargetId), 30);
    CHECK(r.run(b.data()));
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, healthChange)) == -29);
}
