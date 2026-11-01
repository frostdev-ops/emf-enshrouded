// loot_scale: the factor a player sets, applied to the game's "Loot Amount" world setting.
#include <doctest.h>

#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "../mods/loot_scale/loot_scale.h"
#include "../mods/loot_scale/loot_scale_glue.h"

TEST_CASE("loot_scale: the factor stays between 0.1 and 10; nonsense falls back to 1") {
    CHECK(lootscale::clamp_factor(2.0) == doctest::Approx(2.0));
    CHECK(lootscale::clamp_factor(0.01) == doctest::Approx(0.1));
    CHECK(lootscale::clamp_factor(50.0) == doctest::Approx(10.0));
    CHECK(lootscale::clamp_factor(-3.0) == doctest::Approx(0.1));
    CHECK(lootscale::clamp_factor(std::nan("")) == doctest::Approx(1.0));
}

TEST_CASE("loot_scale: the world's own setting is multiplied (so 'Loot Amount' in the world settings still counts)") {
    CHECK(lootscale::scaled(1.0f, 2.0) == doctest::Approx(2.0f));
    CHECK(lootscale::scaled(1.5f, 2.0) == doctest::Approx(3.0f));   // the Relaxed preset's 1.5
    CHECK(lootscale::scaled(0.75f, 1.0) == doctest::Approx(0.75f)); // factor 1: the game's value unchanged
    CHECK(lootscale::scaled(1.0f, 100.0) == doctest::Approx(10.0f)); // clamped
}

// ---- what the hook does not reach: plants picked with E, chopping, mining (the owner: "not when harvesting bushes") ----
using lootscale::Source;
using lootscale::SourceInfo;

namespace {
SourceInfo bush() {  // Scatter_Loot_*_Bush_*: DefaultInventory, an interaction offer, DestroyOnLoot; rolled when picked
    SourceInfo s;
    s.exists = true;
    s.default_inventory = true;
    return s;
}
SourceInfo node(int type) {  // a tree (0, wood) or a rock (1, stone): MiningNode + ResourceNodePickupDrops
    SourceInfo s;
    s.exists = true;
    s.node = type;
    return s;
}
}  // namespace

TEST_CASE("loot_scale: a bush or plant picked with E, a chopped tree and a mined rock are told apart; the rest is the game's") {
    CHECK(lootscale::classify(bush(), "Material_T1_Plant_Fibre") == Source::Harvest);
    CHECK(lootscale::classify(node(0), "Material_T1_Wood_Logs") == Source::Chopping);
    CHECK(lootscale::classify(node(1), "Material_T1_Stone") == Source::Mining);
    SourceInfo fruit_tree = node(0);  // a fruit tree: its fruit is picked with E out of its own inventory
    fruit_tree.inventory = true;
    CHECK(lootscale::classify(fruit_tree, "Food_T2_raw_fruit_Banana") == Source::Harvest);
    CHECK(lootscale::classify(fruit_tree, "Material_T1_Wood_Logs") == Source::Chopping);
    // left to the game's loot setting (the hook scales those when their loot is made) or never scaled:
    SourceInfo pickup = bush();
    pickup.pickup = true;  // twigs and stones on the ground: already scaled by the hook
    CHECK(lootscale::classify(pickup, "Material_T1_Twigs") == Source::Other);
    SourceInfo chest;
    chest.exists = true;
    chest.inventory = true;
    chest.default_inventory = true;  // a chest or the loot spot of a kill: filled with the game's setting
    CHECK(lootscale::classify(chest, "Material_T1_Animal_Fur") == Source::Other);
    SourceInfo creature = bush();
    creature.health = true;
    CHECK(lootscale::classify(creature, "Food_T1_raw_egg") == Source::Other);
    CHECK(lootscale::classify(SourceInfo{}, "Material_T1_Wood_Logs") == Source::Other);  // no source: crafting, gifts
}

TEST_CASE("loot_scale: each source has its own multiplier; 0 (the default) follows the loot amount factor") {
    lootscale::Factors f;
    f.main = 2.0;
    CHECK(lootscale::multiplier(Source::Harvest, f) == doctest::Approx(2.0));
    CHECK(lootscale::multiplier(Source::Chopping, f) == doctest::Approx(2.0));
    CHECK(lootscale::multiplier(Source::Mining, f) == doctest::Approx(2.0));
    CHECK(lootscale::multiplier(Source::Other, f) == doctest::Approx(1.0));  // the hook already did it
    f.harvest = 3.0;
    f.mining = 0.5;
    f.chopping = 50.0;
    CHECK(lootscale::multiplier(Source::Harvest, f) == doctest::Approx(3.0));
    CHECK(lootscale::multiplier(Source::Mining, f) == doctest::Approx(0.5));
    CHECK(lootscale::multiplier(Source::Chopping, f) == doctest::Approx(10.0));  // clamped
    f.main = 1.0;
    f.harvest = 0.0;
    CHECK(lootscale::multiplier(Source::Harvest, f) == doctest::Approx(1.0));
}

