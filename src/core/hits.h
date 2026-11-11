#pragma once
// Hits (API v31): the rules the core applies to each landing hit before it lands, the hit record from the game's
// HitCandidate and HitEvent, and the once-per-hit tracker for hits the game passes up a target's parts
// (private research hit_hooks_2026-11-11). Game-free: the detour (hit_hook.h) feeds it.
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

enum class HitClass : uint8_t { None, Player, Creature, Object };
using HitClassify = HitClass (*)(uint32_t entity, void* ctx);

constexpr size_t kMaxHitRules = 64;

// Why a rule cannot be used (false and *why), or true.
bool hit_rule_valid(const EmfHitRule& r, std::string* why);
// Whether the rule is about this hit (attacker, target or part, flags, damage types, attack kind, damage or healing).
bool hit_rule_matches(const EmfHitRule& r, const EmfHit& h, HitClassify classify, void* ctx);
// Applies the matching rules in order: a cancel sets h.cancel; scale and add change the amount (a damage hit stays
// damage, a heal stays a heal) and scale a blocked hit's stamina; then the largest min_health caps the damage when the
// target's health is known; health_after follows. True when anything changed.
bool apply_hit_rules(std::span<const EmfHitRule> rules, EmfHit& h, HitClassify classify, void* ctx);

// The rules mods added, in id order. Thread-safe: apply takes a shared lock, add and remove an exclusive one.
class HitRules {
public:
    uint32_t add(const EmfHitRule& r, std::string* why);  // its id (from 1, never reused), or 0 and *why
    bool remove(uint32_t id);
    size_t size() const;
    bool apply(EmfHit& h, HitClassify classify, void* ctx) const;
    void reset();  // tests

private:
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<std::pair<uint32_t, EmfHitRule>> rules_;
    uint32_t next_id_ = 1;
};

}  // namespace emf::core
