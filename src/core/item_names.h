#pragma once
// Item names as people type them, for the console and chat commands (give, put, take, stack, items). The game names
// its items with debug names (Material_T1_Wood_Logs, Material_T0_simple_FishingBait, Tool_Torch01); these rules find
// one from what a player types: case ignored, spaces for '_', words in any order, a part of a word is enough ("wood
// log", "Simple Bait", "torch"). The names the game shows ("Wood Log") are in its text tables, which EMF does not read:
// a name that is only there is not found.
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace emf::core {

struct NamedItem {
    uint32_t id = 0;
    std::string name;  // the game's debug name
};

// The words of a debug name or of what was typed, in lower case: split at '_', '-' and spaces, where a capital follows a
// small letter (FishingBait), where a number follows a word of two or more letters (Torch01; not lvl5) and where a word
// of two or more letters follows a number (01COMMON; not 1H or 2m).
std::vector<std::string> item_words(std::string_view text);

// How an item was found.
enum class ItemHow {
    None,       // no item: none matches, or several
    Exact,      // the debug name, in any case
    SameWords,  // exactly the words typed (numbers by value: "tool torch 1" is Tool_Torch01)
    Only,       // the only item that matches
    Meant,      // the only match whose own words were all typed
    Whole,      // the only match that has every word typed as a whole word
};

struct ItemMatch {
    uint32_t id = 0;              // the one item meant (0: none, or several)
    ItemHow how = ItemHow::None;
    bool own_words_typed = false; // every own word of that item was typed (not only its category, tier or number)
    std::vector<size_t> matches;  // every item that matches, best first (indexes into the list)
};

// The item meant by what was typed. An item matches when its name holds every word typed (a number only as a whole
// number word: "torch 3" is not T3). Then, in this order: the exact debug name; the item with exactly the words typed;
// the only match; the only match whose own words (not its category, tier, numbers or "simple": "Material", "T1", "01")
// were all typed, unless it has a typed word only as a part ("wolf" in Wolfsbane) while another match has every typed
// word whole (Trophy_Wolf); the only match with every typed word whole ("hardwood": Material_T2_Wood_Hardwood, not
// Fence_HardWood). Else none, and `matches` lists them.
ItemMatch match_item(const std::vector<NamedItem>& items, std::string_view typed);

// For "items": every item that matches what was typed (every item for ""), best first.
std::vector<size_t> search_items(const std::vector<NamedItem>& items, std::string_view typed);

// A command's words after the command name ("fishing bait 4", "tool torch 01 5"): the item and the numbers after it.
// Up to max_numbers plain numbers at the end may be the command's numbers; one with a leading zero (01) always belongs
// to the name. Every way to split is read: a split whose name is an item's debug name or has exactly its words wins
// (two such splits that disagree are unclear); else the split with the most numbers, unless a split with fewer names a
// different item by all its own words (unclear too). A first word with a space in it (it was "quoted") is the name.
struct ItemArgs {
    uint32_t id = 0;                   // the item (0: none, see match; or unclear, see readings)
    std::vector<std::string> numbers;  // the command's numbers, after the name
    std::string name;                  // the words read as the name
    ItemMatch match;                   // that name's match (its candidates when there is no item)
    bool bad = false;                  // a quoted name followed by too many numbers, or by a word that is not one
    std::vector<std::pair<uint32_t, std::vector<std::string>>> readings;  // unclear: each reading's item and numbers
};
ItemArgs read_item_args(const std::vector<NamedItem>& items, const std::vector<std::string>& words, size_t max_numbers);

}  // namespace emf::core
