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
// InventorySetup, DefaultInventory, DestroyOnLoot, an interaction offer, DoNotSave; no Health), told apart from what
// must never be taken: a player's grave (ForceSave, no DefaultInventory), a loose pickup (PickupItem), a player or a
// creature (Health), a chest that stays when emptied (no DestroyOnLoot). Cheap tests first: most entities have no offer.
inline bool is_loot_container(emf::World w, uint32_t id) {
    namespace gen = emf::gen::ecs;
    return w.get<gen::InteractionOfferRuntime>(id) && w.get<gen::DestroyOnLoot>(id) && w.get<gen::DefaultInventory>(id) &&
           !w.get<gen::Health>(id) && !w.get<gen::PlayerState>(id) && !w.get<gen::PickupItem>(id) &&
           !w.get<gen::ForceSave>(id) && has_inventory(w, id);
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

// The game's own "loot in range" event: the corpse's stacks into the player (seen in game: a PickupEvent each).
inline void fill_loot_event(emf::gen::ecs::LootInteractionEvent& e, uint32_t player, uint32_t corpse) {
    e.guestId.id = player;
    e.hostId.id = corpse;
    e.offerId = 0;
    e.lootAll = true;
    e.bypassInteraction = true;
}

}  // namespace magnet
