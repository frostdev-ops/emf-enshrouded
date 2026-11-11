// Hits (API v31): the rule engine the core applies to each landing hit before it lands (private research
// hit_hooks_2026-11-11, spec 2026-11-11-hit-hooks-design: match by attacker, target, flags, types and attack kind;
// cancel, scale, add, min_health; damage hits only unless a rule says healing).
#include <doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "core/hits.h"
#include "emf/generated/components.hpp"

using namespace emf::core;

namespace {

EmfHit hit(int32_t amount, uint32_t attacker = 7, uint32_t target = 1) {
    EmfHit h{};
    h.size = sizeof h;
    h.attacker = attacker;
    h.source = attacker;
    h.target = target;
    h.part = target;
    h.amount = amount;
    h.damage_types = 1;
    h.health_before = 100;
    h.health_after = -1;
    if (amount < 0) h.flags |= EMF_HIT_HEALING;
    return h;
}

EmfHitRule rule() {
    EmfHitRule r{};
    r.size = sizeof r;
    r.scale = 1.0f;
    return r;
}

// players 1..0x3FE, 500 an object, everything else a creature
HitClass classify(uint32_t id, void*) {
    if (id == 500) return HitClass::Object;
    if (id >= 1 && id <= 0x3FE) return HitClass::Player;
    return id ? HitClass::Creature : HitClass::None;
}

bool apply(std::vector<EmfHitRule> rules, EmfHit& h) { return apply_hit_rules(rules, h, &classify, nullptr); }

}  // namespace

TEST_CASE("a scale rule halves damage, rounding toward zero") {
    EmfHitRule r = rule();
    r.scale = 0.5f;
    EmfHit h = hit(15);
    CHECK(apply({r}, h));
    CHECK(h.amount == 7);
    CHECK(h.health_after == 93);
    CHECK(h.cancel == 0);
}

TEST_CASE("rules apply in id order: scale then add") {
    EmfHitRule a = rule();
    a.scale = 2.0f;
    EmfHitRule b = rule();
    b.add = 5;
    EmfHit h = hit(10);
    apply({a, b}, h);
    CHECK(h.amount == 25);
    EmfHit g = hit(10);
    apply({b, a}, g);
    CHECK(g.amount == 30);
    HitRules set;
    std::string why;
    CHECK(set.add(b, &why) == 1);
    CHECK(set.add(a, &why) == 2);
    EmfHit k = hit(10);
    CHECK(set.apply(k, &classify, nullptr));
    CHECK(k.amount == 30);
}

TEST_CASE("cancel wins") {
    EmfHitRule a = rule();
    a.scale = 2.0f;
    EmfHitRule c = rule();
    c.cancel = 1;
    EmfHit h = hit(10);
    CHECK(apply({a, c}, h));
    CHECK(h.cancel == 1);
}

TEST_CASE("a damage hit never turns into healing, a heal never into damage") {
    EmfHitRule r = rule();
    r.add = -50;
    EmfHit h = hit(10);
    apply({r}, h);
    CHECK(h.amount == 0);
    EmfHitRule heal = rule();
    heal.healing = 1;
    heal.add = 50;
    EmfHit g = hit(-10);
    apply({heal}, g);
    CHECK(g.amount == 0);
}

TEST_CASE("min_health keeps the target alive") {
    EmfHitRule r = rule();
    r.min_health = 1;
    EmfHit h = hit(50);
    h.health_before = 30;
    apply({r}, h);
    CHECK(h.amount == 29);
    CHECK(h.health_after == 1);
    EmfHitRule r10 = rule();
    r10.min_health = 10;
    EmfHit g = hit(50);
    g.health_before = 30;
    apply({r, r10}, g);
    CHECK(g.amount == 20);
    EmfHit low = hit(50);
    low.health_before = 5;  // already below the floor: no damage, never a heal
    apply({r10}, low);
    CHECK(low.amount == 0);
    CHECK(low.health_after == 5);
}

TEST_CASE("min_health with unknown health changes nothing") {
    EmfHitRule r = rule();
    r.min_health = 1;
    EmfHit h = hit(50);
    h.health_before = -1;
    CHECK_FALSE(apply({r}, h));
    CHECK(h.amount == 50);
    CHECK(h.health_after == -1);
}

