// Loot Magnet's game side (mods/loot_magnet/magnet_glue.h): what it reads from a dead entity's components, and the
// loot event it sends, over a fake API.
#include <doctest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../mods/loot_magnet/magnet_glue.h"

namespace gen = emf::gen::ecs;

namespace {
EmfWorld* const kW = reinterpret_cast<EmfWorld*>(0x5000);
std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> g_comps;  // (hash, entity) -> bytes
std::map<std::pair<uint32_t, uint32_t>, uintptr_t> g_tpl;               // (hash, entity) -> template data
void* f_get(EmfWorld*, uint32_t hash, uint32_t entity, uint64_t* size) {
    auto it = g_comps.find({hash, entity});
    if (it == g_comps.end()) return nullptr;
    if (size) *size = it->second.size();
    return it->second.data();
}
void* f_tpl(EmfWorld*, uint32_t hash, uint32_t entity, uint64_t*) {
    auto it = g_tpl.find({hash, entity});
    return it == g_tpl.end() ? nullptr : reinterpret_cast<void*>(it->second);
}
uint8_t f_kind(EmfWorld*) { return EMF_WORLD_SERVER; }
void f_each(EmfWorld*, EmfEntityCallback cb, void* user) {  // every entity that has a component, once
    uint32_t last = 0;
    std::vector<uint32_t> ids;
    for (const auto& [key, bytes] : g_comps) ids.push_back(key.second);
    std::sort(ids.begin(), ids.end());
    for (uint32_t id : ids)
        if (id != last) cb(last = id, user);
}
template <class T>
T& plant(uint32_t entity) {
    auto& v = g_comps[{T::kHash, entity}];
    v.assign(sizeof(T), 0);
    return *reinterpret_cast<T*>(v.data());
}
struct Rig {
    EmfApi api{};
    Rig() {
        g_comps.clear();
        g_tpl.clear();
        api.version = EMF_API_VERSION;
        api.size = sizeof api;
        api.get_component = f_get;
        api.get_template_component = f_tpl;
        api.world_kind = f_kind;
        api.for_each_entity = f_each;
        emf::api_ref() = &api;
    }
    ~Rig() { emf::api_ref() = nullptr; }
};
std::string names(uint32_t item) { return item == 0x11 ? "Material_T1_Animal_Fur" : item == 0x22 ? "Weapon_T1_1H_SwordEpic_01" : ""; }

// The Vukah brawler's corpse as the game leaves it: its template's components that matter, an Inventory added at
// death holding loot, Health 0, no interaction offer.
void brawler_corpse(uint32_t id) {
    auto& t = plant<gen::CurrentTransform>(id);
    t.transform.position.x = int64_t(3739) << 32;
    plant<gen::Health>(id);
    plant<gen::NetworkHealth>(id).healthMax = 1498;  // health 0: dead
    plant<gen::Faction>(id).faction = emf::gen::FactionType::Vukah;
    auto& inv = plant<gen::Inventory>(id);
    inv.slots[0].id.value = 0x11;
    inv.slots[0].data.count = 8;
    g_tpl[{gen::Enemy01BehaviorState::kHash, id}] = 0xABC000;
}
}  // namespace

TEST_CASE("loot magnet glue: a corpse is read as dead, with its stacks' names, its template, and no offer needed") {
    Rig r;
    brawler_corpse(6938);
    const auto b = magnet::read_body(emf::World(kW), 6938, names);
    CHECK(b.exists);
    CHECK(b.dead);
    CHECK(b.has_health);
    CHECK(b.has_inventory);
    CHECK_FALSE(b.has_offer);
    CHECK_FALSE(b.player);
    CHECK(b.faction == 3);
    CHECK(b.tpl == 0xABC000u);
    CHECK(b.items == std::vector<std::string>{"Material_T1_Animal_Fur"});
    CHECK(b.pos.x == doctest::Approx(3739.0));
    const magnet::Death d{6938, b.pos, 0, 1, magnet::template_of(emf::World(kW), 6938)};
    CHECK(magnet::judge(d, b, b.pos, {}, magnet::Rules{}, 2000, false) == magnet::Why::Ok);
}

TEST_CASE("loot magnet glue: owner, tame, villager, player and a living body are read as such") {
    Rig r;
    brawler_corpse(7000);
    plant<gen::OwnerRelationship>(7000).ownerEntityId.id = 1;
    CHECK(magnet::read_body(emf::World(kW), 7000, names).owner == 1u);
    brawler_corpse(7001);
    plant<gen::IsTame>(7001);
    CHECK(magnet::read_body(emf::World(kW), 7001, names).tame);
    brawler_corpse(7002);
    g_tpl[{magnet::kActiveNpcStateHash, 7002}] = 0x1;
    CHECK(magnet::read_body(emf::World(kW), 7002, names).villager);
    brawler_corpse(1);
    plant<gen::PlayerState>(1);
    CHECK(magnet::read_body(emf::World(kW), 1, names).player);
    brawler_corpse(7003);
    plant<gen::NetworkHealth>(7003).health = 50;
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7003, names).dead);
    g_comps.erase({gen::NetworkHealth::kHash, 7003});
    CHECK(magnet::read_body(emf::World(kW), 7003, names).dead);  // no replicated health: the Health slot 0 (0) says dead
    plant<gen::Health>(7003).dataStorage[0] = 50;
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7003, names).dead);
    g_comps.erase({gen::Health::kHash, 7003});
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7003, names).dead);  // no health to read at all: not known dead
    CHECK_FALSE(magnet::read_body(emf::World(kW), 9999, names).exists);
    brawler_corpse(7004);
    plant<gen::Inventory>(7004).slots[2].id.value = 0x77;  // an item the game does not name yet
    const auto b = magnet::read_body(emf::World(kW), 7004, names);
    REQUIRE(b.items.size() == 1);
    CHECK(b.items[0].empty());
}

