// The HUD editor's parts: the 13 scope parts (names, hashes: FNV-1a 64 of the game's UI scope names), the three bars,
// and lookups.
#include <doctest.h>

#include <string>

#include "common/fnv.h"
#include "core/hud_editor/elements.h"

using namespace emf::core::hud;

namespace {
struct Want {
    const char* id;
    const char* scope;
    uint64_t hash;
};
const Want want[13] = {
    {"vitals", "playerStatusParent", 0x9f1a106062b445daull},
    {"buffs", "buffRow", 0xf6702000f3cbf708ull},
    {"hotbar", "doActionbar", 0x9a6bf8b3c5398fefull},
    {"compass", "compassSurvivalMeterParent", 0x83bfdc61be393b46ull},
    {"shroud", "meterParent", 0x472441c51dfafc7aull},
    {"quests", "journalInfo", 0x79a4e6796eec43c4ull},
    {"oxygen", "headDiveAndGliderParent", 0x08f0ee8bd9c41503ull},
    {"loot", "lootLogRoot", 0xf34e480c4ddd9f3bull},
    {"boss", "bossRoot", 0x02e4ad95c4fdaefcull},
    {"crosshair", "crosshairAligner", 0x44025f49f07e2d1full},
    {"chat", "chatParent", 0x87399c5422eeef43ull},
    {"notifications", "notificationParent", 0x0c18cac4b1d25126ull},
    {"multitool", "multiToolRoot", 0xb17f70bc4de3e8acull},
};
}  // namespace

TEST_CASE("fnv1a64: the hash the game passes with a UI scope's name") {
    CHECK(emf::fnv1a64("") == 0xcbf29ce484222325ull);
    CHECK(emf::fnv1a64("doHudCompass()") == 0xb75c8a3e7955cdd4ull);
    CHECK(emf::fnv1a64("doBasicProgressBar()") == 0xb37b692b58e08ab5ull);
    static_assert(emf::fnv1a64("playerStatusParent") == 0x9f1a106062b445daull);
}
TEST_CASE("hud elements: the 13 scope parts of phase 1 in order, each hash fnv1a64 of its scope") {
    REQUIRE(hud_elements().size() == 19);
    for (size_t i = 0; i < 13; ++i) {
        CAPTURE(i);
        CHECK(std::string(hud_elements()[i].id) == want[i].id);
        CHECK(std::string(hud_elements()[i].scope) == want[i].scope);
        CHECK(hud_elements()[i].hash == want[i].hash);
        CHECK(hud_elements()[i].hash == emf::fnv1a64(hud_elements()[i].scope));
        CHECK(std::string(hud_elements()[i].label).size() > 0);
        CHECK(hud_elements()[i].confirm_hide == (i == 0));
        CHECK((hud_elements()[i].note != nullptr) == (i == 0 || i == 2 || i == 3));
        CHECK(hud_elements()[i].bar == -1);
    }
    CHECK(emf::fnv1a64("doPlayStateUi()") == kUiSpaceHash);
}
TEST_CASE("find_scope: each part by its hash, the UI space, nothing else") {
    for (size_t i = 0; i < 13; ++i) CHECK(find_scope(hud_elements()[i].hash) == static_cast<int>(i));
    CHECK(find_scope(kUiSpaceHash) == kUiSpaceIndex);
    CHECK(find_scope(emf::fnv1a64("vitalBars")) == kNoElement);
    CHECK(find_scope(0) == kNoElement);
    CHECK(find_scope(hud_elements()[0].hash ^ 0x100000000ull) == kNoElement);  // same low bits, another hash
}
TEST_CASE("find_element: by id or scope name, ignoring case") {
    CHECK(find_element("vitals") == 0);
    CHECK(find_element("VITALS") == 0);
    CHECK(find_element("playerStatusParent") == 0);
    CHECK(find_element("playerstatusparent") == 0);
    CHECK(find_element("compassSurvivalMeterParent") == 3);
    CHECK(find_element("doActionbar") == 2);
    CHECK(find_element("doHudCompass()") == kNoElement);  // inner scopes are not parts
    CHECK(find_element("multitool") == 12);
    CHECK(find_element("nope") == kNoElement);
    CHECK(find_element("") == kNoElement);
}
TEST_CASE("element_ids: the ids in table order, for replies") {
    CHECK(element_ids() == "vitals, buffs, hotbar, compass, shroud, quests, oxygen, loot, boss, crosshair, chat, "
                           "notifications, multitool, health, mana, stamina, lines_top_right, lines_bottom_right, "
                           "lines_bottom_left");
}

