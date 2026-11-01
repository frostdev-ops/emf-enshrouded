// World settings: the rules (docs/modding/world_settings.md): the fields of ecs::GameSettings, their ranges and names,
// parsing, reading and writing a 144-byte copy, the crash checks and the block for enshrouded_server.json.
#include <doctest.h>

#include <cstddef>
#include <cstring>
#include <string>

#include "core/world_settings.h"
#include "emf/generated/components.hpp"

using namespace emf::core::ws;
using GS = emf::gen::ecs::GameSettings;

namespace {
// The game's defaults (types.json), as a world on the Default preset has them.
GS defaults() {
    GS g{};
    g.playerHealthFactor = g.playerManaFactor = g.playerStaminaFactor = g.playerBodyHeatFactor = 1;
    g.playerDivingTimeFactor = g.foodBuffDurationFactor = g.shroudTimeFactor = 1;
    g.enableDurability = true;
    g.fromHungerToStarving.value = 600'000'000'000;
    g.enableGliderTurbulences = true;
    g.weatherFrequency = static_cast<emf::gen::ecs::WeatherFrequency>(2);
    g.fishingDifficulty = static_cast<emf::gen::ecs::GenericDifficulty>(2);
    g.miningDamageFactor = g.plantGrowthSpeedFactor = g.resourceDropStackAmountFactor = g.factoryProductionSpeedFactor = 1;
    g.perkUpgradeRecyclingFactor = 0.5f;
    g.perkCostFactor = g.experienceCombatFactor = g.experienceMiningFactor = g.experienceExplorationQuestsFactor = 1;
    g.randomSpawnerAmount = static_cast<emf::gen::ecs::RandomSpawnerAmount>(4);
    g.aggroPoolAmount = static_cast<emf::gen::ecs::AggroPoolAmount>(1);
    g.enemyDamageFactor = g.enemyHealthFactor = g.enemyStaminaFactor = g.enemyPerceptionRangeFactor = 1;
    g.bossDamageFactor = g.bossHealthFactor = g.threatBonus = 1;
    g.tamingStartleRepercussion = static_cast<emf::gen::ecs::TamingStartleRepercussion>(1);
    g.dayTimeDuration.value = 1'800'000'000'000;
    g.nightTimeDuration.value = 720'000'000'000;
    g.curseModifier = static_cast<emf::gen::ecs::CurseModifier>(1);
    g.flameLevelFogRemoval = 11;
    return g;
}
uint8_t* bytes(GS& g) { return reinterpret_cast<uint8_t*>(&g); }
}  // namespace

TEST_CASE("world settings: 38 fields at the generated struct's offsets, found by key or alias") {
    CHECK(fields().size() == 38);
    REQUIRE(find("loot") != nullptr);
    CHECK(find("loot") == find("resourceDropStackAmountFactor"));
    REQUIRE(find("DAY_MINUTES") != nullptr);
    CHECK(find("DAY_MINUTES")->offset == offsetof(GS, dayTimeDuration));
    CHECK(find("weather")->offset == offsetof(GS, weatherFrequency));
    CHECK(find("altar_shroud_level")->offset == offsetof(GS, flameLevelFogRemoval));
    CHECK(find("nope") == nullptr);
    for (const auto& f : fields()) CHECK(f.offset < sizeof(GS));
}

TEST_CASE("world settings: parse refuses out of range and names the range") {
    CHECK(parse(*find("loot"), "2").value == doctest::Approx(2));
    const Parsed big = parse(*find("loot"), "10");
    CHECK_FALSE(big.ok);
    CHECK(big.error.find("0.25") != std::string::npos);
    CHECK(big.error.find("2") != std::string::npos);
    CHECK_FALSE(parse(*find("day_minutes"), "0").ok);
    CHECK(parse(*find("day_minutes"), "2").value == doctest::Approx(2));
    CHECK(parse(*find("weather"), "disabled").value == 0);
    CHECK(parse(*find("weather"), "3").value == 3);
    CHECK_FALSE(parse(*find("weather"), "4").ok);
    CHECK_FALSE(parse(*find("fishingDifficulty"), "9").ok);
    CHECK(parse(*find("durability"), "off").value == 0);
    CHECK(parse(*find("durability"), "true").value == 1);
    CHECK_FALSE(parse(*find("loot"), "abc").ok);
    CHECK_FALSE(parse(*find("altar_shroud_level"), "21").ok);
    CHECK_FALSE(parse(*find("altar_shroud_level"), "2.5").ok);  // a whole number
}

TEST_CASE("world settings: minutes are written as keen::Time nanoseconds") {
    GS g = defaults();
    write(bytes(g), *find("day_minutes"), 2);
    CHECK(g.dayTimeDuration.value == 120'000'000'000);
    CHECK(read(bytes(g), *find("day_minutes")) == doctest::Approx(2));
    write(bytes(g), *find("loot"), 1.5);
    CHECK(g.resourceDropStackAmountFactor == doctest::Approx(1.5));
    write(bytes(g), *find("tombstone"), 2);
    CHECK(static_cast<int>(g.tombstoneMode) == 2);
    CHECK(text(*find("tombstone"), 2) == "NoTombstone");
    CHECK(text(*find("day_minutes"), 2) == "2 min");
}

TEST_CASE("world settings: crash_safe checks enums, bools and durations, not the readme ranges") {
    GS g = defaults();
    std::string why;
    CHECK(crash_safe(bytes(g), &why));
    g.resourceDropStackAmountFactor = 3.0f;  // above the readme's 2, as a preset may have it
    CHECK(crash_safe(bytes(g), &why));
    g.fishingDifficulty = static_cast<emf::gen::ecs::GenericDifficulty>(9);
    CHECK_FALSE(crash_safe(bytes(g), &why));
    CHECK(why.find("fishingDifficulty") != std::string::npos);
    g = defaults();
    g.dayTimeDuration.value = 0;
    CHECK_FALSE(crash_safe(bytes(g), &why));
}

TEST_CASE("world settings: equal ignores padding") {
    GS a = defaults(), b = defaults();
    std::memset(b._pad1, 0xAB, sizeof b._pad1);
    std::memset(b._pad4, 0xCD, sizeof b._pad4);
    CHECK(equal(bytes(a), bytes(b)));
    b.threatBonus = 2;
    CHECK_FALSE(equal(bytes(a), bytes(b)));
}

TEST_CASE("world settings: export block is pasteable JSON") {
    GS g = defaults();
    const std::string block = export_block(bytes(g));
    CHECK(block.find("\"gameSettingsPreset\": \"Custom\"") != std::string::npos);
    CHECK(block.find("\"dayTimeDuration\": 1800000000000") != std::string::npos);
    CHECK(block.find("\"weatherFrequency\": \"Normal\"") != std::string::npos);
    CHECK(block.find("\"enableDurability\": true") != std::string::npos);
    CHECK(block.find("\"flameLevelFogRemoval\": 11") != std::string::npos);
}
