#pragma once
// remove_building (API v36): removing a placed object the way a player's pick-up does
// (private research building_removal_2026-11-12.md). The decision is pure and tested with fake components; the API
// glue (api.cpp) destroys the entity and gives the refund.
#include <cstdint>
#include <functional>
#include <string>

namespace emf::core {

// A component of an entity, or null; *size gets its size in bytes.
using ComponentGetter = std::function<const void*(uint32_t hash, uint32_t entity, uint64_t* size)>;
// The game's ItemInfo blob for an item id (off::kItemInfoSize bytes), or null.
using ItemInfoGetter = std::function<const uint8_t*(uint32_t item)>;

constexpr uint32_t kItemInfoPlacementConsume = 0x43C;  // ItemInfo.placementConsume (bool): the item is used up when placed

// What the core reads about an entity before removing it.
struct RemoveFacts {
    bool has_pos = false;
    double pos[3] = {};  // metres
    bool is_player = false;
    bool is_altar = false;     // BaseIdComponent: a Flame Altar, the base itself
    uint32_t used_item = 0;    // UsedItem.itemId: the item a player placed it with (0: not placed by a player)
    bool item_known = false;   // that item is in the registry
    bool consume = false;      // and placing it used it up (the game refunds only then)
    uint32_t stacks = 0;       // item stacks in its own and linked inventories (InventorySetup.linksEntities)
    bool stacks_unknown = false;  // an inventory of an unexpected size: cannot tell whether it is empty
};

RemoveFacts remove_facts(uint32_t entity, const ComponentGetter& get, const ItemInfoGetter& item_info);

struct RemovePlan {
    bool ok = false;
    std::string why;           // why it is refused ("" when ok)
    uint32_t refund_item = 0;  // the item to give back (0: none)
};

// refund: EMF_REFUND_*; player_ok: the refund's player is a player (EMF_REFUND_PLAYER); flags: EMF_REMOVE_*.
RemovePlan plan_remove(const RemoveFacts& f, uint32_t refund, bool player_ok, uint32_t flags);

}  // namespace emf::core