TEST_CASE("loot magnet glue: the loot event goes guest = the player, host = the corpse, offer 0, loot all, bypass") {
    gen::LootInteractionEvent e{};
    magnet::fill_loot_event(e, 1, 6938);
    CHECK(e.guestId.id == 1u);
    CHECK(e.hostId.id == 6938u);
    CHECK(e.offerId == 0u);
    CHECK(e.lootAll);
    CHECK(e.bypassInteraction);
}

// ---- the loot container most creatures leave where they die ----
namespace {
// LootContainer_Enemy_* as the game spawns it: Inventory (holding the loot), InventorySetup, DefaultInventory,
// DestroyOnLoot and an interaction offer; no Health (the glowing loot spot).
void loot_spot(uint32_t id, double x = 3740) {
    plant<gen::CurrentTransform>(id).transform.position.x = static_cast<int64_t>(x * 4294967296.0);
    auto& inv = plant<gen::Inventory>(id);
    inv.slots[0].id.value = 0x11;
    inv.slots[0].data.count = 3;
    plant<gen::InventorySetup>(id);
    plant<gen::DefaultInventory>(id).applied = true;
    plant<gen::DestroyOnLoot>(id);
    plant<gen::InteractionOfferRuntime>(id);
}
}  // namespace

TEST_CASE("loot magnet glue: the loot container a creature leaves where it died is read as one, with its stacks") {
    Rig r;
    loot_spot(7100);
    const auto b = magnet::read_body(emf::World(kW), 7100, names);
    CHECK(b.exists);
    CHECK(b.container);
    CHECK(b.has_inventory);
    CHECK(b.has_offer);
    CHECK_FALSE(b.has_health);
    CHECK(b.items == std::vector<std::string>{"Material_T1_Animal_Fur"});
    CHECK_FALSE(magnet::read_body(emf::World(kW), 6938, names).container);  // nothing there
    brawler_corpse(6938);
    CHECK_FALSE(magnet::read_body(emf::World(kW), 6938, names).container);  // a creature is no loot container
}

TEST_CASE("loot magnet glue: a player's grave, a pickup, a player, a creature, a plain chest are no loot containers") {
    Rig r;
    // the grave a player leaves (Base_LootContainer_Player_LootCorpse_Tombstone): ForceSave, no DefaultInventory
    plant<gen::CurrentTransform>(7200);
    plant<gen::InventorySetup>(7200).linksEntities[0].id = 7201;
    plant<gen::Inventory>(7201).slots[0].id.value = 0x22;
    plant<gen::InteractionOfferRuntime>(7200);
    plant<gen::DestroyOnLoot>(7200);
    plant<gen::ForceSave>(7200);
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7200, names).container);
    plant<gen::DefaultInventory>(7200);
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7200, names).container);  // still kept with the world: a grave
    loot_spot(7300);
    plant<gen::PickupItem>(7300);  // a loose drop lying on the ground: never picked up
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7300, names).container);
    loot_spot(7301);
    plant<gen::Health>(7301);
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7301, names).container);
    loot_spot(7302);
    plant<gen::PlayerState>(7302);
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7302, names).container);
    loot_spot(7303);
    g_comps.erase({gen::DestroyOnLoot::kHash, 7303});  // a chest that stays when emptied
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7303, names).container);
    loot_spot(7304);
    g_comps.erase({gen::InteractionOfferRuntime::kHash, 7304});  // nothing the loot event can open
    CHECK_FALSE(magnet::read_body(emf::World(kW), 7304, names).container);
}

TEST_CASE("loot magnet glue: loot in linked inventories (an InventorySetup, as on a corpse the game turned into loot) counts") {
    Rig r;
    brawler_corpse(6938);
    g_comps.erase({gen::Inventory::kHash, 6938});
    CHECK_FALSE(magnet::read_body(emf::World(kW), 6938, names).has_inventory);
    plant<gen::InventorySetup>(6938).linksEntities[0].id = 6990;
    auto& inv = plant<gen::Inventory>(6990);
    inv.slots[1].id.value = 0x11;
    inv.slots[1].data.count = 2;
    const auto b = magnet::read_body(emf::World(kW), 6938, names);
    CHECK(b.has_inventory);
    CHECK(b.items == std::vector<std::string>{"Material_T1_Animal_Fur"});
}

TEST_CASE("loot magnet glue: the loot containers near the places watched are found in one walk of the world") {
    Rig r;
    loot_spot(7100, 3740);
    loot_spot(7101, 3790);  // 50 m away
    brawler_corpse(6938);   // a creature, not a container
    const auto found = magnet::containers_near(emf::World(kW), {{3739, 0, 0}}, 10.0, names);
    REQUIRE(found.size() == 1);
    CHECK(found[0].id == 7100u);
    CHECK(found[0].container);
    CHECK(magnet::containers_near(emf::World(kW), {}, 10.0, names).empty());  // nothing watched: no walk
    const auto ids = magnet::container_ids_near(emf::World(kW), {3739, 0, 0}, 10.0);
    CHECK(ids == std::vector<uint32_t>{7100});
}
