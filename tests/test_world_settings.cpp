// World settings: the rules (docs/modding/world_settings.md): the fields of ecs::GameSettings, their ranges and names,
// parsing, reading and writing a 144-byte copy, the crash checks and the block for enshrouded_server.json.
#include <doctest.h>

#include <cstddef>
#include <cstring>
#include <string>

#include "core/world_settings.h"
#include "emf/generated/components.hpp"
#include "world_settings_test_util.h"

using namespace emf::core::ws;
using GS = emf::gen::ecs::GameSettings;

using ws_test::bytes;
using ws_test::defaults;

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

TEST_CASE("world settings: a float setting prints as typed (1.1, not 1.10000002)") {
    auto g = ws_test::defaults();
    const Field& loot = *find("loot");
    write(ws_test::bytes(g), loot, 1.1);
    CHECK(text(loot, read(ws_test::bytes(g), loot)) == "1.1");
    CHECK(export_block(ws_test::bytes(g)).find("\"resourceDropStackAmountFactor\": 1.1,") != std::string::npos);
}

TEST_CASE("world settings: plausible accepts the game's settings and refuses bytes that are not floats, enums or minutes") {
    auto g = ws_test::defaults();
    std::string why;
    CHECK(plausible(ws_test::bytes(g), &why));
    auto f = g;
    f.enemyDamageFactor = 1e20f;
    CHECK_FALSE(plausible(ws_test::bytes(f), &why));
    CHECK(why.find("enemyDamageFactor") != std::string::npos);
    auto m = g;
    m.dayTimeDuration.value = 600LL * 60 * 1000000000LL;  // 600 minutes
    CHECK_FALSE(plausible(ws_test::bytes(m), &why));
}
