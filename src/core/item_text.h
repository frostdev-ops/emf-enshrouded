#pragma once
// An item's name as the game shows it (its name tag, looked up through GameText), for EMF's screens (English where they
// cannot draw the language's script) or for the game's own text (the player's language always), cached per language.
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/game_text.h"
#include "core/item_names.h"
#include "core/items.h"
#include "core/screen_glyphs.h"

namespace emf::core {

constexpr size_t kItemInfoName = 0x48;  // ItemInfo.name: a 16-byte LocaTagReference (a GUID)

// FNV-1a-32 of the name GUID (the game's own rule); 0 for a null GUID or an unreadable pointer.
uint32_t item_name_tag(const uint8_t* info);

enum class NameFor {
    Screen,  // EMF's screens: English where they cannot draw the language's script (Japanese, Korean, Chinese, Thai)
    Game,    // the game's own text (its chat): the player's language always
};

// Whether EMF's screens can draw this text (ScreenGlyphs::can_draw): nothing while that is not known yet (before the
// overlay built its fonts, or no overlay); then the names of Japanese, Korean, Chinese and Thai show in English.
using ScreenCanDraw = std::function<std::optional<bool>(std::string_view utf8)>;

class ItemNames {
public:
    ItemNames(const GameText& text, ScreenCanDraw can_draw) : text_(text), can_draw_(std::move(can_draw)) {}
    // The item's shown name; nothing when it has none, the item is unknown or there is no game text (the dedicated
    // server). For EMF's screens: the English name when they cannot draw it. Any thread but the render thread.
    std::optional<std::string> name(const uint8_t* registry, uint32_t item, NameFor f);
    // Every item with its debug name and its shown name for EMF's screens ("" when it has none).
    std::vector<NamedItem> named(const uint8_t* registry);
    void set_can_draw(ScreenCanDraw can_draw);

private:
    struct Entry {
        std::optional<std::string> shown, english;
        bool english_done = false;
    };
    bool screen_wants_english(uint32_t language, const std::optional<std::string>& shown);

    const GameText& text_;
    std::mutex mu_;
    ScreenCanDraw can_draw_;
    uint64_t key_ = 0;
    std::unordered_map<uint32_t, Entry> cache_;  // by name tag
};

// The core's instance over game_text().
ItemNames& item_names();

// The game thread, each step (cheap while nothing changed): once per text table, the characters of every item's name
// in the player's language go to `glyphs` (ScreenGlyphs::want), for the overlay's fonts. Nothing without a registry
// (no world yet: tried again later) or without game text (the dedicated server).
void pump_screen_glyphs(const uint8_t* registry, ItemNames& names, const GameText& text, ScreenGlyphs& glyphs);

}  // namespace emf::core
