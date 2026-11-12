// remove_building (remove_building.h).
#include "core/remove_building.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "common/offsets.h"
#include "emf/emf_api.h"
#include "emf/generated/components.hpp"

namespace emf::core {

namespace {
namespace gen = emf::gen::ecs;
constexpr double kFixed = 4294967296.0;  // CurrentTransform: 32.32 fixed-point metres

// The component when it has the generated struct's size; *wrong_size is set when it is there with another size.
template <class T>
const T* comp(const ComponentGetter& get, uint32_t entity, bool* wrong_size = nullptr) {
    uint64_t size = 0;
    const void* p = get ? get(T::kHash, entity, &size) : nullptr;
    if (!p) return nullptr;
    if (size != sizeof(T)) {
        if (wrong_size) *wrong_size = true;
        return nullptr;
    }
    return static_cast<const T*>(p);
}
}  // namespace

RemoveFacts remove_facts(uint32_t entity, const ComponentGetter& get, const ItemInfoGetter& item_info) {
    RemoveFacts f;
    if (!entity || !get) return f;
    uint64_t size = 0;
    if (const void* t = get(gen::CurrentTransform::kHash, entity, &size); t && size >= 24) {
        int64_t raw[3];
        std::memcpy(raw, t, sizeof(raw));
        for (int i = 0; i < 3; ++i) f.pos[i] = static_cast<double>(raw[i]) / kFixed;
        f.has_pos = std::isfinite(f.pos[0]) && std::isfinite(f.pos[1]) && std::isfinite(f.pos[2]);
    }
    f.is_player = get(gen::PlayerState::kHash, entity, nullptr) != nullptr;
    f.is_altar = get(gen::BaseIdComponent::kHash, entity, nullptr) != nullptr;
    if (const auto* u = comp<gen::UsedItem>(get, entity)) f.used_item = u->itemId.value;
    if (f.used_item && item_info) {
        if (const uint8_t* info = item_info(f.used_item)) {
            f.item_known = true;
            f.consume = info[off::kItemInfoPlacementConsume] != 0;
        }
    }
    // The stacks in its own inventory and the ones it links (a chest's slots may live in linked entities).
    std::vector<uint32_t> invs{entity};
    bool odd = false;
    if (const auto* setup = comp<gen::InventorySetup>(get, entity, &odd))
        for (const auto& link : setup->linksEntities)
            if (link.id && link.id != entity) invs.push_back(link.id);
    for (uint32_t id : invs)
        if (const auto* inv = comp<gen::Inventory>(get, id, &odd))
            for (const auto& st : inv->slots) {
                if (!st.id.value) continue;
                ++f.stacks;
                ContentStack c;
                c.item = st.id.value;
                c.count = st.data.count;
                if (st.data.pide.id)
                    if (const auto* is = comp<gen::ItemState>(get, st.data.pide.id)) {
                        c.level = is->itemLevelUi;
                        c.rarity = static_cast<uint8_t>(is->itemRarityUi);
                    }
                f.contents.push_back(c);
            }
    f.stacks_unknown = odd;
    return f;
}

RemovePlan plan_remove(const RemoveFacts& f, uint32_t refund, bool player_ok, uint32_t flags) {
    RemovePlan p;
    if (refund > EMF_REFUND_PLAYER) {
        p.why = "unknown refund (EMF_REFUND_NONE, _DROP or _PLAYER)";
        return p;
    }
    if (flags & ~static_cast<uint32_t>(EMF_REMOVE_FORCE)) {
        p.why = "unknown flags (a newer mod?)";
        return p;
    }
    if (refund == EMF_REFUND_PLAYER && !player_ok) {
        p.why = "the refund's player is not a player in this world";
        return p;
    }
    if (f.is_player) {
        p.why = "that is a player";
        return p;
    }
    if (!f.has_pos) {
        p.why = "no such entity (or it has no position)";
        return p;
    }
    if (f.is_altar) {
        p.why = "that is a Flame Altar: the base itself (every object of the base would be lost at the next save)";
        return p;
    }
    if (!f.used_item) {
        p.why = "not something a player placed (no UsedItem): use destroy to remove it";
        return p;
    }
    if (f.stacks_unknown && (flags & EMF_REMOVE_FORCE)) {  // force would lose what it cannot read
        p.why = "its inventory cannot be read (a game update?): empty it by hand first";
        return p;
    }
    if ((f.stacks || f.stacks_unknown) && !(flags & EMF_REMOVE_FORCE)) {
        p.why = f.stacks_unknown ? "it holds items, or its inventory cannot be read: empty it by hand first"
                                 : "it holds items (" + std::to_string(f.stacks) +
                                       " stacks): empty it, or pass force to remove it with them";
        return p;
    }
    p.ok = true;
    if (refund != EMF_REFUND_NONE && f.item_known && f.consume) p.refund_item = f.used_item;
    return p;
}

}  // namespace emf::core
