// Fall, shroud, cold and drowning damage (feature 4b, private research env_damage_2026-11-11): the program hooks'
// game-free part: the slice's entities from the job, Health before and after the program, one env hit per drop, the
// rules and before-callbacks on it, Health put right when the amount shrank, the after-queue.
#include <doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "core/env_damage.h"

using namespace emf::core;

namespace {

std::map<uint32_t, int32_t> g_health;
std::map<uint32_t, int32_t> g_max;
std::vector<std::pair<uint32_t, int32_t>> g_adds;

int32_t f_health(uint32_t e, void*) {
    const auto it = g_health.find(e);
    return it == g_health.end() ? kEnvNoHealth : it->second;
}
std::vector<uint32_t> g_deadly;  // entities standing in deadly shroud (EnvironmentSensor.fogType 3)
bool f_deadly(uint32_t e, void*) {
    for (uint32_t d : g_deadly)
        if (d == e) return true;
    return false;
}
bool g_add_fails = false;
bool f_add(uint32_t e, int32_t delta, void*, int32_t* now) {
    if (g_add_fails) return false;
    g_adds.emplace_back(e, delta);
    g_health[e] += delta;
    *now = g_health[e];
    return true;
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
        g_deadly.clear();
        g_add_fails = false;
        env.classify = &f_classify;
        env.add_health = &f_add;
        env.deadly_fog = &f_deadly;
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

TEST_CASE("env apply: instant is deadly shroud (fog type 3) or a creature out of air, whatever health it had (review I3)") {
    Rig r;
    g_max[3] = 100;
    g_max[5] = 100;
    g_max[2000] = 100;
    g_deadly = {3};
    EmfHit h{};
    r.apply(EMF_HIT_KIND_SHROUD, 3, 20, 0, &h);  // already hurt, then deadly shroud
    CHECK((h.flags & EMF_HIT_INSTANT) != 0);
    r.apply(EMF_HIT_KIND_SHROUD, 5, 30, 0, &h);  // a long step's killing tick in dangerous shroud
    CHECK((h.flags & EMF_HIT_INSTANT) == 0);
    r.apply(EMF_HIT_KIND_DROWNING, 2000, 10, 0, &h);  // a creature out of air dies at once
    CHECK((h.flags & EMF_HIT_INSTANT) != 0);
    r.apply(EMF_HIT_KIND_DROWNING, 5, 30, 0, &h);  // a player drowns tick by tick
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

TEST_CASE("env carry: two programs on two threads keep exact carries for their own entities (review I10)") {
    HitRules rules;
    EmfHitRule half = rule();
    half.scale = 0.5f;
    std::string why;
    REQUIRE(rules.add(half, &why) != 0);
    HitCarry carry;
    auto run = [&](uint32_t first, int64_t* total) {
        for (int round = 0; round < 40; ++round)
            for (uint32_t e = first; e < first + 3000; ++e) {
                EmfHit h{};
                h.size = sizeof h;
                h.kind = EMF_HIT_KIND_COLD;
                h.target = e;
                h.amount = 1;
                h.health_before = 100;
                rules.apply(h, &f_classify, nullptr, &carry);
                *total += h.amount;
            }
    };
    int64_t a = 0, b = 0;
    std::thread t1(run, 0x1000u, &a), t2(run, 0x100000u, &b);
    t1.join();
    t2.join();
    CHECK(a == 3000 * 40 / 2);
    CHECK(b == 3000 * 40 / 2);
    CHECK(carry.size() == 6000);
}

// ---- the review's fixes ----

TEST_CASE("env snapshot: a health below 0 after the program is a drop: shroud, cold and drowning subtract without a floor (review I1)") {
    g_health = {{5, 1}, {6, 40}};
    uint8_t list[3 * 16] = {};
    const uint32_t ids[3] = {5, 6, 7};  // 7 has no health
    for (int i = 0; i < 3; ++i) std::memcpy(list + i * 16, &ids[i], 4);
    const EnvSlice s{list, 3};
    int job = 0;
    EnvSnapshot snap;
    snap.pre(&job, s, &f_health, nullptr);
    g_health[5] = -1;  // a 2-point tick on 1 health
    g_health[6] = -3;
    const auto drops = snap.post(&job, s, &f_health, nullptr);
    REQUIRE(drops.size() == 2);
    CHECK(drops[0].entity == 5);
    CHECK(drops[0].before == 1);
    CHECK(drops[0].after == -1);
    CHECK(drops[1].after == -3);
}

TEST_CASE("env apply: min_health holds a tick that went below 0 at 1 (review I1)") {
    Rig r;
    g_max[5] = 100;
    EmfHitRule keep = rule();
    keep.min_health = 1;
    std::string why;
    REQUIRE(r.rules.add(keep, &why) != 0);
    EmfHit h{};
    CHECK(r.apply(EMF_HIT_KIND_COLD, 5, 1, -1, &h));
    CHECK(g_health[5] == 1);
    CHECK(h.amount == 0);
    CHECK(h.health_after == 1);
}

TEST_CASE("env apply: a correction that fails is reported as what happened, not as done (review I7)") {
    Rig r;
    g_max[5] = 100;
    EmfHitRule cancel = rule();
    cancel.cancel = 1;
    std::string why;
    REQUIRE(r.rules.add(cancel, &why) != 0);
    g_add_fails = true;
    EmfHit h{};
    CHECK(r.apply(EMF_HIT_KIND_FALL, 5, 100, 60, &h));  // the damage landed: on_hit gets it
    CHECK(h.cancel == 0);
    CHECK(h.amount == 40);
    CHECK(h.health_after == 60);
    CHECK(r.st.changed.load() == 0);
    CHECK(r.st.cancelled.load() == 0);
    CHECK(r.st.faults.load() == 1);
}


TEST_CASE("env apply: a drop that leaves no health says killed; one a rule kept alive does not (polish)") {
    Rig r;
    EmfHit h{};
    r.apply(EMF_HIT_KIND_FALL, 5, 30, 0, &h);
    CHECK((h.flags & EMF_HIT_KILLED) != 0);
    EmfHitRule keep = rule();
    keep.min_health = 1;
    std::string why;
    REQUIRE(r.rules.add(keep, &why) != 0);
    r.apply(EMF_HIT_KIND_FALL, 6, 30, -5, &h);
    CHECK((h.flags & EMF_HIT_KILLED) == 0);
    CHECK(h.health_after == 1);
}
