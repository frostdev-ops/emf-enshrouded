#pragma once
// Loot Magnet's game side, kept apart from loot_magnet.cpp so tests can run it over a fake API: what it reads from a
// dead entity's components, the loot containers it finds where creatures died, and the loot event it sends.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "emf/emf.hpp"
#include "emf/generated/components.hpp"
#include "emf/inventory.hpp"
#include "magnet_logic.h"

namespace magnet {

constexpr uint32_t kActiveNpcStateHash = 0x1a8b5909u;  // villagers' template data
constexpr double kFixed = 4294967296.0;                // positions: 1 m = 2^32

// Who an entity is: its template's Enemy01BehaviorState data (every entity of a template shares it), 0 if it has none
// (then identity is not compared). A reused id belongs to another template, or is alive.
inline uintptr_t template_of(emf::World w, uint32_t id) {
    const EmfApi* a = emf::api();
    if (!a || !a->get_template_component) return 0;
    return reinterpret_cast<uintptr_t>(a->get_template_component(w.raw(), emf::gen::ecs::Enemy01BehaviorState::kHash, id, nullptr));
}

// An inventory of its own, or an InventorySetup that links one (a chest, a corpse the game turned into loot).
inline bool has_inventory(emf::World w, uint32_t id) {
    namespace gen = emf::gen::ecs;
    if (w.get<gen::Inventory>(id)) return true;
    if (auto* setup = w.get<gen::InventorySetup>(id))
        for (const auto& link : setup->linksEntities)
            if (link.id && link.id != id && w.get<gen::Inventory>(link.id)) return true;
    return false;
}

// The loot container a creature leaves where it died (templates LootContainer_Enemy_*, all 76 of them: Inventory,
// InventorySetup, DefaultInventory, DestroyOnLoot, an interaction offer, DoNotSave, SpawnTime; no Health), told apart
// from what must never be taken: a player's grave (CreateItemLoot, the tombstone mark Grave Keeper saw in game; and a
// grave is saved: no DoNotSave, ForceSave on its template), a loose pickup (PickupItem), a player or a creature
// (Health), a chest that stays when emptied (no DestroyOnLoot; chests are saved too). Cheap tests first: most entities
// have no offer.
inline bool is_loot_container(emf::World w, uint32_t id) {
    namespace gen = emf::gen::ecs;
    return w.get<gen::InteractionOfferRuntime>(id) && w.get<gen::DestroyOnLoot>(id) && w.get<gen::DefaultInventory>(id) &&
           w.get<gen::DoNotSave>(id) && !w.get<gen::CreateItemLoot>(id) && !w.get<gen::ForceSave>(id) &&
           !w.get<gen::Health>(id) && !w.get<gen::PlayerState>(id) && !w.get<gen::PickupItem>(id) && has_inventory(w, id);
}

// A dead entity (or a loot container) as the magnet judges it. `name_of(item)` gives an item's debug name ("" when the
// game does not name it yet: a materials-only corpse then waits).
inline Body read_body(emf::World w, uint32_t id, const std::function<std::string(uint32_t)>& name_of) {
    namespace gen = emf::gen::ecs;
    Body b;
    b.id = id;
    auto* t = w.get<gen::CurrentTransform>(id);
    if (!t) return b;
    b.exists = true;
    b.pos = {t->transform.position.x / kFixed, t->transform.position.y / kFixed, t->transform.position.z / kFixed};
    b.player = w.get<gen::PlayerState>(id) != nullptr;
    auto* health = w.get<gen::Health>(id);
    b.has_health = health != nullptr;
    if (auto* n = w.get<gen::NetworkHealth>(id)) b.dead = n->health == 0;
    else b.dead = health && health->dataStorage[0] <= 0;
    b.has_inventory = has_inventory(w, id);
    b.container = is_loot_container(w, id);
    if (auto* st = w.get<gen::SpawnTime>(id)) b.spawned = st->spawnTime.value;
    b.has_offer = w.get<gen::InteractionOfferRuntime>(id) != nullptr;
    if (auto* o = w.get<gen::OwnerRelationship>(id)) b.owner = o->ownerEntityId.id;
    if (auto* f = w.get<gen::Faction>(id)) b.faction = static_cast<int>(f->faction);
    b.tame = w.get<gen::IsTame>(id) != nullptr;
    const EmfApi* a = emf::api();
    b.villager = a && a->get_template_component && a->get_template_component(w.raw(), kActiveNpcStateHash, id, nullptr);
    b.tpl = template_of(w, id);
    for (const auto& s : emf::inventory_stacks(w, id)) b.items.push_back(name_of(s.item));
    return b;
}

// Calls f(id, pos) for each loot container within `radius` metres (horizontally) of one of `places`, in one walk of the
// world. No places, no walk.
template <class F>
void for_each_container_near(emf::World w, const std::vector<Vec3>& places, double radius, F&& f) {
    if (places.empty()) return;
    namespace gen = emf::gen::ecs;
    w.for_each_entity([&](uint32_t id) {
        if (!w.get<gen::InteractionOfferRuntime>(id)) return;  // most entities: one look
        auto* t = w.get<gen::CurrentTransform>(id);
        if (!t) return;
        const Vec3 p{t->transform.position.x / kFixed, t->transform.position.y / kFixed, t->transform.position.z / kFixed};
        bool in_reach = false;
        for (const auto& place : places) in_reach = in_reach || horizontal_distance(p, place) <= radius;
        if (in_reach && is_loot_container(w, id)) f(id, p);
    });
}

// The loot containers near the places the magnet watches, read for judging.
inline std::vector<Body> containers_near(emf::World w, const std::vector<Vec3>& places, double radius,
                                         const std::function<std::string(uint32_t)>& name_of) {
    std::vector<Body> out;
    for_each_container_near(w, places, radius, [&](uint32_t id, const Vec3&) { out.push_back(read_body(w, id, name_of)); });
    return out;
}

// The ids of the loot containers near `place` (taken at a death: those are not its loot).
inline std::vector<uint32_t> container_ids_near(emf::World w, const Vec3& place, double radius) {
    std::vector<uint32_t> out;
    for_each_container_near(w, {place}, radius, [&](uint32_t id, const Vec3&) { out.push_back(id); });
    return out;
}

// How the loot of `host` (a corpse or a loot spot) comes to a player. The game's loot event moves the stacks of its
// own Inventory (gear with its gems and perks); it does not reach inventories an InventorySetup links (seen in game
// with Grave Keeper's tombstones), so plain stacks there are taken out and put into the player, and gear there stays
// (a take would lose its gems and perks).
struct LootPlan {
    bool event = false;              // its own Inventory holds something: send the loot event
    std::vector<emf::Stack> takes;   // plain stacks in linked inventories: take and put
    int gear_left = 0;               // gear stacks in linked inventories: they stay
};
inline LootPlan plan_loot(emf::World w, uint32_t host) {
    LootPlan p;
    for (const auto& st : emf::inventory_stacks(w, host)) {
        if (st.inventory == host) p.event = true;
        else if (st.data_entity) ++p.gear_left;
        else p.takes.push_back(st);
    }
    return p;
}

// Whether `host`'s own Inventory (the part the loot event moves) still holds something.
inline bool holds_own(emf::World w, uint32_t host) {
    for (const auto& st : emf::inventory_stacks(w, host))
        if (st.inventory == host) return true;
    return false;
}

// Takes plain stack `st` (in an inventory `host` links) out of that exact slot and puts it into `player`. What does not
// fit is dropped at the player's feet, else put back into `host`: never lost. done(came, dropped) at the end. False
// when the take was refused at once (an older core, not the server world).
template <class Done>
bool move_linked(emf::World w, uint32_t host, uint32_t player, const emf::Stack& st, Done done) {
    const uint32_t item = st.item;
    return emf::take_items(
        w, host, item, st.count, emf::slot_id(st.inventory, st.slot),
        [host, player, item, done](emf::World w2, uint32_t taken) {
            if (!taken) {
                done(0u, 0u);
                return;
            }
            auto rest = [host, player, item](emf::World w3, uint32_t n) -> uint32_t {  // dropped at the player's feet
                if (!n) return 0;
                if (emf::api()->give_item && emf::api()->give_item(w3.raw(), player, item, n)) return n;
                if (!emf::put_items_ex(w3, host, item, n, 0, 0xFF, [](emf::World, uint32_t) {}))
                    EMF_LOG("loot_magnet", "LOST: %u x item %08x could be neither put into player %u nor dropped", n,
                            item, player);
                return 0;
            };
            const bool queued =
                emf::put_items_ex(w2, player, item, taken, 0, 0xFF, [taken, done, rest](emf::World w3, uint32_t added) {
                    const uint32_t came = added < taken ? added : taken;
                    done(came, rest(w3, taken - came));
                });
            if (!queued) done(0u, rest(w2, taken));
        },
        true);
}

// The game's own "loot in range" event: the corpse's stacks into the player (seen in game: a PickupEvent each).
inline void fill_loot_event(emf::gen::ecs::LootInteractionEvent& e, uint32_t player, uint32_t corpse) {
    e.guestId.id = player;
    e.hostId.id = corpse;
    e.offerId = 0;
    e.lootAll = true;
    e.bypassInteraction = true;
}

}  // namespace magnet
