#pragma once
// Hits (API v31): the rules the core applies to each landing hit before it lands, the hit record from the game's
// HitCandidate and HitEvent, and the once-per-hit tracker for hits the game passes up a target's parts
// (private research hit_hooks_2026-11-11). Game-free: the detour (hit_hook.h) feeds it.
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

enum class HitClass : uint8_t { None, Player, Creature, Object };
using HitClassify = HitClass (*)(uint32_t entity, void* ctx);

constexpr size_t kMaxHitRules = 64;
constexpr uint32_t kHitRuleV31Size = 36;  // the smallest rule size taken: a newer SDK's larger rule still works
constexpr uint16_t kMaxHitReaction = 10;  // the game's HitReactionType values (0 none .. 10)

// Why a rule cannot be used (false and *why), or true.
bool hit_rule_valid(const EmfHitRule& r, std::string* why);
// Whether the rule is about this hit (attacker, target or part, flags, damage types, attack kind, damage or healing).
bool hit_rule_matches(const EmfHitRule& r, const EmfHit& h, HitClassify classify, void* ctx);
// The fraction a scale rule left over from per-step env damage (shroud, cold, drowning: a few points a step), per
// entity and kind, so 0.5 halves 1-point ticks instead of rounding them all to 0. Not thread-safe: the env programs run
// one at a time (their Health conflicts), and hits never use it.
class HitCarry {
public:
    // The whole part of `exact` plus what this entity's kind carried; the fraction is kept. Locked: two env programs
    // may run at once on two worker threads (on different entities).
    int64_t take_whole(uint32_t entity, uint8_t kind, double exact);
    void clear();
    size_t size() const;

private:
    mutable std::mutex m_;
    std::unordered_map<uint64_t, double> carry_;
};
// Whether a kind is per-step env damage (shroud, cold, drowning).
constexpr bool per_step_kind(uint8_t kind) {
    return kind == EMF_HIT_KIND_SHROUD || kind == EMF_HIT_KIND_COLD || kind == EMF_HIT_KIND_DROWNING;
}

// Applies the matching rules in order: a cancel sets h.cancel; scale and add change the amount (a damage hit stays
// damage, a heal stays a heal) and scale a blocked hit's stamina; then the largest min_health caps the damage when the
// target's health is known; health_after follows. A per-step env kind with `carry` keeps the fraction scale left over.
// True when anything changed.
bool apply_hit_rules(std::span<const EmfHitRule> rules, EmfHit& h, HitClassify classify, void* ctx,
                     HitCarry* carry = nullptr);

// The rules mods added, in id order. Thread-safe: apply takes a shared lock, add and remove an exclusive one.
class HitRules {
public:
    uint32_t add(const EmfHitRule& r, std::string* why);  // its id (from 1, never reused), or 0 and *why
    bool remove(uint32_t id);
    size_t size() const;
    bool apply(EmfHit& h, HitClassify classify, void* ctx, HitCarry* carry = nullptr) const;
    // Whether a rule takes part in fall, shroud, cold or drowning (kinds beyond entity hits): the env programs read
    // nothing for a v31 rule alone. Lock-free.
    bool any_env() const { return env_rules_.load(std::memory_order_acquire) != 0; }
    void reset();  // tests

private:
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<std::pair<uint32_t, EmfHitRule>> rules_;
    std::atomic<uint32_t> env_rules_{0};
    uint32_t next_id_ = 1;
};

// ---- the game's records ----

// EmfHit from the game's 0x150-byte HitCandidate (a hit about to be applied): amount = -healthChange, stamina =
// -staminaChange, target = the root target or else the entity hit; weapon_item 0 and health -1 (the caller fills them).
void hit_from_candidate(const uint8_t* hc, EmfHit* h);
// Writes amount, stamina and reaction back (a blocked or parried hit's health change is left: the game ignores it); a
// cancelled hit gets no health or stamina change and no HitEvent (sendHitEvent 0).
void hit_to_candidate(const EmfHit& h, uint8_t* hc);
// EmfHit from the game's HitEvent (a hit applied): false when it is too small or a terrain or building hit.
bool hit_from_event(const uint8_t* ev, uint32_t size, EmfHit* h);

// Fires a hit's rules and callbacks once: the game passes one hit up to five parents of a multi-part target, in the
// same stack copy, each level with propagatedFromEntityId = the level before. A level that does not apply the hit
// (CombatFlags.handleHits 0) passes the firing up to its parent. One per impact thread (thread_local in the detour).
class HitOnce {
public:
    bool should_fire(const void* hit, uint32_t impact, uint32_t target, uint32_t propagated_from, bool applies);
    // After the game's applyHitToEntity returned: whether it set the caller's propagate byte (the conclusion walks up
    // to the parent only then). The next call continues this chain only when it did.
    void note_propagate(bool propagate) { propagate_ = propagate; }
    void reset();

private:
    const void* hit_ = nullptr;
    uint32_t impact_ = 0;
    uint32_t last_target_ = 0;
    bool fired_ = false;
    bool propagate_ = false;
};

}  // namespace emf::core
