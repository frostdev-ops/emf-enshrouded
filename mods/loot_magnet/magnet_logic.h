#pragma once
// Loot Magnet: which corpse is looted into which player, without the game (unit-tested in tests/test_loot_magnet.cpp).
// The mod (loot_magnet.cpp) records deaths (EntityDiedEvent), describes the dead bodies (a Body from their components)
// and sends the game's own bypass loot event {guest = player, host = corpse, lootAll} for each Loot this returns: the
// event confirmed in game (the corpse's stacks moved into player 1, a PickupEvent each).
// Most creatures do not keep their loot: they fade away and the game spawns a loot container where they died (the
// glowing loot spot, templates LootContainer_Enemy_*: Inventory, DefaultInventory, DestroyOnLoot, an interaction
// offer, no Health). So a death is looted from the corpse itself when the corpse holds the loot, else from the loot
// container that appeared within reach of where it died at or after the death, by the game's clock: its SpawnTime
// against the EntityDiedEvent's timeStamp (a spot may spawn in the death's own step; one spawned before is not its
// loot).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace magnet {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};
inline double horizontal_distance(const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }
inline double distance(const Vec3& a, const Vec3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// What the mod read from a dead entity's components.
struct Body {
    uint32_t id = 0;
    bool exists = false;         // still in the world (has a position)
    Vec3 pos;
    bool player = false;         // PlayerState
    bool has_health = false;     // Health: a creature, not a chest or a prop
    bool has_inventory = false;  // an Inventory, or an InventorySetup linking inventories: the loot event's "real
                                 // inventory" path
    bool has_offer = false;      // InteractionOfferRuntime: read for the log only (a corpse need not have it: G1)
    bool dead = false;           // NetworkHealth.health 0 (else the Health slot 0): a corpse, not a living creature
    uintptr_t tpl = 0;           // its template's data (Enemy01BehaviorState): who it is; 0 = unknown
    uint32_t owner = 0;          // OwnerRelationship.ownerEntityId
    int faction = -1;            // FactionType; -1 = none
    bool tame = false;           // IsTame
    bool villager = false;       // a villager (ActiveNpcState template)
    std::vector<std::string> items;  // the debug names of the stacks it holds ("" = not named by the game yet)
    bool container = false;          // a loot container the game left where a creature died (not a creature, not a
                                     // player's grave, not a pickup: see magnet_glue.h read_body)
    int64_t spawned = 0;             // SpawnTime (the game's clock, ns): when it appeared; 0 = none, or not set yet
};

struct Death {
    uint32_t id = 0;
    Vec3 pos;            // where it died
    uint64_t at_ms = 0;  // when
    uint32_t session = 0;
    uintptr_t tpl = 0;   // its template's data when it died (0 = unknown: not compared)
    std::vector<uint32_t> before;  // the loot containers already near when it died: not its loot
    bool guarded = false;          // a player, an ally, a farm animal, owned, tame or a villager when it died: nothing
                                   // it leaves is taken
    int64_t time = 0;              // EntityDiedEvent.timeStamp (the game's clock, ns); 0 = not given: `before` decides
};

// A loot spot near a death that was not taken for it, and why: for the decision log, so a game test can tell the cases
// apart. Each is told once per death and reason.
struct Skip {
    enum Why { Before, OutOfReach, NoSpawnTime, Claimed };
    uint32_t corpse = 0, container = 0;
    Why why = Before;
    double distance = 0;  // from where it died (or where its body was last), metres
    double seconds = 0;   // Before: how long before the death it appeared (0 when the game gave no times)
};
inline const char* skip_text(Skip::Why w) {
    switch (w) {
        case Skip::Before: return "it was there before the death (an earlier kill's loot)";
        case Skip::OutOfReach: return "too far from where it died";
        case Skip::NoSpawnTime: return "the game has not given it a spawn time (yet)";
        case Skip::Claimed: return "taken for another kill";
    }
    return "?";
}

struct Rules {
    double radius = 8.0;         // metres from the player
    uint64_t delay_ms = 1000;    // after the death, before the loot comes
    uint64_t window_ms = 15000;  // after the death: the magnet keeps looking this long (the game adds a corpse's
                                 // Inventory 1-3 s after the death); an older corpse is left for the player (exploring)
    double moved_max = 5.0;      // a body farther than this from where it died is not that corpse (an id reused)
    double base_radius = 30.0;   // nothing inside a base: within this of a flame altar (horizontally)
    bool materials_only = true;  // only corpses whose every stack is a material
    bool include_ammo = true;    // arrows count as materials
    double container_reach = 5.0;  // its loot container: this close to where it died (or where its body was last)
};

enum class Why { Ok, TooSoon, TooLate, TooFar, Looted, Protected, Moved, Gone, NoLoot, Empty, InBase, Filtered, Alive, Reused };

// For the decision log.
inline const char* why_text(Why w) {
    switch (w) {
        case Why::Ok: return "looted";
        case Why::TooSoon: return "too soon after the death";
        case Why::TooLate: return "too long after the death";
        case Why::TooFar: return "nobody with a magnet within reach";
        case Why::Looted: return "already looted";
        case Why::Protected: return "a player's, an ally's, a farm animal's, an owned or tame body, or a villager";
        case Why::Moved: return "more than 5 m from where it died (not that corpse)";
        case Why::Gone: return "gone (the game removed it)";
        case Why::NoLoot: return "no Health or no inventory (yet): nothing the loot event can take";
        case Why::Empty: return "empty, or an item the game does not name yet";
        case Why::InBase: return "inside a base";
        case Why::Filtered: return "holds something that is not a material (materials only)";
        case Why::Alive: return "not dead (alive again, or the id belongs to a living creature now)";
        case Why::Reused: return "the id belongs to another kind of entity now";
    }
    return "?";
}

// A material by its debug name, the rule life_skills uses for raw materials: material_, food_, _food_,
// build_terrainmaterial_ (any case); ammo_ when include_ammo.
inline bool is_material(const std::string& name, bool include_ammo) {
    std::string n = name;
    for (auto& c : n)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    for (const char* p : {"material_", "food_", "_food_", "build_terrainmaterial_"})
        if (n.rfind(p, 0) == 0) return true;
    return include_ammo && n.rfind("ammo_", 0) == 0;
}

// Factions never looted: Player 7, AutomatedPlayer 8, PlayerAlly 11 (summons, the companion), FarmAnimal 12.
inline bool protected_faction(int f) { return f == 7 || f == 8 || f == 11 || f == 12; }

// Whether a verdict is for good (the death is forgotten) or the magnet looks again on its next round: the loot comes a
// moment after the death (on the corpse, or in a loot container where it died, while the corpse fades and goes), so "no
// inventory", "empty", "alive", "too soon", "nobody near", and a corpse gone, moved or replaced may still change; who
// it belongs to, a base, a non-material in it, or the time running out do not.
inline bool is_final(Why w) {
    return w != Why::TooSoon && w != Why::TooFar && w != Why::NoLoot && w != Why::Empty && w != Why::Alive &&
           w != Why::Gone && w != Why::Moved && w != Why::Reused;
}

// Whose death leaves nothing for the magnet: a player, an ally (the companion, summons), a farm animal, anything owned
// or tame, a villager.
inline bool guarded(const Body& b) {
    return b.player || protected_faction(b.faction) || b.owner || b.tame || b.villager;
}

inline bool in_base(const Vec3& p, const std::vector<Vec3>& bases, const Rules& r) {
    for (const auto& base : bases)
        if (horizontal_distance(p, base) <= r.base_radius) return true;
    return false;
}

// Whether what `b` holds may come (materials only: every stack a material), else why not (yet).
inline Why judge_items(const Body& b, const Rules& r) {
    if (b.items.empty()) return Why::Empty;
    if (r.materials_only) {
        for (const auto& item : b.items)
            if (item.empty()) return Why::Empty;  // a name the game does not give yet: ask again next time
        for (const auto& item : b.items)
            if (!is_material(item, r.include_ammo)) return Why::Filtered;
    }
    return Why::Ok;
}

// The death itself: the time, whose it was, a base where it died. Ok here means: look at what it left.
inline Why judge_death(const Death& d, const std::vector<Vec3>& bases, const Rules& r, uint64_t now_ms, bool looted_before) {
    if (looted_before) return Why::Looted;
    if (now_ms < d.at_ms + r.delay_ms) return Why::TooSoon;
    if (now_ms > d.at_ms + r.window_ms) return Why::TooLate;
    if (d.guarded) return Why::Protected;
    if (in_base(d.pos, bases, r)) return Why::InBase;
    return Why::Ok;
}

// May corpse `b` (died as `d`) be looted now into a player standing at `player`?
inline Why judge(const Death& d, const Body& b, const Vec3& player, const std::vector<Vec3>& bases, const Rules& r,
                 uint64_t now_ms, bool looted_before) {
    if (const Why w = judge_death(d, bases, r, now_ms, looted_before); w != Why::Ok) return w;
    if (!b.exists) return Why::Gone;
    // lasting reasons first: these never change
    if (d.tpl && b.tpl && b.tpl != d.tpl) return Why::Reused;
    if (b.player || protected_faction(b.faction) || b.owner || b.tame || b.villager) return Why::Protected;
    if (horizontal_distance(b.pos, d.pos) > r.moved_max) return Why::Moved;
    if (in_base(b.pos, bases, r)) return Why::InBase;
    // then what may still change: its loot comes a moment after the death
    if (!b.has_health || !b.has_inventory) return Why::NoLoot;
    if (!b.dead) return Why::Alive;
    if (distance(b.pos, player) > r.radius) return Why::TooFar;
    return judge_items(b, r);
}

// May loot container `c`, which appeared where `d` died, be looted now into a player standing at `player`? (The death's
// own verdict, judge_death, comes first.)
inline Why judge_container(const Body& c, const Vec3& player, const std::vector<Vec3>& bases, const Rules& r) {
    if (!c.exists) return Why::Gone;
    if (c.player || c.owner || c.tame || c.villager) return Why::Protected;
    if (in_base(c.pos, bases, r)) return Why::InBase;
    if (!c.has_inventory) return Why::NoLoot;
    if (distance(c.pos, player) > r.radius) return Why::TooFar;
    return judge_items(c, r);
}

struct Looter {
    uint32_t id = 0;
    Vec3 pos;
    bool on = false;  // this player's magnet
};
struct Loot {
    uint32_t corpse = 0, player = 0;  // corpse: the entity the loot event opens (the corpse, or the loot container it left)
    int tries = 1;  // loot events sent for this corpse so far (overdue)
};
// What became of a corpse the magnet watched (one per corpse, when it stops watching it), for the decision log.
struct Decision {
    uint32_t corpse = 0;
    Why why = Why::Ok;       // Ok: looted into `player`
    uint32_t player = 0;
    double distance = -1;    // to `player` (Ok), or to the nearest player with a magnet on (TooLate), -1 = none
    Why waited = Why::Ok;    // TooLate: what it was waiting for at the end
    bool offer = false;      // the body had an InteractionOfferRuntime (for the log)
    uint32_t container = 0;  // Ok or Filtered: the loot container it left (0 = the corpse itself)
};
using Lookup = std::function<Body(uint32_t id)>;

class Magnet {
public:
    static constexpr size_t kMaxWatched = 256;
    static constexpr uint64_t kConfirmMs = 2000;  // a PickupEvent from the corpse is expected within this
    static constexpr int kMaxTries = 5;           // a loot event nothing came from is sent again, this often at most

    void on_death(uint32_t id, const Vec3& pos, uint64_t now_ms, uint32_t session, uintptr_t tpl = 0) {
        on_death(Death{id, pos, now_ms, session, tpl});
    }
    // With what the mod saw at the death: the loot containers already near (d.before) and whether it was guarded.
    void on_death(Death d) {
        if (d.session == session_) looted_.erase(d.id);  // a death of a looted id: a new creature reused it
        if (deaths_.size() >= kMaxWatched) deaths_.pop_front();
        const Vec3 at = d.pos;
        deaths_.push_back({std::move(d), Why::TooSoon, -1, at});
    }
    // Where the watched deaths happened (and where their bodies were last): the mod looks for loot containers near these.
    std::vector<Vec3> watched_places() const {
        std::vector<Vec3> out;
        for (const auto& w : deaths_) {
            out.push_back(w.d.pos);
            if (horizontal_distance(w.last, w.d.pos) > 0.5) out.push_back(w.last);
        }
        return out;
    }

    // The corpses to loot now, each into the nearest player whose magnet is on and who is within the radius. A corpse
    // returned here is never returned again; corpses that can never be looted are forgotten, the others wait.
    std::vector<Loot> tick(uint64_t now_ms, uint32_t session, const std::vector<Looter>& players, const Lookup& body,
                           const std::vector<Vec3>& bases, const Rules& r, std::vector<Decision>* decisions = nullptr,
                           const std::vector<Body>& containers = {}) {
        if (session != session_) {  // ids are reused after a reload: what we knew means nothing now
            session_ = session;
            looted_.clear();
            pending_.clear();
            claimed_.clear();
        }
        for (auto it = claimed_.begin(); it != claimed_.end();)  // a destroyed container's id may come back later
            it = now_ms > it->second + kClaimMs ? claimed_.erase(it) : std::next(it);
        std::vector<Loot> out;
        auto decide = [&](const Watched& w, Why why, uint32_t player, double dist, bool offer, uint32_t container = 0) {
            if (decisions)
                decisions->push_back({w.d.id, why, player, dist, why == Why::TooLate ? w.waited : Why::Ok, offer, container});
        };
        // The nearest player whose magnet is on and who is within the radius of `at` (nullptr: none; `nearest` = the
        // distance to the nearest player with a magnet, -1 = none).
        auto looter_for = [&](const Vec3& at, double* best_d, double* nearest) -> const Looter* {
            const Looter* best = nullptr;
            *best_d = r.radius;
            *nearest = -1;
            for (const auto& p : players) {
                if (!p.on) continue;
                const double dd = distance(p.pos, at);
                if (*nearest < 0 || dd < *nearest) *nearest = dd;
                if (dd <= *best_d) {
                    best = &p;
                    *best_d = dd;
                }
            }
            return best;
        };
        for (auto it = deaths_.begin(); it != deaths_.end();) {
            if (it->d.session != session) {
                it = deaths_.erase(it);
                continue;
            }
            const Body b = body(it->d.id);
            // the corpse's own verdict first (a player standing on it), then who is near enough
            const Why corpse = judge(it->d, b, b.pos, bases, r, now_ms, looted_.count(it->d.id) != 0);
            if (b.exists && corpse != Why::Moved && corpse != Why::Reused && horizontal_distance(b.pos, it->d.pos) <= r.moved_max)
                it->last = b.pos;
            if (!is_final(corpse)) {  // its loot may still come: on it, or in a loot container where it died
                it->waited = corpse;
                if (corpse != Why::TooSoon && from_container(*it, containers, bases, r, now_ms, looter_for, decide, out)) {
                    it = deaths_.erase(it);
                    continue;
                }
                ++it;
                continue;
            }
            if (corpse != Why::Ok) {
                decide(*it, corpse, 0, corpse == Why::TooLate ? it->nearest : -1, b.has_offer);
                it = deaths_.erase(it);
                continue;
            }
            double best_d = r.radius, nearest = -1;
            const Looter* best = looter_for(b.pos, &best_d, &nearest);
            if (!best) {
                it->waited = Why::TooFar;
                it->nearest = nearest;
                ++it;
                continue;
            }
            out.push_back({it->d.id, best->id});
            decide(*it, Why::Ok, best->id, best_d, b.has_offer);
            looted_.insert(it->d.id);
            it = deaths_.erase(it);
        }
        return out;
    }

    // The loot event for `corpse` went out for `player`; a PickupEvent (by player, from corpse) confirms it.
    void on_sent(uint32_t corpse, uint32_t player, uint64_t now_ms, uint32_t session, int tries = 1) {
        pending_.push_back({corpse, player, now_ms, session, tries});
    }
    // Nothing came from the corpse (no PickupEvent) while it still holds loot: send again? (A corpse may become
    // lootable a moment after it died; nothing moved, so nothing is taken twice.)
    bool should_retry(const Loot& late) const { return late.tries < kMaxTries; }
    void on_pickup(uint32_t by, uint32_t from) {
        for (auto it = pending_.begin(); it != pending_.end();)
            it = it->player == by && it->corpse == from ? pending_.erase(it) : it + 1;
    }
    // Loot sent more than 2 s ago without a pickup (the mod then checks whether the corpse still holds loot: no room).
    std::vector<Loot> overdue(uint64_t now_ms) {
        std::vector<Loot> out;
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (now_ms > it->at_ms + kConfirmMs) {
                out.push_back({it->corpse, it->player, it->tries});
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
        return out;
    }
    // "No room" is told once per player and session.
    bool should_tell_full(uint32_t player, uint32_t session) { return told_full_.insert({session, player}).second; }

    size_t watched() const { return deaths_.size(); }
    // The loot spots skipped since the last call (each told once per death and reason).
    std::vector<Skip> take_skips() {
        std::vector<Skip> out;
        out.swap(skips_);
        return out;
    }

private:
    static constexpr uint64_t kClaimMs = 60000;  // a claim is kept this long (a spot without spawn times: id only)
    struct Sent {
        uint32_t corpse, player;
        uint64_t at_ms;
        uint32_t session;
        int tries;
    };
    struct Watched {
        Death d;
        Why waited;      // what it waits for (TooSoon, NoLoot, Empty, Alive, TooFar, Gone, Moved, Reused)
        double nearest;  // TooFar: the nearest player with a magnet on, -1 = none
        Vec3 last;       // where its body was last seen (it may slide a little before it fades)
        std::set<std::pair<uint32_t, int>> told;  // (loot spot, Skip::Why) already told
    };

    void tell(Watched& w, const Body& c, Skip::Why why, double dist) {
        if (!w.told.insert({c.id, static_cast<int>(why)}).second) return;
        const double seconds = why == Skip::Before && w.d.time > 0 && c.spawned > 0 ? (w.d.time - c.spawned) / 1e9 : 0;
        skips_.push_back({w.d.id, c.id, why, dist, seconds});
    }

    // The loot container `w` left: the nearest one within reach of where it died (or its body was last) that appeared at
    // or after the death (by the game's clock; without it, one not in `before`) and nobody took yet. True when the death
    // is decided (looted from it, or the container's lasting verdict); false while it waits (nothing there yet, empty
    // yet, nobody near).
    template <class LooterFor, class Decide>
    bool from_container(Watched& w, const std::vector<Body>& containers, const std::vector<Vec3>& bases, const Rules& r,
                        uint64_t now_ms, const LooterFor& looter_for, const Decide& decide, std::vector<Loot>& out) {
        const Body* c = nullptr;
        double c_d = 0;
        for (const auto& candidate : containers) {
            if (!candidate.container) continue;
            const double dd = (std::min)(horizontal_distance(candidate.pos, w.d.pos), horizontal_distance(candidate.pos, w.last));
            bool before = false;
            if (w.d.time > 0) {
                before = candidate.spawned > 0 && candidate.spawned < w.d.time;
            } else {
                for (uint32_t id : w.d.before) before = before || id == candidate.id;
            }
            if (dd > r.container_reach) {
                tell(w, candidate, Skip::OutOfReach, dd);
            } else if (w.d.time > 0 && candidate.spawned <= 0) {
                tell(w, candidate, Skip::NoSpawnTime, dd);  // it may get one next round: asked again
            } else if (before) {
                tell(w, candidate, Skip::Before, dd);
            } else if (claimed_.count({candidate.id, candidate.spawned})) {
                tell(w, candidate, Skip::Claimed, dd);
            } else if (!c || dd < c_d) {
                c = &candidate;
                c_d = dd;
            }
        }
        if (!c) return false;  // nothing there (yet): w.waited says what the corpse waits for
        const Why v = judge_container(*c, c->pos, bases, r);
        if (!is_final(v)) {
            w.waited = v;
            return false;
        }
        if (v != Why::Ok) {  // a container it will never be right to take: the death is decided
            claimed_[{c->id, c->spawned}] = now_ms;
            decide(w, v, 0, -1, c->has_offer, c->id);
            return true;
        }
        double best_d = r.radius, nearest = -1;
        const Looter* best = looter_for(c->pos, &best_d, &nearest);
        if (!best) {
            w.waited = Why::TooFar;
            w.nearest = nearest;
            return false;
        }
        out.push_back({c->id, best->id});
        claimed_[{c->id, c->spawned}] = now_ms;
        looted_.insert(w.d.id);
        decide(w, Why::Ok, best->id, best_d, c->has_offer, c->id);
        return true;
    }

    std::deque<Watched> deaths_;
    std::set<uint32_t> looted_;
    // loot containers taken (or decided), by id and spawn time (the game gives a later spot the same id), and when
    std::map<std::pair<uint32_t, int64_t>, uint64_t> claimed_;
    std::vector<Skip> skips_;
    std::vector<Sent> pending_;
    std::set<std::pair<uint32_t, uint32_t>> told_full_;
    uint32_t session_ = 0;
};

// Each player's magnet switch, by account (the same in every session), kept in the world's file: "0x<16 hex> on|off"
// per line. Players without a line get the host's default.
class Toggles {
public:
    bool on(uint64_t account, bool fallback) const {
        auto it = map_.find(account);
        return it == map_.end() ? fallback : it->second;
    }
    void set(uint64_t account, bool on) { map_[account] = on; }
    void clear() { map_.clear(); }
    std::string text() const {
        std::string out;
        char b[48];
        for (const auto& [account, on] : map_) {
            snprintf(b, sizeof b, "0x%016llx %s\n", static_cast<unsigned long long>(account), on ? "on" : "off");
            out += b;
        }
        return out;
    }
    void parse(const std::string& text) {
        map_.clear();
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t space = line.find(' ');
            if (space == std::string::npos || line.compare(0, 2, "0x") != 0) continue;
            const std::string hex = line.substr(2, space - 2), word = line.substr(space + 1);
            if (hex.empty() || hex.size() > 16 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) continue;
            if (word != "on" && word != "off") continue;
            map_[std::stoull(hex, nullptr, 16)] = word == "on";
        }
    }

private:
    std::map<uint64_t, bool> map_;
};

}  // namespace magnet
