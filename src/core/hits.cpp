#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "core/hits.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "core/attributes.h"
#include "emf/generated/components.hpp"

namespace emf::core {

namespace {

bool special(uint32_t who) { return who == EMF_HIT_PLAYERS || who == EMF_HIT_CREATURES || who == EMF_HIT_OBJECTS; }

bool who_ok(uint32_t who) { return who < 0xFFFFFFF0u || special(who); }

HitClass class_of(uint32_t who) {
    switch (who) {
    case EMF_HIT_PLAYERS: return HitClass::Player;
    case EMF_HIT_CREATURES: return HitClass::Creature;
    default: return HitClass::Object;
    }
}

bool is_heal(const EmfHit& h) { return (h.flags & EMF_HIT_HEALING) != 0 || h.amount < 0; }

// One side of a rule: anyone, a class, or the entity (the target side also matches the part hit).
bool side_matches(uint32_t want, uint32_t entity, uint32_t part, HitClassify classify, void* ctx) {
    if (want == 0) return true;
    if (special(want)) return entity != 0 && classify && classify(entity, ctx) == class_of(want);
    return want == entity || (part != 0 && want == part);
}

int32_t clamp_i64(int64_t v) {
    return static_cast<int32_t>(std::clamp<int64_t>(v, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

}  // namespace

bool hit_rule_valid(const EmfHitRule& r, std::string* why) {
    auto no = [&](const char* text) {
        if (why) *why = text;
        return false;
    };
    if (r.size < sizeof(EmfHitRule)) return no("its size is smaller than EmfHitRule (set size = sizeof(EmfHitRule))");
    if (!who_ok(r.attacker)) return no("attacker is not 0, an entity or EMF_HIT_PLAYERS / _CREATURES / _OBJECTS");
    if (!who_ok(r.target)) return no("target is not 0, an entity or EMF_HIT_PLAYERS / _CREATURES / _OBJECTS");
    if (!std::isfinite(r.scale) || r.scale < 0.0f || r.scale > 100.0f) return no("scale must be between 0 and 100");
    if (r.add < -1000000 || r.add > 1000000) return no("add must be between -1000000 and 1000000");
    if (r.min_health < 0) return no("min_health must be 0 or more");
    if (r.healing && r.min_health > 0) return no("min_health does nothing on healing hits");
    if (!r.cancel && r.scale == 1.0f && r.add == 0 && r.min_health == 0)
        return no("it does nothing (no cancel, scale 1, add 0, min_health 0)");
    return true;
}

bool hit_rule_matches(const EmfHitRule& r, const EmfHit& h, HitClassify classify, void* ctx) {
    if ((r.healing != 0) != is_heal(h)) return false;
    if (!side_matches(r.attacker, h.attacker, 0, classify, ctx)) return false;
    if (!side_matches(r.target, h.target, h.part, classify, ctx)) return false;
    if ((h.flags & r.flags_all) != r.flags_all) return false;
    if (r.damage_types_any && !(h.damage_types & r.damage_types_any)) return false;
    if (r.attacks && !(h.attacks & r.attacks)) return false;
    return true;
}

bool apply_hit_rules(std::span<const EmfHitRule> rules, EmfHit& h, HitClassify classify, void* ctx) {
    const EmfHit before = h;
    const bool heal = is_heal(h);
    int32_t floor = 0;
    for (const EmfHitRule& r : rules) {
        if (!hit_rule_matches(r, h, classify, ctx)) continue;
        if (r.cancel) h.cancel = 1;
        const int64_t scaled = static_cast<int64_t>(std::trunc(static_cast<double>(h.amount) * r.scale));
        int64_t amount = scaled + r.add;
        amount = heal ? std::min<int64_t>(amount, 0) : std::max<int64_t>(amount, 0);
        h.amount = clamp_i64(amount);
        h.stamina = clamp_i64(std::max<int64_t>(
            0, static_cast<int64_t>(std::trunc(static_cast<double>(h.stamina) * r.scale))));
        floor = std::max(floor, r.min_health);
    }
    if (floor > 0 && !heal && h.health_before >= 0 && h.amount > 0)
        h.amount = std::min(h.amount, std::max(0, h.health_before - floor));
    if (h.health_before < 0) h.health_after = -1;
    else if (h.amount != before.amount || before.health_after < 0)
        h.health_after = std::max(0, h.health_before - h.amount);
    return h.amount != before.amount || h.stamina != before.stamina || h.cancel != before.cancel;
}

// ---- the game's records ----

namespace {

using HC = emf::gen::ecs::HitCandidate;
using HE = emf::gen::ecs::HitEvent;
static_assert(sizeof(HC) == 0x150 && offsetof(HC, healthChange) == 0xBC && offsetof(HC, sendHitEvent) == 0x14C,
              "HitCandidate layout (research hit_hooks_2026-11-11 section 3)");
static_assert(sizeof(HE) == 0xF8 && offsetof(HE, healthChange) == offsetof(HC, healthChange) &&
                  offsetof(HE, targetFaction) == offsetof(HC, targetFaction),
              "a HitEvent is the HitCandidate's first 0xF8 bytes");

template <class T>
T rd(const uint8_t* p, size_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}
template <class T>
void wr(uint8_t* p, size_t off, T v) {
    std::memcpy(p + off, &v, sizeof v);
}

// The fields a HitEvent shares with the candidate (its first 0xF8 bytes).
void common_fields(const uint8_t* p, EmfHit* h) {
    *h = EmfHit{};
    h->size = sizeof(EmfHit);
    h->attacker = rd<uint32_t>(p, offsetof(HC, rootSourceId));
    h->source = rd<uint32_t>(p, offsetof(HC, sourceId));
    h->part = rd<uint32_t>(p, offsetof(HC, targetId));
    const uint32_t root = rd<uint32_t>(p, offsetof(HC, rootTargetId));
    h->target = root ? root : h->part;
    h->amount = -rd<int32_t>(p, offsetof(HC, healthChange));
    h->stamina = -rd<int32_t>(p, offsetof(HC, staminaChange));
    h->flags = rd<uint32_t>(p, offsetof(HC, flags));
    h->damage_types = rd<uint16_t>(p, offsetof(HC, damageTypes));
    h->attacks = rd<uint8_t>(p, offsetof(HC, attackCategory));  // a mask (applyDamage sets and clears bits)
    h->weapon_category = rd<uint32_t>(p, offsetof(HC, weaponCategory));
    h->program = rd<uint32_t>(p, offsetof(HC, programId));
    h->impact = rd<uint32_t>(p, offsetof(HC, impactId));
    h->reaction = rd<uint16_t>(p, offsetof(HC, hitReaction));
    h->target_faction = rd<uint8_t>(p, offsetof(HC, targetFaction));
    h->from_mod = is_mod_impact(h->impact) ? 1 : 0;
    h->health_before = -1;
    h->health_after = -1;
    std::memcpy(h->position, p + offsetof(HC, position), sizeof h->position);
}

}  // namespace

void hit_from_candidate(const uint8_t* hc, EmfHit* h) {
    common_fields(hc, h);
    h->kind = rd<uint8_t>(hc, offsetof(HC, candidateType));
}

void hit_to_candidate(const EmfHit& h, uint8_t* hc) {
    if (h.cancel) {
        wr<int32_t>(hc, offsetof(HC, healthChange), 0);
        wr<int32_t>(hc, offsetof(HC, staminaChange), 0);
        wr<uint8_t>(hc, offsetof(HC, sendHitEvent), 0);
        return;
    }
    const uint32_t flags = rd<uint32_t>(hc, offsetof(HC, flags));
    if (!(flags & (EMF_HIT_BLOCKED | EMF_HIT_PARRIED))) wr<int32_t>(hc, offsetof(HC, healthChange), -h.amount);
    wr<int32_t>(hc, offsetof(HC, staminaChange), -h.stamina);
    wr<uint16_t>(hc, offsetof(HC, hitReaction), h.reaction);
}

bool hit_from_event(const uint8_t* ev, uint32_t size, EmfHit* h) {
    if (!ev || size < sizeof(HE)) return false;
    if (rd<uint32_t>(ev, offsetof(HE, flags)) & (EMF_HIT_TERRAIN | EMF_HIT_BUILDING)) return false;
    common_fields(ev, h);
    return true;
}

bool HitOnce::should_fire(const void* hit, uint32_t impact, uint32_t target, uint32_t propagated_from, bool applies) {
    const bool parent = hit == hit_ && impact == impact_ && propagated_from != 0 && propagated_from == last_target_;
    if (!parent) {
        hit_ = hit;
        impact_ = impact;
        fired_ = false;
    }
    last_target_ = target;
    if (!applies || fired_) return false;
    fired_ = true;
    return true;
}

void HitOnce::reset() { *this = HitOnce{}; }

uint32_t HitRules::add(const EmfHitRule& r, std::string* why) {
    if (!hit_rule_valid(r, why)) return 0;
    AcquireSRWLockExclusive(&lock_);
    uint32_t id = 0;
    if (rules_.size() >= kMaxHitRules) {
        if (why) *why = "there are already 64 rules";
    } else {
        id = next_id_++;
        EmfHitRule copy = r;
        copy.size = sizeof copy;
        rules_.emplace_back(id, copy);
    }
    ReleaseSRWLockExclusive(&lock_);
    return id;
}

bool HitRules::remove(uint32_t id) {
    AcquireSRWLockExclusive(&lock_);
    const auto it = std::find_if(rules_.begin(), rules_.end(), [&](const auto& e) { return e.first == id; });
    const bool found = it != rules_.end();
    if (found) rules_.erase(it);
    ReleaseSRWLockExclusive(&lock_);
    return found;
}

size_t HitRules::size() const {
    AcquireSRWLockShared(&lock_);
    const size_t n = rules_.size();
    ReleaseSRWLockShared(&lock_);
    return n;
}

bool HitRules::apply(EmfHit& h, HitClassify classify, void* ctx) const {
    EmfHitRule local[kMaxHitRules];
    size_t n = 0;
    AcquireSRWLockShared(&lock_);
    for (const auto& e : rules_) local[n++] = e.second;
    ReleaseSRWLockShared(&lock_);
    if (!n) return false;
    return apply_hit_rules(std::span<const EmfHitRule>(local, n), h, classify, ctx);
}

void HitRules::reset() {
    AcquireSRWLockExclusive(&lock_);
    rules_.clear();
    next_id_ = 1;
    ReleaseSRWLockExclusive(&lock_);
}

}  // namespace emf::core
