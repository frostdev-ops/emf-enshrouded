// Hits (API v31): the rule engine the core applies to each landing hit before it lands (private research
// hit_hooks_2026-11-11, spec 2026-11-11-hit-hooks-design: match by attacker, target, flags, types and attack kind;
// cancel, scale, add, min_health; damage hits only unless a rule says healing).
#include <doctest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/hits.h"

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
    ranged.attack = 1;
    CHECK(hit_rule_matches(r, ranged, &classify, nullptr));
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