TEST_CASE("a damage rule leaves a healing hit alone") {
    EmfHitRule pvp = rule();
    pvp.attacker = EMF_HIT_PLAYERS;
    pvp.target = EMF_HIT_PLAYERS;
    pvp.cancel = 1;
    EmfHit heal = hit(-20, 5, 6);
    CHECK_FALSE(apply({pvp}, heal));
    CHECK(heal.cancel == 0);
    CHECK(heal.amount == -20);
    EmfHit dmg = hit(20, 5, 6);
    CHECK(apply({pvp}, dmg));
    CHECK(dmg.cancel == 1);
    pvp.healing = 1;
    CHECK(apply({pvp}, heal));
    CHECK(heal.cancel == 1);
}

TEST_CASE("classes, ids, flags, types and attacks match") {
    EmfHitRule r = rule();
    r.attacker = EMF_HIT_PLAYERS;
    CHECK(hit_rule_matches(r, hit(5, 5), &classify, nullptr));
    CHECK_FALSE(hit_rule_matches(r, hit(5, 2000), &classify, nullptr));
    CHECK_FALSE(hit_rule_matches(r, hit(5, 0), &classify, nullptr));  // no attacker: no class
    r = rule();
    r.attacker = EMF_HIT_CREATURES;
    CHECK(hit_rule_matches(r, hit(5, 2000), &classify, nullptr));
    r = rule();
    r.target = EMF_HIT_OBJECTS;
    CHECK(hit_rule_matches(r, hit(5, 7, 500), &classify, nullptr));
    CHECK_FALSE(hit_rule_matches(r, hit(5, 7, 1), &classify, nullptr));
    r = rule();
    r.target = 31;
    EmfHit part = hit(5, 7, 30);
    part.part = 31;
    CHECK(hit_rule_matches(r, part, &classify, nullptr));
    r.target = 30;
    CHECK(hit_rule_matches(r, part, &classify, nullptr));
    r.target = 32;
    CHECK_FALSE(hit_rule_matches(r, part, &classify, nullptr));
    r = rule();
    r.flags_all = EMF_HIT_CRIT;
    EmfHit crit = hit(5);
    CHECK_FALSE(hit_rule_matches(r, crit, &classify, nullptr));
    crit.flags |= EMF_HIT_CRIT | EMF_HIT_BACKSTAB;
    CHECK(hit_rule_matches(r, crit, &classify, nullptr));
    r = rule();
    r.damage_types_any = 1u << 3;
    EmfHit fire = hit(5);
    CHECK_FALSE(hit_rule_matches(r, fire, &classify, nullptr));
    fire.damage_types = (1u << 3) | 1u;
    CHECK(hit_rule_matches(r, fire, &classify, nullptr));
    r = rule();
    r.attacks = 1u << 1;
    EmfHit ranged = hit(5);
    CHECK_FALSE(hit_rule_matches(r, ranged, &classify, nullptr));
    ranged.attacks = 1u << 1;  // the game's attackCategory is a mask: bit per kind
    CHECK(hit_rule_matches(r, ranged, &classify, nullptr));
}

TEST_CASE("an attack rule matches any of the hit's attack kinds (both masks)") {
    EmfHitRule r = rule();
    r.attacks = (1u << 1) | (1u << 2);  // ranged or magic
    EmfHit h = hit(5);
    h.attacks = 1u << 0;  // melee
    CHECK_FALSE(hit_rule_matches(r, h, &classify, nullptr));
    h.attacks = (1u << 0) | (1u << 2);  // melee and magic
    CHECK(hit_rule_matches(r, h, &classify, nullptr));
    h.attacks = 0;  // no kind (a damage() hit)
    CHECK_FALSE(hit_rule_matches(r, h, &classify, nullptr));
}

TEST_CASE("a scaled blocked hit scales stamina, not health") {
    EmfHitRule r = rule();
    r.scale = 0.5f;
    EmfHit h = hit(0);
    h.stamina = 20;
    h.flags |= EMF_HIT_BLOCKED;
    CHECK(apply({r}, h));
    CHECK(h.amount == 0);
    CHECK(h.stamina == 10);
}

