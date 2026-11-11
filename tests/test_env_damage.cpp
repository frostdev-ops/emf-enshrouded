// Fall, shroud, cold and drowning damage (feature 4b, private research env_damage_2026-11-11): the program hooks'
// game-free part: the slice's entities from the job, Health before and after the program, one env hit per drop, the
// rules and before-callbacks on it, Health put right when the amount shrank, the after-queue.
#include <doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/env_damage.h"

using namespace emf::core;

namespace {

std::map<uint32_t, int32_t> g_health;
std::map<uint32_t, int32_t> g_max;
std::vector<std::pair<uint32_t, int32_t>> g_adds;

int32_t f_health(uint32_t e, void*) {
    const auto it = g_health.find(e);
    return it == g_health.end() ? -1 : it->second;
}
int32_t f_max(uint32_t e, void*) {
    const auto it = g_max.find(e);
    return it == g_max.end() ? -1 : it->second;
}
int32_t f_add(uint32_t e, int32_t delta, void*) {
    g_adds.emplace_back(e, delta);
    g_health[e] += delta;
    return g_health[e];
}
HitClass f_classify(uint32_t id, void*) { return id >= 1 && id <= 0x3FE ? HitClass::Player : HitClass::Creature; }

struct Rig {
    HitRules rules;
    HitCallbacks cbs;
    HitStats st;
    HitCarry carry;
    EnvEnv env;
    Rig() {
        g_health.clear();
        g_max.clear();
        g_adds.clear();
        env.classify = &f_classify;
        env.add_health = &f_add;
        env.max_health = &f_max;
    }
    bool apply(uint8_t kind, uint32_t entity, int32_t before, int32_t after, EmfHit* out) {
        g_health[entity] = after;
        return env_apply(kind, EnvDrop{entity, before, after}, env, rules, cbs, st, carry, out);
    }
};

EmfHitRule rule(uint8_t kinds = EMF_HIT_KINDS_ALL) {
    EmfHitRule r{};
    r.size = sizeof r;
    r.scale = 1.0f;
    r.kinds = kinds;
    return r;
}

int g_seen_amount = 0;
void cb_three(EmfWorld*, EmfHit* h, void*) {
    g_seen_amount = h->amount;
    h->amount = 3;
}

}  // namespace

TEST_CASE("env slice: the job's entity list, 16 bytes per entity, the id first") {
    uint8_t list[3 * 16] = {};
    const uint32_t ids[3] = {7, 300, 9001};
    for (int i = 0; i < 3; ++i) std::memcpy(list + i * 16, &ids[i], 4);
    const EnvSlice s{list, 3};
    CHECK(env_slice_entity(s, 0) == 7);
    CHECK(env_slice_entity(s, 2) == 9001);
    CHECK(env_slice_entity(s, 3) == 0);  // past the end
    CHECK(env_slice_entity(EnvSlice{}, 0) == 0);
}

TEST_CASE("env snapshot: one drop per entity whose health fell; none for a rise or no change; only for its own job") {
    g_health = {{5, 100}, {6, 80}, {7, 50}};
    uint8_t list[3 * 16] = {};
    const uint32_t ids[3] = {5, 6, 7};
    for (int i = 0; i < 3; ++i) std::memcpy(list + i * 16, &ids[i], 4);
    const EnvSlice s{list, 3};
    int job = 0, other = 0;
    EnvSnapshot snap;
    snap.pre(&job, s, &f_health, nullptr);
    g_health[5] = 90;
    g_health[7] = 55;  // a rise (cannot happen inside these programs, but must not count)
    CHECK(snap.post(&other, s, &f_health, nullptr).empty());
    snap.pre(&job, s, &f_health, nullptr);
    g_health[5] = 70;
    const auto drops = snap.post(&job, s, &f_health, nullptr);
    REQUIRE(drops.size() == 1);
    CHECK(drops[0].entity == 5);
    CHECK(drops[0].before == 90);
    CHECK(drops[0].after == 70);
    CHECK(snap.post(&job, s, &f_health, nullptr).empty());  // taken once
}

