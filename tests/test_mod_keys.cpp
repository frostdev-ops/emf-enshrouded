// The shipped mods' default keys, side by side: no two mods start on the same key (a player who switches both on
// would get two things from one press). Each mod's own test checks its key against the game's bindings
// (game_bindings.h).
#include <windows.h>

#include <doctest.h>

#include <string>

#include "game_bindings.h"

TEST_CASE("shipped mods: no two mods take the same key by default") {
    const auto keys = game_bindings::shipped_keys();
    REQUIRE(keys.size() >= 5);  // the source tree was found
    for (const auto& [vk, users] : keys) {
        std::string list;
        for (const auto& u : users) list += (list.empty() ? "" : ", ") + u;
        CAPTURE(list);
        CHECK(users.size() == 1);
    }
}

TEST_CASE("shipped mods: the wheels sit on the free F-keys: Necromancer F2, Beastmaster F3, Companion F6, Hand-over F7, "
          "Bulk Craft F8") {
    const auto keys = game_bindings::shipped_keys();
    auto owner = [&keys](uint32_t vk) { return keys.count(vk) ? keys.at(vk).front() : std::string("(nobody)"); };
    CHECK(owner(VK_F2) == "necromancer.wheel_key");
    CHECK(owner(VK_F3) == "beastmaster.wheel_key");
    CHECK(owner(VK_F6) == "companion.key");
    CHECK(owner(VK_F7) == "handover.key");
    CHECK(owner(VK_F8) == "bulk_craft.key");
    CHECK(owner('L') == "quick_stack.key");
    CHECK(owner('K') == "villager_jobs.key");
}

TEST_CASE("bulk_craft: the wheel key is F8 by default, a key the game leaves free (U is its slow walk)") {
    const std::string key = game_bindings::manifest_default("mods/bulk_craft/mod.json", "key");
    CHECK(key == "F8");
    CHECK_FALSE(game_bindings::is_game_key(key));
}

TEST_CASE("game bindings: the list holds the preset's keys and leaves K and the free F-keys out") {
    for (const char* k : {"G", "H", "U", "E", "L", "1", "0", "Tab", "Space", "Enter", "F1", "F5", "F12", "Delete",
                          "PageUp", "PageDown"}) {
        CAPTURE(k);
        CHECK(game_bindings::is_game_key(std::string(k)));
    }
    CHECK(game_bindings::is_game_key(static_cast<uint32_t>(VK_ESCAPE)));
    for (const char* k : {"K", "F2", "F3", "F4", "F6", "F7", "F8", "F9", "F10", "F11", "Numpad1", "Insert", ""}) {
        CAPTURE(k);
        CHECK_FALSE(game_bindings::is_game_key(std::string(k)));
    }
}