TEST_CASE("invalid rules are refused with a reason") {
    auto refused = [](const EmfHitRule& r) {
        std::string why;
        const bool ok = hit_rule_valid(r, &why);
        CAPTURE(why);
        return !ok && !why.empty();
    };
    EmfHitRule r = rule();
    r.scale = 0.5f;
    std::string why;
    CHECK(hit_rule_valid(r, &why));
    EmfHitRule tiny = r;
    tiny.size = 8;
    CHECK(refused(tiny));
    EmfHitRule neg = r;
    neg.scale = -1.0f;
    CHECK(refused(neg));
    EmfHitRule nan = r;
    nan.scale = std::numeric_limits<float>::quiet_NaN();
    CHECK(refused(nan));
    EmfHitRule big = r;
    big.scale = 101.0f;
    CHECK(refused(big));
    EmfHitRule bad_target = r;
    bad_target.target = 0xFFFFFFF5u;
    CHECK(refused(bad_target));
    EmfHitRule bad_attacker = r;
    bad_attacker.attacker = 0xFFFFFFF0u;
    CHECK(refused(bad_attacker));
    EmfHitRule bad_min = r;
    bad_min.min_health = -1;
    CHECK(refused(bad_min));
    EmfHitRule bad_add = r;
    bad_add.add = 2000000;
    CHECK(refused(bad_add));
    CHECK(refused(rule()));  // does nothing
    EmfHitRule heal_min = rule();
    heal_min.healing = 1;
    heal_min.min_health = 1;
    CHECK(refused(heal_min));
}

TEST_CASE("HitRules: ids, removal, 64 at most") {
    HitRules set;
    EmfHitRule r = rule();
    r.add = 1;
    std::string why;
    CHECK(set.add(r, &why) == 1);
    CHECK(set.add(r, &why) == 2);
    CHECK(set.remove(1));
    CHECK_FALSE(set.remove(1));
    CHECK(set.size() == 1);
    for (size_t i = 1; i < kMaxHitRules; ++i) CHECK(set.add(r, &why) != 0);
    CHECK(set.size() == kMaxHitRules);
    why.clear();
    CHECK(set.add(r, &why) == 0);
    CHECK_FALSE(why.empty());
    CHECK(set.remove(2));
    CHECK(set.add(r, &why) == kMaxHitRules + 2);  // ids are never reused
    EmfHitRule bad = rule();
    CHECK(set.add(bad, &why) == 0);
}

// ---- Task 2: the game's records and once per hit ----

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

std::array<uint8_t, 0x150> candidate() {
    std::array<uint8_t, 0x150> b{};
    uint8_t* m = b.data();
    put_at<uint32_t>(m, offsetof(HC, rootSourceId), 7);
    put_at<uint32_t>(m, offsetof(HC, sourceId), 9);
    put_at<uint32_t>(m, offsetof(HC, targetId), 31);
    put_at<uint32_t>(m, offsetof(HC, rootTargetId), 30);
    put_at<int32_t>(m, offsetof(HC, healthChange), -42);
    put_at<int32_t>(m, offsetof(HC, staminaChange), -5);
    put_at<uint32_t>(m, offsetof(HC, flags), 0x8);
    put_at<uint16_t>(m, offsetof(HC, damageTypes), 0x8);
    put_at<uint8_t>(m, offsetof(HC, attackCategory), 1);  // a mask: melee
    put_at<uint32_t>(m, offsetof(HC, impactId), 0x7E5F0003u);
    put_at<uint16_t>(m, offsetof(HC, hitReaction), 2);
    put_at<uint32_t>(m, offsetof(HC, weaponCategory), 0xABCDu);
    put_at<uint32_t>(m, offsetof(HC, programId), 0x1234u);
    put_at<float>(m, offsetof(HC, position), 1.5f);
    put_at<uint8_t>(m, offsetof(HC, sendHitEvent), 1);
    return b;
}

}  // namespace

TEST_CASE("hit_from_candidate reads the game's fields") {
    auto b = candidate();
    EmfHit h{};
    hit_from_candidate(b.data(), &h);
    CHECK(h.size == sizeof(EmfHit));
    CHECK(h.attacker == 7);
    CHECK(h.source == 9);
    CHECK(h.target == 30);
    CHECK(h.part == 31);
    CHECK(h.amount == 42);
    CHECK(h.stamina == 5);
    CHECK(h.flags == 8);
    CHECK(h.damage_types == 8);
    CHECK(h.attacks == 1);  // the game's mask, as it is (bit 0: melee; a wolf's bite, seen live)
    CHECK(h.kind == 0);
    CHECK(h.impact == 0x7E5F0003u);
    CHECK(h.from_mod == 1);
    CHECK(h.reaction == 2);
    CHECK(h.weapon_category == 0xABCDu);
    CHECK(h.program == 0x1234u);
    CHECK(h.position[0] == doctest::Approx(1.5f));
    CHECK(h.weapon_item == 0);
    CHECK(h.health_before == -1);
    CHECK(h.health_after == -1);
    CHECK(h.cancel == 0);
    put_at<uint32_t>(b.data(), offsetof(HC, rootTargetId), 0);
    put_at<uint32_t>(b.data(), offsetof(HC, impactId), 77u);
    hit_from_candidate(b.data(), &h);
    CHECK(h.target == 31);
    CHECK(h.from_mod == 0);
}

