// The HUD editor's 13 parts: names, hashes (FNV-1a 64 of the game's UI scope names) and lookups.
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
TEST_CASE("hud elements: the 13 parts of phase 1 in order, each hash fnv1a64 of its scope") {
    REQUIRE(hud_elements().size() == 13);
    for (size_t i = 0; i < 13; ++i) {
        CAPTURE(i);
        CHECK(std::string(hud_elements()[i].id) == want[i].id);
        CHECK(std::string(hud_elements()[i].scope) == want[i].scope);
        CHECK(hud_elements()[i].hash == want[i].hash);
        CHECK(hud_elements()[i].hash == emf::fnv1a64(hud_elements()[i].scope));
        CHECK(std::string(hud_elements()[i].label).size() > 0);
        CHECK(hud_elements()[i].confirm_hide == (i == 0));
        CHECK((hud_elements()[i].note != nullptr) == (i == 0 || i == 2 || i == 3));
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
                           "notifications, multitool");
}