TEST_CASE("loot_scale: the game's yield becomes count x multiplier: items to add or to take back, a fraction by chance") {
    CHECK(lootscale::adjust(3, 2.0, 0.0) == 3);   // 3 berries at x2: 3 more
    CHECK(lootscale::adjust(3, 1.0, 0.5) == 0);
    CHECK(lootscale::adjust(1, 1.5, 0.2) == 1);   // 1.5: one more half the time
    CHECK(lootscale::adjust(1, 1.5, 0.7) == 0);
    CHECK(lootscale::adjust(4, 0.5, 0.0) == -2);  // fewer: 2 of the 4 go back
    CHECK(lootscale::adjust(1, 0.1, 0.5) == -1);  // 0.1 of one: gone 9 times in 10
    CHECK(lootscale::adjust(1, 0.1, 0.05) == 0);
    CHECK(lootscale::adjust(5, 10.0, 0.9) == 45);
}

// ---- the source as the mod reads it from the game (over a fake API) ----
namespace {
namespace gen = emf::gen::ecs;
EmfWorld* const kW = reinterpret_cast<EmfWorld*>(0x6000);
std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> g_comps, g_tpls;  // (hash, entity) -> bytes
void* find(std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>>& m, uint32_t hash, uint32_t entity, uint64_t* size) {
    auto it = m.find({hash, entity});
    if (it == m.end()) return nullptr;
    if (size) *size = it->second.size();
    return it->second.data();
}
void* f_get(EmfWorld*, uint32_t hash, uint32_t entity, uint64_t* size) { return find(g_comps, hash, entity, size); }
void* f_tpl(EmfWorld*, uint32_t hash, uint32_t entity, uint64_t* size) { return find(g_tpls, hash, entity, size); }
template <class T>
T& plant(uint32_t entity) {
    auto& v = g_comps[{T::kHash, entity}];
    v.assign(sizeof(T), 0);
    return *reinterpret_cast<T*>(v.data());
}
template <class T>
T& plant_tpl(uint32_t entity) {
    auto& v = g_tpls[{T::kHash, entity}];
    v.assign(sizeof(T), 0);
    return *reinterpret_cast<T*>(v.data());
}
struct ScaleRig {
    EmfApi api{};
    ScaleRig() {
        g_comps.clear();
        g_tpls.clear();
        api.version = EMF_API_VERSION;
        api.size = sizeof api;
        api.get_component = f_get;
        api.get_template_component = f_tpl;
        emf::api_ref() = &api;
    }
    ~ScaleRig() { emf::api_ref() = nullptr; }
};
}  // namespace

TEST_CASE("loot_scale glue: the PickupEvent's source read from the game: a bush, a tree, a rock, a chest, a pickup") {
    ScaleRig r;
    const emf::World w(kW);
    plant<gen::CurrentTransform>(100);  // a berry bush picked with E
    plant<gen::DefaultInventory>(100);
    plant<gen::InteractionOfferRuntime>(100);
    CHECK(lootscale::classify(lootscale::read_source(w, 100), "Food_T1_raw_fruit_Berries") == Source::Harvest);
    plant<gen::CurrentTransform>(101);  // a tree
    plant_tpl<gen::MiningNode>(101).resourceType = gen::GatherResourceType::Wood;
    CHECK(lootscale::read_source(w, 101).node == 0);
    CHECK(lootscale::classify(lootscale::read_source(w, 101), "Material_T1_Wood_Logs") == Source::Chopping);
    plant<gen::CurrentTransform>(102);  // a rock
    plant_tpl<gen::MiningNode>(102).resourceType = gen::GatherResourceType::Stone;
    CHECK(lootscale::classify(lootscale::read_source(w, 102), "Material_T1_Stone") == Source::Mining);
    plant<gen::CurrentTransform>(103);  // a chest (or the loot spot of a kill)
    plant<gen::InventorySetup>(103);
    plant<gen::DefaultInventory>(103);
    CHECK(lootscale::read_source(w, 103).inventory);
    CHECK(lootscale::classify(lootscale::read_source(w, 103), "Material_T1_Animal_Fur") == Source::Other);
    plant<gen::CurrentTransform>(104);  // twigs on the ground
    plant<gen::PickupItem>(104);
    plant<gen::DefaultInventory>(104);
    CHECK(lootscale::read_source(w, 104).pickup);
    CHECK(lootscale::classify(lootscale::read_source(w, 104), "Material_T1_Twigs") == Source::Other);
    CHECK_FALSE(lootscale::read_source(w, 0).exists);    // no source (crafting, EMF's own gifts)
    CHECK_FALSE(lootscale::read_source(w, 999).exists);  // gone
}
