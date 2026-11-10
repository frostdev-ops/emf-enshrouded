#pragma once
// An item's name as the game shows it (its name tag, looked up through GameText), for EMF's screens (English where they
// cannot draw the language's script) or for the game's own text (the player's language always), cached per language.
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/game_text.h"
#include "core/item_names.h"
#include "core/items.h"

namespace emf::core {

constexpr size_t kItemInfoName = 0x48;  // ItemInfo.name: a 16-byte LocaTagReference (a GUID)

// FNV-1a-32 of the name GUID (the game's own rule); 0 for a null GUID or an unreadable pointer.
uint32_t item_name_tag(const uint8_t* info);

enum class NameFor {
    Screen,  // EMF's screens: English where they cannot draw the language's script (Japanese, Korean, Chinese, Thai)
    Game,    // the game's own text (its chat): the player's language always
};

// Whether EMF's screens can draw this language's script now (part 2: an extra font the player added). Until then none.
using ExtraFontReady = std::function<bool(uint32_t language)>;

class ItemNames {
public:
    ItemNames(const GameText& text, ExtraFontReady font) : text_(text), font_(std::move(font)) {}
    // The item's shown name; nothing when it has none, the item is unknown or there is no game text (the dedicated
    // server). Any thread but the render thread.
    std::optional<std::string> name(const uint8_t* registry, uint32_t item, NameFor f);
    // Every item with its debug name and its shown name for EMF's screens ("" when it has none).
    std::vector<NamedItem> named(const uint8_t* registry);
    void set_font_ready(ExtraFontReady font);

private:
    struct Entry {
        std::optional<std::string> shown, english;
        bool english_done = false;
    };
    bool screen_wants_english(uint32_t language);

    const GameText& text_;
    std::mutex mu_;
    ExtraFontReady font_;
    uint64_t key_ = 0;
    std::unordered_map<uint32_t, Entry> cache_;  // by name tag
};

// The core's instance over game_text().
ItemNames& item_names();

}  // namespace emf::core
