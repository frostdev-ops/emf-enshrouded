// remove_building (API v36): the decision before a placed object is removed like a pick-up (fake components).
#include <doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/api.h"
#include "core/api_sizes.h"
#include "common/offsets.h"
#include "core/remove_building.h"
#include "emf/emf_api.h"
#include "emf/generated/components.hpp"

using namespace emf::core;

namespace {
namespace gen = emf::gen::ecs;

constexpr uint32_t kChest = 500, kLinked = 501, kPlayer = 1, kItem = 0x1234abcd;
constexpr size_t kInfoSize = 0x828, kConsumeAt = emf::off::kItemInfoPlacementConsume;

struct Fake {
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> comps;
    std::map<uint32_t, std::vector<uint8_t>> items;

    template <class T>
    T& add(uint32_t entity) {
        auto& v = comps[{T::kHash, entity}];
        v.assign(sizeof(T), 0);
        return *reinterpret_cast<T*>(v.data());
    }
    void item(uint32_t id, bool consume) {
        auto& v = items[id];
        v.assign(kInfoSize, 0);
        v[kConsumeAt] = consume ? 1 : 0;
    }
    ComponentGetter getter() {
        return [this](uint32_t hash, uint32_t entity, uint64_t* size) -> const void* {
            auto it = comps.find({hash, entity});
            if (it == comps.end()) return nullptr;
            if (size) *size = it->second.size();
            return it->second.data();
        };
    }
    ItemInfoGetter item_getter() {
        return [this](uint32_t id) -> const uint8_t* {
            auto it = items.find(id);
            return it == items.end() ? nullptr : it->second.data();
        };
    }
    RemoveFacts facts(uint32_t entity = kChest) { return remove_facts(entity, getter(), item_getter()); }
};

// A placed chest at (10, 20, 30) m whose item is kItem.
Fake placed(bool consume = true) {
    Fake f;
    auto& t = f.add<gen::CurrentTransform>(kChest);
    std::memset(&t, 0, sizeof(t));
    int64_t pos[3] = {int64_t(10) << 32, int64_t(20) << 32, int64_t(30) << 32};
    std::memcpy(&t, pos, sizeof(pos));
    f.add<gen::UsedItem>(kChest).itemId.value = kItem;
    f.item(kItem, consume);
    return f;
}
}  // namespace

TEST_CASE("remove: a placed object is removed and refunds its item when placementConsume is set") {
    Fake f = placed();
    const RemoveFacts facts = f.facts();
    CHECK(facts.has_pos);
    CHECK(facts.pos[0] == doctest::Approx(10));
    CHECK(facts.pos[2] == doctest::Approx(30));
    CHECK(facts.used_item == kItem);
    CHECK(facts.consume);
    const RemovePlan p = plan_remove(facts, EMF_REFUND_DROP, false, 0);
    CHECK(p.ok);
    CHECK(p.why.empty());
    CHECK(p.refund_item == kItem);
    CHECK(plan_remove(facts, EMF_REFUND_PLAYER, true, 0).refund_item == kItem);
}

TEST_CASE("remove: no refund when the item is unknown or not consumed on placement, or refund NONE") {
    Fake f = placed(false);
    RemovePlan p = plan_remove(f.facts(), EMF_REFUND_DROP, false, 0);
    CHECK(p.ok);  // still removed
    CHECK(p.refund_item == 0);
    f.items.clear();
    p = plan_remove(f.facts(), EMF_REFUND_DROP, false, 0);
    CHECK(p.ok);
    CHECK(p.refund_item == 0);
    Fake g = placed();
    p = plan_remove(g.facts(), EMF_REFUND_NONE, false, 0);
    CHECK(p.ok);
    CHECK(p.refund_item == 0);
}

TEST_CASE("remove: refused for no position, a player, no UsedItem, an altar (even with force)") {
    Fake none;
    RemovePlan p = plan_remove(none.facts(), EMF_REFUND_NONE, false, 0);
    CHECK_FALSE(p.ok);
    CHECK(p.why.find("no such entity") != std::string::npos);

    Fake pl = placed();
    pl.add<gen::PlayerState>(kChest);
    p = plan_remove(pl.facts(), EMF_REFUND_NONE, false, EMF_REMOVE_FORCE);
    CHECK_FALSE(p.ok);
    CHECK(p.why.find("player") != std::string::npos);

    Fake world_obj = placed();
    world_obj.comps.erase({gen::UsedItem::kHash, kChest});
    p = plan_remove(world_obj.facts(), EMF_REFUND_NONE, false, 0);
    CHECK_FALSE(p.ok);
    CHECK(p.why.find("destroy") != std::string::npos);  // not placed by a player: points at destroy

    Fake altar = placed();
    altar.add<gen::BaseIdComponent>(kChest);
    p = plan_remove(altar.facts(), EMF_REFUND_NONE, false, EMF_REMOVE_FORCE);
    CHECK_FALSE(p.ok);
    CHECK(p.why.find("Flame Altar") != std::string::npos);
}

TEST_CASE("remove: refused for an unknown refund or flags, and a refund to someone who is not a player") {
    Fake f = placed();
    const RemoveFacts facts = f.facts();
    CHECK_FALSE(plan_remove(facts, 3, false, 0).ok);
    CHECK_FALSE(plan_remove(facts, EMF_REFUND_NONE, false, 0x80).ok);
    const RemovePlan p = plan_remove(facts, EMF_REFUND_PLAYER, false, 0);
    CHECK_FALSE(p.ok);
    CHECK(p.why.find("not a player") != std::string::npos);
}

TEST_CASE("remove: items in its own or a linked inventory refuse it unless force") {
    Fake f = placed();
    f.add<gen::Inventory>(kChest);
    CHECK(f.facts().stacks == 0);
    CHECK(plan_remove(f.facts(), EMF_REFUND_NONE, false, 0).ok);  // an empty chest is fine

    f.add<gen::InventorySetup>(kChest).linksEntities[0].id = kLinked;
    f.add<gen::Inventory>(kLinked).slots[3].id.value = 0x77;  // the items are only in the linked inventory
    CHECK(f.facts().stacks == 1);
    RemovePlan p = plan_remove(f.facts(), EMF_REFUND_NONE, false, 0);
    CHECK_FALSE(p.ok);
    CHECK(p.why.find("holds items") != std::string::npos);
    p = plan_remove(f.facts(), EMF_REFUND_DROP, false, EMF_REMOVE_FORCE);
    CHECK(p.ok);
    CHECK(p.refund_item == kItem);

    reinterpret_cast<gen::Inventory*>(f.comps[{gen::Inventory::kHash, kChest}].data())->slots[0].id.value = 0x55;
    CHECK(f.facts().stacks == 2);
}

TEST_CASE("remove: a component of the wrong size is not trusted (a game update changed it)") {
    Fake f = placed();
    f.comps[{gen::UsedItem::kHash, kChest}].resize(8);
    CHECK(f.facts().used_item == 0);
    f = placed();
    f.add<gen::Inventory>(kChest).slots[0].id.value = 9;
    f.comps[{gen::Inventory::kHash, kChest}].resize(40);
    CHECK(f.facts().stacks_unknown);
    CHECK_FALSE(plan_remove(f.facts(), EMF_REFUND_NONE, false, 0).ok);  // cannot tell: treated as holding items
}

TEST_CASE("api table (v36): remove_building is there") {
    const EmfApi* api = emf::core::api_table();
    CHECK(api->version >= 36u);  // the newest version is checked by its own test (test_path_glue.cpp: v37)
    CHECK(api->size >= kApiV36Size);
    CHECK(api->remove_building != nullptr);
}
