#pragma once
// Enemy orders (API v39): a creature with an enemy AI sent at any entity for a while, a door or a gate included
// (private spec enemy-targets 2026-11-12, live check). While an order holds, its target is the enemy's pick
// (SelectedTargets slot 0, target and root) with its target switches off (DynamicAggro.allowTargetSwitches), and the
// target has an alarmed aggro entry (aggro kOrderAggro): seen live, a Vukah walked 15 m to a door and swung at it (the
// pick alone did not move it). Written at once and after every server step. An order ends after its time, when its
// target is gone or dead, when the enemy is gone, on cancel, with a new order on the same enemy, or with the world: the
// switches go back on and its entry (and the pick, if still the target) are cleared. Damage: a hit rule adds it to each
// of the enemy's hits on the target (the game's hits on a placed door do 0, seen). Game-free: the world is reached
// through Deps; server world's thread only.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

constexpr size_t kEnemyOrdersMax = 256;
constexpr float kOrderAggro = 100.0f;
constexpr int32_t kOrderDamageMax = 100000;

class EnemyOrders {
public:
    struct Deps {
        // a component of an entity in that world now (writable), its size in *size; null when it has none
        std::function<void*(EmfWorld*, uint32_t hash, uint32_t entity, uint64_t* size)> get;
        std::function<uint64_t()> now_ms;
        // a hit rule {attacker, target, add}: its id, or 0 when refused; and its removal
        std::function<uint32_t(uint32_t attacker, uint32_t target, int32_t add)> add_rule;
        std::function<void(uint32_t id)> remove_rule;
        // the entity's health is known and 0 or less (a door has none: never dead)
        std::function<bool(EmfWorld*, uint32_t entity)> dead;
    };
    explicit EnemyOrders(Deps d) : d_(std::move(d)) {}

    // An order (seconds: 0 = until it ends otherwise; damage: 0 none; flags: EMF_ORDER_*); its id (from 1, never
    // reused), or 0 and *why. Written at once.
    uint32_t add(EmfWorld* world, uint32_t enemy, uint32_t target, double seconds, int32_t damage, uint32_t flags,
                 std::string* why);
    // false: no such order. now: written at once (inside a callback on its world), else it ends at the next step.
    bool cancel(uint32_t id, bool now = true);
    void step(EmfWorld* world);              // after a server step of `world`
    void world_reset();                      // a new server world: every order ends, nothing is written
    uint32_t target_of(EmfWorld* world, uint32_t enemy) const;  // its pick now, 0 none
    size_t count() const { return orders_.size(); }

private:
    struct Order {
        uint32_t id = 0, enemy = 0, target = 0, rule = 0;
        EmfWorld* world = nullptr;
        uint64_t until = 0;  // 0: no time limit
        bool cancelled = false;
    };
    bool write(EmfWorld* w, const Order& o);   // false: the enemy has no AI to write any more
    void finish(const Order& o, bool clear);  // its rule goes; clear: the switches back on, its entry and pick cleared

    Deps d_;
    std::vector<Order> orders_;
    uint32_t next_id_ = 1;
};

}  // namespace emf::core