TEST_CASE("env apply: a drop with no rules is delivered as it is, nothing put back") {
    Rig r;
    EmfHit h{};
    CHECK(r.apply(EMF_HIT_KIND_FALL, 3, 100, 60, &h));
    CHECK(h.kind == EMF_HIT_KIND_FALL);
    CHECK(h.target == 3);
    CHECK(h.part == 3);
    CHECK(h.attacker == 0);
    CHECK(h.amount == 40);
    CHECK(h.health_before == 100);
    CHECK(h.health_after == 60);
    CHECK(g_adds.empty());
    CHECK(r.st.seen.load() == 1);
}

TEST_CASE("env apply: a v31 rule (kinds 0) leaves env damage alone; a cancel for all kinds puts it all back") {
    Rig r;
    EmfHitRule hits_only = rule(0);
    hits_only.cancel = 1;
    std::string why;
    REQUIRE(r.rules.add(hits_only, &why) != 0);
    EmfHit h{};
    CHECK(r.apply(EMF_HIT_KIND_COLD, 3, 100, 98, &h));
    CHECK(g_adds.empty());
    EmfHitRule all = rule();
    all.cancel = 1;
    REQUIRE(r.rules.add(all, &why) != 0);
    CHECK_FALSE(r.apply(EMF_HIT_KIND_COLD, 3, 98, 96, &h));  // cancelled: not delivered
    REQUIRE(g_adds.size() == 1);
    CHECK(g_adds[0].second == 2);
    CHECK(g_health[3] == 98);
    CHECK(r.st.cancelled.load() == 1);
}

TEST_CASE("env apply: min_health keeps a lethal fall at 1") {
    Rig r;
    EmfHitRule keep = rule(1u << EMF_HIT_KIND_FALL);
    keep.target = EMF_HIT_PLAYERS;
    keep.min_health = 1;
    std::string why;
    REQUIRE(r.rules.add(keep, &why) != 0);
    EmfHit h{};
    CHECK(r.apply(EMF_HIT_KIND_FALL, 1, 30, -5, &h));
    CHECK(h.amount == 29);
    CHECK(g_health[1] == 1);
    CHECK(h.health_after == 1);
}

TEST_CASE("env apply: a before-callback sees the env hit and its change is put right") {
    Rig r;
    REQUIRE(r.cbs.add_before(&cb_three, nullptr, "test"));
    EmfHit h{};
    CHECK(r.apply(EMF_HIT_KIND_SHROUD, 2000, 100, 90, &h));
    CHECK(g_seen_amount == 10);
    CHECK(h.amount == 3);
    CHECK(g_health[2000] == 97);
}

TEST_CASE("env apply: half of 1-point drowning ticks is 1 every second tick") {
    Rig r;
    EmfHitRule half = rule();
    half.scale = 0.5f;
    std::string why;
    REQUIRE(r.rules.add(half, &why) != 0);
    int32_t health = 100;
    int total = 0;
    for (int i = 0; i < 10; ++i) {
        EmfHit h{};
        r.apply(EMF_HIT_KIND_DROWNING, 1, health, health - 1, &h);
        health = g_health[1];
        total += h.amount;
    }
    CHECK(total == 5);
    CHECK(health == 95);
}

TEST_CASE("env apply: instant when deadly shroud or a creature out of air takes a quarter of max health or more at once") {
    Rig r;
    g_max[3] = 100;
    g_max[2000] = 100;
    EmfHit h{};
    r.apply(EMF_HIT_KIND_SHROUD, 3, 80, 0, &h);
    CHECK((h.flags & EMF_HIT_INSTANT) != 0);
    r.apply(EMF_HIT_KIND_DROWNING, 2000, 1, -1, &h);  // the last tick of drowning: not instant
    CHECK((h.flags & EMF_HIT_INSTANT) == 0);
    r.apply(EMF_HIT_KIND_FALL, 3, 80, 0, &h);  // a fall is never instant
    CHECK((h.flags & EMF_HIT_INSTANT) == 0);
}

TEST_CASE("env queue: pushed on a worker, taken after the step, capped") {
    EnvQueue q;
    EmfHit h{};
    h.amount = 4;
    q.push(h);
    q.push(h);
    CHECK(q.size() == 2);
    const auto taken = q.take();
    CHECK(taken.size() == 2);
    CHECK(q.size() == 0);
    for (size_t i = 0; i < EnvQueue::kMax + 10; ++i) q.push(h);
    CHECK(q.size() == EnvQueue::kMax);
    CHECK(q.dropped() == 10);
}
