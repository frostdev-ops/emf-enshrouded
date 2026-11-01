#include "world_settings.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "emf/generated/components.hpp"

namespace emf::core::ws {
namespace {

using GS = emf::gen::ecs::GameSettings;
constexpr double kNsPerMinute = 60e9;

#define WS_OFF(f) static_cast<uint32_t>(offsetof(GS, f))
// Ranges: the dedicated server's readme (the game's own presets resource holds the same limits).
const std::vector<Field> kFields = {
    {"playerHealthFactor", nullptr, WS_OFF(playerHealthFactor), Type::Float, 0.25, 4, {}, true, "Player"},
    {"playerManaFactor", nullptr, WS_OFF(playerManaFactor), Type::Float, 0.25, 4, {}, true, "Player"},
    {"playerStaminaFactor", nullptr, WS_OFF(playerStaminaFactor), Type::Float, 0.25, 4, {}, true, "Player"},
    {"playerBodyHeatFactor", nullptr, WS_OFF(playerBodyHeatFactor), Type::Float, 0.5, 2, {}, true, "Player"},
    {"playerDivingTimeFactor", nullptr, WS_OFF(playerDivingTimeFactor), Type::Float, 0.5, 2, {}, true, "Player"},
    {"enableDurability", "durability", WS_OFF(enableDurability), Type::Bool, 0, 1, {}, true, "Survival"},
    {"enableStarvingDebuff", "starving", WS_OFF(enableStarvingDebuff), Type::Bool, 0, 1, {}, false, "Survival"},
    {"foodBuffDurationFactor", "food_buff", WS_OFF(foodBuffDurationFactor), Type::Float, 0.5, 2, {}, false, "Survival"},
    {"fromHungerToStarving", "hunger_minutes", WS_OFF(fromHungerToStarving), Type::Minutes, 5, 20, {}, false, "Survival"},
    {"shroudTimeFactor", nullptr, WS_OFF(shroudTimeFactor), Type::Float, 0.5, 2, {}, true, "Player"},
    {"tombstoneMode", "tombstone", WS_OFF(tombstoneMode), Type::Enum, 0, 2,
     {"AddBackpackMaterials", "Everything", "NoTombstone"}, false, "Survival"},
    {"enableGliderTurbulences", "glider_turbulence", WS_OFF(enableGliderTurbulences), Type::Bool, 0, 1, {}, true,
     "Survival"},
    {"weatherFrequency", "weather", WS_OFF(weatherFrequency), Type::Enum, 0, 3, {"Disabled", "Rare", "Normal", "Often"},
     false, "World"},
    {"fishingDifficulty", nullptr, WS_OFF(fishingDifficulty), Type::Enum, 0, 4,
     {"VeryEasy", "Easy", "Normal", "Hard", "VeryHard"}, true, "Other"},
    {"miningDamageFactor", "mining", WS_OFF(miningDamageFactor), Type::Float, 0.5, 2, {}, false, "Rates"},
    {"plantGrowthSpeedFactor", "plant_growth", WS_OFF(plantGrowthSpeedFactor), Type::Float, 0.25, 2, {}, false, "Rates"},
    {"resourceDropStackAmountFactor", "loot", WS_OFF(resourceDropStackAmountFactor), Type::Float, 0.25, 2, {}, false,
     "Rates"},
    {"factoryProductionSpeedFactor", "factory_speed", WS_OFF(factoryProductionSpeedFactor), Type::Float, 0.25, 2, {},
     false, "Rates"},
    {"perkUpgradeRecyclingFactor", "salvage_runes", WS_OFF(perkUpgradeRecyclingFactor), Type::Float, 0, 1, {}, true,
     "Rates"},
    {"perkCostFactor", "upgrade_cost", WS_OFF(perkCostFactor), Type::Float, 0.25, 2, {}, true, "Rates"},
    {"experienceCombatFactor", "xp_combat", WS_OFF(experienceCombatFactor), Type::Float, 0.25, 2, {}, false, "Rates"},
    {"experienceMiningFactor", "xp_mining", WS_OFF(experienceMiningFactor), Type::Float, 0, 2, {}, false, "Rates"},
    {"experienceExplorationQuestsFactor", "xp_explore", WS_OFF(experienceExplorationQuestsFactor), Type::Float, 0.25, 2,
     {}, false, "Rates"},
    {"randomSpawnerAmount", "enemy_amount", WS_OFF(randomSpawnerAmount), Type::Enum, 0, 4,
     {"Few", "Normal", "Many", "Extreme", "Automatic"}, false, "World"},
    {"aggroPoolAmount", nullptr, WS_OFF(aggroPoolAmount), Type::Enum, 0, 3, {"Few", "Normal", "Many", "Extreme"}, false,
     "Enemies"},
    {"enemyDamageFactor", nullptr, WS_OFF(enemyDamageFactor), Type::Float, 0.25, 5, {}, false, "Enemies"},
    {"enemyHealthFactor", nullptr, WS_OFF(enemyHealthFactor), Type::Float, 0.25, 4, {}, true, "Enemies"},
    {"enemyStaminaFactor", nullptr, WS_OFF(enemyStaminaFactor), Type::Float, 0.5, 2, {}, true, "Enemies"},
    {"enemyPerceptionRangeFactor", nullptr, WS_OFF(enemyPerceptionRangeFactor), Type::Float, 0.5, 2, {}, false,
     "Enemies"},
    {"bossDamageFactor", nullptr, WS_OFF(bossDamageFactor), Type::Float, 0.2, 5, {}, false, "Enemies"},
    {"bossHealthFactor", nullptr, WS_OFF(bossHealthFactor), Type::Float, 0.2, 5, {}, true, "Enemies"},
    {"threatBonus", nullptr, WS_OFF(threatBonus), Type::Float, 0.25, 4, {}, false, "Enemies"},
    {"pacifyAllEnemies", nullptr, WS_OFF(pacifyAllEnemies), Type::Bool, 0, 1, {}, false, "Enemies"},
    {"tamingStartleRepercussion", nullptr, WS_OFF(tamingStartleRepercussion), Type::Enum, 0, 2,
     {"KeepProgress", "LoseSomeProgress", "LoseAllProgress"}, false, "Other"},
    {"dayTimeDuration", "day_minutes", WS_OFF(dayTimeDuration), Type::Minutes, 2, 60, {}, true, "Time"},
    {"nightTimeDuration", "night_minutes", WS_OFF(nightTimeDuration), Type::Minutes, 2, 60, {}, true, "Time"},
    {"curseModifier", nullptr, WS_OFF(curseModifier), Type::Enum, 0, 2, {"Easy", "Normal", "Hard"}, true, "Enemies"},
    {"flameLevelFogRemoval", "altar_shroud_level", WS_OFF(flameLevelFogRemoval), Type::Byte, 0, 20, {}, true, "World"},
};
#undef WS_OFF

bool same_text(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
        if (x != y) return false;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

bool number(std::string_view s, double* out) {
    const std::string t(s);
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

std::string num(double v) {  // the settings are float32 or whole minutes: 7 digits print what was typed
    char b[48];
    std::snprintf(b, sizeof b, "%.7g", v);
    return b;
}

std::string name_of(const Field& f) { return f.alias ? std::string(f.alias) + " (" + f.key + ")" : f.key; }

}  // namespace

std::span<const Field> fields() { return kFields; }

bool plausible(const uint8_t* gs, std::string* why) {
    if (!crash_safe(gs, why)) return false;
    for (const auto& f : kFields) {
        const double v = read(gs, f);
        bool ok = true;
        if (f.type == Type::Float) ok = std::isfinite(v) && v >= 0 && v <= 10;
        if (f.type == Type::Minutes) ok = v >= 1 && v <= 240;
        if (!ok) {
            if (why) *why = std::string(f.key) + " = " + num(v) + " is not a value the game uses";
            return false;
        }
    }
    return true;
}

const Field* find(std::string_view key_or_alias) {
    const std::string_view k = trim(key_or_alias);
    for (const auto& f : kFields)
        if (same_text(f.key, k) || (f.alias && same_text(f.alias, k))) return &f;
    return nullptr;
}

std::string range_text(const Field& f) {
    switch (f.type) {
        case Type::Bool: return "on or off";
        case Type::Enum: {
            std::string out;
            for (size_t i = 0; i < f.names.size(); ++i)
                out += std::string(i == 0 ? "" : i + 1 == f.names.size() ? " or " : ", ") + f.names[i];
            return out + " (or 0-" + std::to_string(f.names.size() - 1) + ")";
        }
        case Type::Minutes: return num(f.min) + " to " + num(f.max) + " minutes";
        case Type::Byte: return "a whole number from " + num(f.min) + " to " + num(f.max);
        default: return num(f.min) + " to " + num(f.max);
    }
}

Parsed parse(const Field& f, std::string_view raw) {
    Parsed p;
    const std::string_view t = trim(raw);
    const auto bad = [&] {
        p.ok = false;
        p.error = name_of(f) + " must be " + range_text(f);
        return p;
    };
    if (f.type == Type::Bool) {
        for (const char* on : {"1", "on", "true", "yes"})
            if (same_text(t, on)) return {true, 1, ""};
        for (const char* off : {"0", "off", "false", "no"})
            if (same_text(t, off)) return {true, 0, ""};
        return bad();
    }
    if (f.type == Type::Enum) {
        for (size_t i = 0; i < f.names.size(); ++i)
            if (same_text(t, f.names[i])) return {true, static_cast<double>(i), ""};
        double v = 0;
        if (number(t, &v) && v == std::floor(v) && v >= 0 && v < static_cast<double>(f.names.size()))
            return {true, v, ""};
        return bad();
    }
    double v = 0;
    if (!number(t, &v)) return bad();
    if (f.type == Type::Byte && v != std::floor(v)) return bad();
    if (v < f.min - 1e-9 || v > f.max + 1e-9) return bad();
    return {true, v, ""};
}

double read(const uint8_t* gs, const Field& f) {
    switch (f.type) {
        case Type::Float: {
            float x;
            std::memcpy(&x, gs + f.offset, 4);
            return x;
        }
        case Type::Minutes: {
            int64_t ns;
            std::memcpy(&ns, gs + f.offset, 8);
            return static_cast<double>(ns) / kNsPerMinute;
        }
        default: return gs[f.offset];
    }
}

void write(uint8_t* gs, const Field& f, double v) {
    switch (f.type) {
        case Type::Float: {
            const float x = static_cast<float>(v);
            std::memcpy(gs + f.offset, &x, 4);
            break;
        }
        case Type::Minutes: {
            const int64_t ns = static_cast<int64_t>(std::llround(v * kNsPerMinute));
            std::memcpy(gs + f.offset, &ns, 8);
            break;
        }
        default: gs[f.offset] = static_cast<uint8_t>(std::lround(v)); break;
    }
}

std::string text(const Field& f, double v) {
    switch (f.type) {
        case Type::Bool: return v != 0 ? "on" : "off";
        case Type::Enum: {
            const auto i = static_cast<size_t>(std::lround(v));
            return v >= 0 && i < f.names.size() ? f.names[i] : num(v);
        }
        case Type::Minutes: return num(v) + " min";
        default: return num(v);
    }
}

bool crash_safe(const uint8_t* gs, std::string* why) {
    for (const auto& f : kFields) {
        const double v = read(gs, f);
        bool ok = true;
        if (f.type == Type::Enum) ok = v < static_cast<double>(f.names.size());
        else if (f.type == Type::Bool) ok = v <= 1;
        else if (f.type == Type::Minutes) ok = v > 0;
        else if (f.type == Type::Float) ok = std::isfinite(v);
        if (!ok) {
            if (why) *why = std::string(f.key) + " would be " + num(v) + " (it must be " + range_text(f) + ")";
            return false;
        }
    }
    return true;
}

bool equal(const uint8_t* a, const uint8_t* b) {
    for (const auto& f : kFields) {
        const size_t n = f.type == Type::Float ? 4 : f.type == Type::Minutes ? 8 : 1;
        if (std::memcmp(a + f.offset, b + f.offset, n) != 0) return false;
    }
    return true;
}

std::string export_block(const uint8_t* gs) {
    std::string out = "\"gameSettingsPreset\": \"Custom\",\n\"gameSettings\": {\n";
    for (size_t i = 0; i < kFields.size(); ++i) {
        const Field& f = kFields[i];
        std::string v;
        if (f.type == Type::Minutes) {
            int64_t ns;
            std::memcpy(&ns, gs + f.offset, 8);
            v = std::to_string(ns);
        } else if (f.type == Type::Bool) {
            v = gs[f.offset] ? "true" : "false";
        } else if (f.type == Type::Enum) {
            v = "\"" + text(f, gs[f.offset]) + "\"";
        } else {
            v = num(read(gs, f));
        }
        out += "\t\"" + std::string(f.key) + "\": " + v + (i + 1 < kFields.size() ? ",\n" : "\n");
    }
    return out + "}";
}

}  // namespace emf::core::ws
