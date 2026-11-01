#pragma once
// loot_scale: the factor applied to the game's "Loot Amount" world setting (unit-tested).
#include <cmath>
#include <cstdint>
#include <string>

namespace lootscale {
constexpr double kMin = 0.1, kMax = 10.0;

// The player's factor, kept between kMin and kMax; anything that is not a number means 1 (no change).
inline double clamp_factor(double f) {
    if (std::isnan(f)) return 1.0;
    return f < kMin ? kMin : (f > kMax ? kMax : f);
}

// The value the game gets: its own setting (the world's "Loot Amount", 0.25..2) times the factor.
inline float scaled(float game_value, double factor) {
    return static_cast<float>(game_value * clamp_factor(factor));
}
// ---- what the game's loot setting does not reach (scaled after the fact, from the game's PickupEvent) ----
// The hooked getter is read when the game makes the loot of chests, kills and things lying on the ground. A plant or
// bush picked with E rolls its loot on the spot without it (immediate_loot_action, docs/engine_parts/re_villages.md),
// and trees and rocks hit with a tool drop theirs without it too (mining_node_events, re_content.md "Loot"): for those
// the mod gives the difference (or takes it back) when the game reports the pickup.
enum class Source { Other, Harvest, Chopping, Mining };

inline const char* source_name(Source s) {
    switch (s) {
        case Source::Harvest: return "harvesting";
        case Source::Chopping: return "chopping";
        case Source::Mining: return "mining";
        case Source::Other: break;
    }
    return "other";
}

// The entity a PickupEvent came from, as read from its components (loot_scale_glue.h).
struct SourceInfo {
    bool exists = false;             // a source at all (0 = crafting, EMF's own gifts, ...)
    bool pickup = false;             // PickupItem: twigs, stones and other things lying on the ground
    bool inventory = false;          // Inventory or InventorySetup: a chest, the loot spot of a kill, an animal
    bool default_inventory = false;  // DefaultInventory: a plant or bush picked with E (its loot is rolled then)
    bool health = false;             // a creature or a player
    int node = -1;                   // MiningNode.resourceType: 0 wood (trees, bushes hit with an axe), 1 stone; -1 none
};

inline bool is_food(const std::string& name) {
    std::string n = name;
    for (auto& c : n)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return n.rfind("food_", 0) == 0 || n.rfind("_food_", 0) == 0;
}

// What an item picked up from `s` counts as. Other: left to the game (loot made with its setting, which the hook
// scales; or something a player made, dropped or stored, which must never be multiplied).
inline Source classify(const SourceInfo& s, const std::string& item_name) {
    if (!s.exists || s.pickup || s.health) return Source::Other;
    if (s.node == 1) return Source::Mining;
    if (s.node == 0) return s.inventory && is_food(item_name) ? Source::Harvest : Source::Chopping;  // a fruit tree's fruit
    if (s.inventory) return Source::Other;
    if (s.default_inventory) return Source::Harvest;
    return Source::Other;
}

// The factors as set: main = "Loot amount factor"; the others 0 (their default) = the same as main.
struct Factors {
    double main = 2.0;
    double harvest = 0, chopping = 0, mining = 0;
};

// The multiplier for loot from `s`: its own factor, or the main one when that is 0 (or less); 1 for Other (the hook).
inline double multiplier(Source s, const Factors& f) {
    double own = 0;
    switch (s) {
        case Source::Harvest: own = f.harvest; break;
        case Source::Chopping: own = f.chopping; break;
        case Source::Mining: own = f.mining; break;
        case Source::Other: return 1.0;
    }
    return clamp_factor(own > 0 ? own : f.main);
}

// The game gave `count` (at x1); the player wants count x m. How many more to give (> 0) or to take back (< 0); `roll`
// (0 <= roll < 1) decides a fraction: 1 x 1.5 is 2 half the time.
inline int adjust(uint32_t count, double m, double roll) {
    const double want = count * clamp_factor(m);
    const double whole = std::floor(want);
    const double target = whole + (roll < want - whole ? 1.0 : 0.0);
    return static_cast<int>(target) - static_cast<int>(count);
}
}  // namespace lootscale
