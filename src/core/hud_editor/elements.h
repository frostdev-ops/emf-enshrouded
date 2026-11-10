#pragma once
// The HUD parts the editor can move, scale and hide (docs/engine_parts/re_ui_scopes.md): each is the outermost named
// UI scope of one part of the game's HUD. The game passes the FNV-1a 64 hash of the scope's name with every open.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace emf::core::hud {

struct HudElement {
    const char* id;     // short name for commands and replies ("vitals")
    const char* scope;  // the game's UI scope name
    const char* label;  // what the page shows
    const char* note;   // one line under the label, or nullptr
    uint64_t hash;      // fnv1a64(scope)
    bool confirm_hide;  // the page asks before hiding it
    int bar;            // -1: a scope part; 0 health, 1 mana, 2 stamina: a bar of the vitals (moved by the bar detour,
                        // no scope: hash 0)
};

constexpr size_t kHudElementCount = 16;
constexpr uint64_t kUiSpaceHash = 0xf5139eefe5b90316ull;  // "doPlayStateUi()", the UI space
constexpr int kNoElement = -1, kUiSpaceIndex = -2;

std::span<const HudElement> hud_elements();

// The vitals' bars, in the order the game draws them (docs/engine_parts/re_ui_scopes.md).
constexpr size_t kBarCount = 3;
constexpr int kBarHealth = 0, kBarMana = 1, kBarStamina = 2;
// The element index of a bar, kNoElement for anything else.
int bar_element(int bar);
// "health", "mana", "stamina"; "" for anything else.
const char* bar_id(int bar);
// The order the bars are stacked in, top first (bar numbers).
using BarOrder = std::array<uint8_t, kBarCount>;
constexpr BarOrder kDefaultOrder{0, 1, 2};
bool valid_order(const BarOrder& order);  // a permutation of 0..2
// The part with this scope-name hash, kUiSpaceIndex for the UI space, else kNoElement. No locks, no allocation.
int find_scope(uint64_t hash);
// A part by its id or its scope name, ignoring case; kNoElement when there is none.
int find_element(std::string_view id_or_scope);
// "vitals, buffs, ..." in table order.
std::string element_ids();

}  // namespace emf::core::hud
