#pragma once
// Item names as people type them, for the console and chat commands (give, put, take, stack, items). The game names
// its items with debug names (Material_T1_Wood_Logs, Material_T0_simple_FishingBait); these rules find one from what a
// player types: case ignored, spaces for '_', words in any order, a part of a word is enough ("wood log", "Fishing
// Bait", "FishingBait"). The names the game shows ("Wood Log") are in its text tables, which EMF does not read: a name
// that is only in those ("Hardwood") is not found.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace emf::core {

struct NamedItem {
    uint32_t id = 0;
    std::string name;  // the game's debug name
};

// The words of a debug name or of what was typed, in lower case: split at '_', '-', spaces and where a capital follows a
// small letter ("FishingBait" -> fishing, bait).
std::vector<std::string> item_words(std::string_view text);

struct ItemMatch {
    uint32_t id = 0;              // the one item meant (0: none, or several)
    std::vector<size_t> matches;  // every item that matches, best first (indexes into the list)
};

// The item meant by what was typed: the exact debug name (then in any case), then the item with exactly the words typed,
// then the only item that matches (every word typed is in its name). Of several, the one whose own words (not its
// category or tier: "Material", "T1", "01") are all typed, when only one is; else none, and `matches` names them.
ItemMatch match_item(const std::vector<NamedItem>& items, std::string_view typed);

// For "items": every item that matches what was typed (every item for ""), best first.
std::vector<size_t> search_items(const std::vector<NamedItem>& items, std::string_view typed);

}  // namespace emf::core