TEST_CASE("hit_to_candidate writes amount, stamina and reaction; cancel silences") {
    auto b = candidate();
    const auto orig = b;
    EmfHit h{};
    hit_from_candidate(b.data(), &h);
    h.amount = 10;
    h.stamina = 3;
    h.reaction = 0;
    hit_to_candidate(h, b.data());
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, healthChange)) == -10);
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, staminaChange)) == -3);
    CHECK(get_at<uint16_t>(b.data(), offsetof(HC, hitReaction)) == 0);
    auto expect = orig;  // nothing else moved
    put_at<int32_t>(expect.data(), offsetof(HC, healthChange), -10);
    put_at<int32_t>(expect.data(), offsetof(HC, staminaChange), -3);
    put_at<uint16_t>(expect.data(), offsetof(HC, hitReaction), 0);
    CHECK(b == expect);
    h.cancel = 1;
    hit_to_candidate(h, b.data());
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, healthChange)) == 0);
    CHECK(get_at<int32_t>(b.data(), offsetof(HC, staminaChange)) == 0);
    CHECK(get_at<uint8_t>(b.data(), offsetof(HC, sendHitEvent)) == 0);
    auto blocked = candidate();
    put_at<uint32_t>(blocked.data(), offsetof(HC, flags), EMF_HIT_BLOCKED);
    put_at<int32_t>(blocked.data(), offsetof(HC, healthChange), -7);  // the game ignores it for a blocked hit
    EmfHit g{};
    hit_from_candidate(blocked.data(), &g);
    g.amount = 10;
    hit_to_candidate(g, blocked.data());
    CHECK(get_at<int32_t>(blocked.data(), offsetof(HC, healthChange)) == -7);
}

TEST_CASE("hit_from_event skips terrain and building hits") {
    auto b = candidate();  // a HitEvent is the candidate's first 0xF8 bytes
    EmfHit h{};
    CHECK(hit_from_event(b.data(), sizeof(emf::gen::ecs::HitEvent), &h));
    CHECK(h.amount == 42);
    CHECK(h.target == 30);
    CHECK_FALSE(hit_from_event(b.data(), 100, &h));
    put_at<uint32_t>(b.data(), offsetof(HC, flags), EMF_HIT_TERRAIN);
    CHECK_FALSE(hit_from_event(b.data(), sizeof(emf::gen::ecs::HitEvent), &h));
    put_at<uint32_t>(b.data(), offsetof(HC, flags), EMF_HIT_BUILDING | EMF_HIT_CRIT);
    CHECK_FALSE(hit_from_event(b.data(), sizeof(emf::gen::ecs::HitEvent), &h));
}

TEST_CASE("HitOnce: parent levels of the same hit fire once") {
    HitOnce once;
    int p = 0;
    CHECK(once.should_fire(&p, 5, 31, 0, true));
    CHECK_FALSE(once.should_fire(&p, 5, 30, 31, true));
    CHECK_FALSE(once.should_fire(&p, 5, 29, 30, true));
    CHECK(once.should_fire(&p, 6, 31, 0, true));  // a new hit in the same stack copy
    CHECK(once.should_fire(&p, 6, 31, 0, true));  // the same swing hitting again later: a new hit
}

TEST_CASE("HitOnce: a level that does not apply passes the firing up") {
    HitOnce once;
    int p = 0;
    CHECK_FALSE(once.should_fire(&p, 5, 31, 0, false));
    CHECK(once.should_fire(&p, 5, 30, 31, true));
    CHECK_FALSE(once.should_fire(&p, 5, 29, 30, true));
}

TEST_CASE("HitOnce: reset starts over") {
    HitOnce once;
    int p = 0;
    CHECK(once.should_fire(&p, 5, 31, 0, true));
    once.reset();
    CHECK(once.should_fire(&p, 5, 30, 31, true));
}
