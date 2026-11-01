// loot_scale: more (or fewer) materials per loot stack, on top of the world's own "Loot Amount" setting.
//
// The game reads that setting through one small function (BalancingRegistry -> GameSettings
// .resourceDropStackAmountFactor) whenever it fills a container, applies an enemy's or chest's default inventory, or
// drops a world pickup (docs/engine_parts/re_content.md, "Loot"). This mod hooks that function by its bytes (API v14
// hook_pattern) and multiplies what it returns. Nothing is written into the world: switch the mod off and the world's
// setting is what it was. On a dedicated server or a game you host it applies to every player (loot is the host's).
// The pattern is in loot_scale.patterns too: "emf_sigcheck --patterns loot_scale.patterns <exe>" checks it after an
// update without starting the game.
//
// Some loot never reads that setting: a plant or bush picked with E, and trees and rocks hit with a tool. For those the
// mod waits for the game's PickupEvent (what a player got, and from which entity) and gives the difference with the
// game's own inventory transaction (put_items_ex), or takes it back for a factor under 1 (take_items, API v11). Each of
// the three has its own factor, 0 = the same as the main one. The rules are loot_scale.h (unit-tested).
#include <windows.h>

#include <atomic>
#include <map>
#include <mutex>
#include <random>
#include <string>

#include "emf/emf.hpp"
#include "emf/generated/components.hpp"
#include "emf/inventory.hpp"
#include "loot_scale.h"
#include "loot_scale_glue.h"

namespace {
constexpr const char* kMod = "loot_scale";
// mov rax, [rcx+0x90] (BalancingRegistry -> GameSettings); movss xmm0, [rax+0x38] (resourceDropStackAmountFactor); ret
constexpr const char* kLootFactor = "48 8B 81 90 00 00 00 F3 0F 10 40 38 C3";

using GetFactorFn = float (*)(void* balancing);
GetFactorFn g_original = nullptr;
std::atomic<double> g_factor{2.0};

std::atomic<bool> g_told{false};

std::mutex g_factors_lock;  // the setting callback may run on any thread
lootscale::Factors g_factors;

float detour(void* balancing) {
    const float game = g_original(balancing);
    const float ours = lootscale::scaled(game, g_factor.load());
    if (!g_told.exchange(true))  // once: shows the hook works (the first loot after loading)
        EMF_LOG(kMod, "first loot: the world's loot amount %.2f, with this mod %.2f", game, ours);
    return ours;
}

void read_settings() {
    const double f = lootscale::clamp_factor(emf::setting_float(kMod, "factor", 2.0));
    if (f != g_factor.load()) EMF_LOG(kMod, "loot amount x%.2f", f);
    g_factor = f;
    lootscale::Factors fs;
    fs.main = f;
    fs.harvest = emf::setting_float(kMod, "harvest", 0.0);
    fs.chopping = emf::setting_float(kMod, "chopping", 0.0);
    fs.mining = emf::setting_float(kMod, "mining", 0.0);
    std::lock_guard<std::mutex> l(g_factors_lock);
    g_factors = fs;
}

lootscale::Factors factors() {
    std::lock_guard<std::mutex> l(g_factors_lock);
    return g_factors;
}

// ---- server world thread only (the PickupEvent callback) ----
std::map<uint32_t, std::string> g_names;  // item id -> debug name
std::mt19937 g_rng{std::random_device{}()};
bool g_first[4] = {};                     // the first pickup of each source was logged

const std::string& item_name(emf::World w, uint32_t item) {
    auto it = g_names.find(item);
    if (it != g_names.end()) return it->second;
    EmfItemInfo info{};
    info.size = sizeof info;
    static const std::string unknown;
    if (!emf::api()->item_info(w.raw(), item, &info) || !info.name[0]) return unknown;  // ask again later
    return g_names.emplace(item, info.name).first->second;
}

void on_pickup(emf::World w, const emf::gen::ecs::PickupEvent& e) {
    namespace gen = emf::gen::ecs;
    const uint32_t player = e.pickedUpByEntityId.id, from = e.pickedUpFromEntityId.id;
    const uint32_t item = e.itemId.value, count = e.itemCount;
    if (!from || !item || !count || player < 1 || player > 1023 || !w.get<gen::PlayerState>(player)) return;
    const lootscale::SourceInfo source = lootscale::read_source(w, from);
    const lootscale::Source kind = lootscale::classify(source, item_name(w, item));
    if (kind == lootscale::Source::Other) return;  // the game's loot setting (the hook) or not loot at all
    const double m = lootscale::multiplier(kind, factors());
    const int n = lootscale::adjust(count, m, std::uniform_real_distribution<double>(0.0, 1.0)(g_rng));
    if (!g_first[static_cast<int>(kind)]) {
        g_first[static_cast<int>(kind)] = true;
        EMF_LOG(kMod, "first %s: %u x %s from %u, x%.2f: %+d", lootscale::source_name(kind), count,
                item_name(w, item).c_str(), from, m, n);
    }
    if (n > 0) {
        if (!emf::put_items_ex(w, player, item, static_cast<uint32_t>(n), 0, 0xFF, [](emf::World, uint32_t) {}))
            EMF_LOG(kMod, "could not give %d more of item %08x to player %u", n, item, player);
    } else if (n < 0) {
        const uint64_t slot = emf::slot_id(e.targetSlotId.entityId.id, e.targetSlotId.slotIndex);
        emf::take_items(w, player, item, static_cast<uint32_t>(-n), slot, [](emf::World, uint32_t) {});
    }
}
}  // namespace

extern "C" __declspec(dllexport) bool emf_mod_load(const EmfApi* api) {
    if (!emf::init(api)) return false;
    if (!emf::has_v14()) {
        EMF_LOG(kMod, "needs EMF API v14 (hook_pattern)");
        return false;
    }
    read_settings();
    if (!emf::hook_pattern("loot_scale.factor", kLootFactor, reinterpret_cast<void*>(&detour),
                           reinterpret_cast<void**>(&g_original))) {
        EMF_LOG(kMod, "the game's loot amount function was not found or could not be hooked (see above): off");
        return false;
    }
    emf::on_setting_changed(kMod, [](emf::World, const std::string&) { read_settings(); });
    // plants picked with E, chopping and mining: the game reports what a player got, the mod gives the difference
    if (!emf::on_event<emf::gen::ecs::PickupEvent>(EMF_WORLD_SERVER, on_pickup))
        EMF_LOG(kMod, "could not watch pickups: harvesting, chopping and mining keep the game's amounts");
    const lootscale::Factors f = factors();
    EMF_LOG(kMod, "on: loot amount x%.2f (on top of the world's Loot Amount setting); harvesting x%.2f, chopping x%.2f, "
                  "mining x%.2f", g_factor.load(), lootscale::multiplier(lootscale::Source::Harvest, f),
            lootscale::multiplier(lootscale::Source::Chopping, f), lootscale::multiplier(lootscale::Source::Mining, f));
    return true;
}
