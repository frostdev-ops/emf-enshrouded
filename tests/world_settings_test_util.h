#pragma once
// A GameSettings struct with the game's defaults (types.json), as a world on the Default preset has them.
#include <cstdint>

#include "emf/generated/components.hpp"

namespace ws_test {
using GS = emf::gen::ecs::GameSettings;

inline GS defaults() {
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
inline uint8_t* bytes(GS& g) { return reinterpret_cast<uint8_t*>(&g); }
}  // namespace ws_test