TEST_CASE("hud elements: the 13 scope parts, then health, mana and stamina (bars, no scope hash)") {
    const auto p = hud_elements();
    REQUIRE(p.size() == 19);
    CHECK(std::string(p[0].label) == "Health, mana and stamina");
    for (int b = 0; b < static_cast<int>(kBarCount); ++b) {
        CAPTURE(b);
        REQUIRE(bar_element(b) >= 13);
        const auto& e = p[static_cast<size_t>(bar_element(b))];
        CHECK(e.bar == b);
        CHECK(e.hash == 0);
        CHECK(std::string(e.id) == bar_id(b));
        CHECK(std::string(e.scope) == std::string("bar:") + bar_id(b));
        CHECK(e.note != nullptr);
        CHECK(e.confirm_hide == (b == kBarHealth));
    }
    CHECK(std::string(bar_id(kBarHealth)) == "health");
    CHECK(std::string(bar_id(kBarMana)) == "mana");
    CHECK(std::string(bar_id(kBarStamina)) == "stamina");
    CHECK(bar_element(3) == kNoElement);
    CHECK(bar_element(-1) == kNoElement);
}

TEST_CASE("find_scope never returns a bar; find_element finds them by id or their scope text") {
    CHECK(find_scope(0) == kNoElement);
    CHECK(find_element("Mana") == bar_element(kBarMana));
    CHECK(find_element("bar:stamina") == bar_element(kBarStamina));
    CHECK(find_element("health") == bar_element(kBarHealth));
}

TEST_CASE("valid_order: permutations of 0..2 only") {
    CHECK(valid_order(BarOrder{0, 1, 2}));
    CHECK(valid_order(BarOrder{1, 0, 2}));
    CHECK(valid_order(BarOrder{2, 1, 0}));
    CHECK_FALSE(valid_order(BarOrder{1, 1, 2}));
    CHECK_FALSE(valid_order(BarOrder{0, 1, 3}));
    CHECK(valid_order(kDefaultOrder));
}

TEST_CASE("hud elements: the three mod line parts, one per place, no scope hash") {
    const auto p = hud_elements();
    REQUIRE(p.size() == 19);
    const char* ids[] = {"lines_top_right", "lines_bottom_right", "lines_bottom_left"};
    const char* scopes[] = {"lines:top_right", "lines:bottom_right", "lines:bottom_left"};
    const char* labels[] = {"Mod lines, top right", "Mod lines, bottom right", "Mod lines, bottom left"};
    for (int place = 0; place < 3; ++place) {
        CAPTURE(place);
        const int i = lines_element(place);
        REQUIRE(i == 16 + place);
        const auto& e = p[static_cast<size_t>(i)];
        CHECK(std::string(e.id) == ids[place]);
        CHECK(std::string(e.scope) == scopes[place]);
        CHECK(std::string(e.label) == labels[place]);
        CHECK(e.lines == place);
        CHECK(e.bar == -1);
        CHECK(e.hash == 0);
        CHECK_FALSE(e.confirm_hide);
        CHECK(e.note != nullptr);
        CHECK(find_element(ids[place]) == i);
        CHECK(find_element(scopes[place]) == i);
        CHECK(find_scope(emf::fnv1a64(scopes[place])) == kNoElement);
    }
    for (size_t i = 0; i < 16; ++i) CHECK(p[i].lines == -1);
    CHECK(lines_element(3) == kNoElement);
    CHECK(lines_element(-1) == kNoElement);
}
