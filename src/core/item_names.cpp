#include "core/item_names.h"

#include <algorithm>
#include <cctype>

namespace emf::core {

namespace {

char low(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = low(c);
    return out;
}

bool digits(const std::string& w) {
    return !w.empty() && std::all_of(w.begin(), w.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// A tier or a number in a debug name: T1, lvl3, 01.
bool tier_word(const std::string& w) {
    if (digits(w)) return true;
    if (w.size() >= 2 && w[0] == 't' && digits(w.substr(1))) return true;
    return w.size() >= 4 && w.compare(0, 3, "lvl") == 0 && digits(w.substr(3));
}

// The word a debug name starts with to say what kind of item it is (Material_T1_Wood_Logs, Tool_Torch_01).
bool category_word(const std::string& w) {
    static const char* const kinds[] = {"material", "food",  "tool",  "ammo", "weapon",   "armor",
                                        "armour",   "block", "build", "prop", "resource", "consumable"};
    for (const char* k : kinds)
        if (w == k) return true;
    return false;
}

// An item's own words: its name's words without the category it starts with and without tiers and numbers.
std::vector<std::string> own_words(const std::vector<std::string>& words) {
    std::vector<std::string> out;
    for (size_t i = 0; i < words.size(); ++i)
        if (!tier_word(words[i]) && !(i == 0 && category_word(words[i]))) out.push_back(words[i]);
    return out;
}

std::string joined(const std::vector<std::string>& words) {
    std::string s;
    for (const auto& w : words) s += w;
    return s;
}

struct Scored {
    size_t index = 0;
    bool meant = false;  // every own word of the item was typed (a typed word starts it, or it starts the typed word)
    size_t whole = 0;    // typed words that are whole words of the name
    size_t own = 0, all = 0;
    std::string name;    // lower case
};

// Every item whose name holds every word typed (with the separators removed: "woodlog" and "wood log" both match
// Material_T1_Wood_Logs), best first.
std::vector<Scored> scored_matches(const std::vector<NamedItem>& items, const std::vector<std::string>& typed) {
    std::vector<Scored> out;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto words = item_words(items[i].name);
        if (words.empty()) continue;
        const std::string compact = joined(words);
        bool all = true;
        for (const auto& t : typed) all = all && compact.find(t) != std::string::npos;
        if (!all) continue;
        Scored s;
        s.index = i;
        s.name = lower(items[i].name);
        s.all = words.size();
        const auto own = own_words(words);
        s.own = own.size();
        const auto& need = own.empty() ? words : own;
        s.meant = std::all_of(need.begin(), need.end(), [&](const std::string& w) {
            return std::any_of(typed.begin(), typed.end(), [&](const std::string& t) {
                return w.compare(0, t.size(), t) == 0 || t.compare(0, w.size(), w) == 0;
            });
        });
        for (const auto& t : typed) s.whole += std::find(words.begin(), words.end(), t) != words.end() ? 1 : 0;
        out.push_back(std::move(s));
    }
    std::stable_sort(out.begin(), out.end(), [](const Scored& a, const Scored& b) {
        if (a.meant != b.meant) return a.meant;
        if (a.whole != b.whole) return a.whole > b.whole;
        if (a.own != b.own) return a.own < b.own;
        if (a.all != b.all) return a.all < b.all;
        return a.name < b.name;
    });
    return out;
}

}  // namespace

std::vector<std::string> item_words(std::string_view text) {
    std::vector<std::string> out;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) out.push_back(lower(word));
        word.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '_' || c == '-' || c == ' ' || c == '\t') {
            flush();
            continue;
        }
        const bool capital = c >= 'A' && c <= 'Z';
        if (capital && !word.empty() && word.back() >= 'a' && word.back() <= 'z') flush();  // FishingBait
        word += c;
    }
    flush();
    return out;
}

ItemMatch match_item(const std::vector<NamedItem>& items, std::string_view typed_in) {
    ItemMatch m;
    std::string_view typed = typed_in;
    while (!typed.empty() && (typed.front() == ' ' || typed.front() == '\t')) typed.remove_prefix(1);
    while (!typed.empty() && (typed.back() == ' ' || typed.back() == '\t')) typed.remove_suffix(1);
    if (typed.empty()) return m;
    for (const auto& it : items)  // the exact debug name
        if (it.name == typed) {
            m.id = it.id;
            return m;
        }
    const std::string want = lower(typed);
    for (const auto& it : items)  // in any case
        if (lower(it.name) == want) {
            m.id = it.id;
            return m;
        }
    const auto typed_words = item_words(typed);
    if (typed_words.empty()) return m;
    const auto scored = scored_matches(items, typed_words);
    for (const auto& s : scored) m.matches.push_back(s.index);
    size_t same = 0, meant = 0;  // items with exactly the words typed; items whose own words were all typed
    uint32_t same_id = 0, meant_id = 0;
    for (const auto& s : scored) {
        if (item_words(items[s.index].name) == typed_words) {
            ++same;
            same_id = items[s.index].id;
        }
        if (s.meant) {
            ++meant;
            meant_id = items[s.index].id;
        }
    }
    if (same == 1) m.id = same_id;
    else if (scored.size() == 1) m.id = items[scored[0].index].id;
    else if (meant == 1) m.id = meant_id;
    return m;
}

std::vector<size_t> search_items(const std::vector<NamedItem>& items, std::string_view typed) {
    std::vector<size_t> out;
    for (const auto& s : scored_matches(items, item_words(typed))) out.push_back(s.index);
    return out;
}

}  // namespace emf::core
