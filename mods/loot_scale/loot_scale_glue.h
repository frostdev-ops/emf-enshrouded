#pragma once
// loot_scale's game side, kept apart so tests can run it over a fake API: what the entity a PickupEvent came from is.
#include <cstdint>

#include "emf/emf.hpp"
#include "emf/generated/components.hpp"
#include "loot_scale.h"

namespace lootscale {

inline SourceInfo read_source(emf::World w, uint32_t id) {
    namespace gen = emf::gen::ecs;
    SourceInfo s;
    if (!id || !w.get<gen::CurrentTransform>(id)) return s;
    s.exists = true;
    s.pickup = w.get<gen::PickupItem>(id) != nullptr;
    s.inventory = w.get<gen::Inventory>(id) || w.get<gen::InventorySetup>(id);
    s.default_inventory = w.get<gen::DefaultInventory>(id) != nullptr;
    s.health = w.get<gen::Health>(id) || w.get<gen::PlayerState>(id);
    if (const auto* n = w.get_template<gen::MiningNode>(id)) s.node = static_cast<int>(n->resourceType);
    return s;
}

}  // namespace lootscale
