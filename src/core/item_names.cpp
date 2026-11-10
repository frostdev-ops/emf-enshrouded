#include "core/item_names.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>

namespace emf::core {

namespace {

char low(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// Lower case for UTF-8 beyond ASCII (Cyrillic, accented Latin, Greek: the names the game shows), through Windows'
// invariant mapping; the text as it was when it is not UTF-8.
std::string lower_unicode(std::string_view s) {
    const int wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (wn <= 0) return std::string(s);
    std::wstring w(static_cast<size_t>(wn), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), w.data(), wn);
    const int ln = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), wn, nullptr, 0, nullptr, nullptr, 0);
    if (ln <= 0) return std::string(s);
    std::wstring l(static_cast<size_t>(ln), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), wn, l.data(), ln, nullptr, nullptr, 0);
    const int un = WideCharToMultiByte(CP_UTF8, 0, l.data(), ln, nullptr, 0, nullptr, nullptr);
    if (un <= 0) return std::string(s);
    std::string out(static_cast<size_t>(un), '\0');
    WideCharToMultiByte(CP_UTF8, 0, l.data(), ln, out.data(), un, nullptr, nullptr);
    return out;
}

std::string lower(std::string_view s) {
    for (char c : s)
        if (static_cast<unsigned char>(c) >= 0x80) return lower_unicode(s);
    std::string out(s);
    for (char& c : out) c = low(c);
    return out;
}

bool digits(const std::string& w) { return !w.empty() && std::all_of(w.begin(), w.end(), is_digit); }
bool letters(const std::string& w) { return !w.empty() && std::all_of(w.begin(), w.end(), is_alpha); }

// A number word by its value: "01" and "1" are the same number.
std::string number_value(const std::string& w) {
    const size_t nz = w.find_first_not_of('0');
    return nz == std::string::npos ? "0" : w.substr(nz);
}

// A tier, level or number in a debug name: T1, TX, lvl5, 01.
bool tier_word(const std::string& w) {
    if (digits(w) || w == "tx") return true;
    if (w.size() >= 2 && w[0] == 't' && digits(w.substr(1))) return true;
    return w.compare(0, 3, "lvl") == 0 && (w.size() == 3 || digits(w.substr(3)));
}

// The word a debug name starts with to say what kind of item it is (Material_T1_Wood_Logs, Tool_Torch01).
bool category_word(const std::string& w) {
    static const char* const kinds[] = {"material", "food",  "tool",  "ammo", "weapon",   "armor",
                                        "armour",   "block", "build", "prop", "resource", "consumable"};
    for (const char* k : kinds)
        if (w == k) return true;
    return false;
}

// An item's own words: not the category it starts with, not tiers and numbers, not "simple"
// (Material_T0_simple_FishingBait is the plain fishing bait).
std::vector<std::string> own_words(const std::vector<std::string>& words) {
    std::vector<std::string> out;
    for (size_t i = 0; i < words.size(); ++i)
        if (!tier_word(words[i]) && !(i == 0 && category_word(words[i])) && words[i] != "simple")
            out.push_back(words[i]);
    return out;
}

// Every own word of an item was typed: a typed word starts it, or it starts a typed word.
bool own_words_typed(const std::vector<std::string>& words, const std::vector<std::string>& typed) {
    const auto own = own_words(words);
    const auto& need = own.empty() ? words : own;
    return std::all_of(need.begin(), need.end(), [&](const std::string& w) {
        return std::any_of(typed.begin(), typed.end(), [&](const std::string& t) {
            return w.compare(0, t.size(), t) == 0 || t.compare(0, w.size(), w) == 0;
        });
    });
}

std::vector<std::string> by_value(std::vector<std::string> words) {
    for (auto& w : words)
        if (digits(w)) w = number_value(w);
    return words;
}

struct Scored {
    size_t index = 0;
    bool meant = false;      // own_words_typed
    bool whole_all = false;  // every typed word is a whole word of the name
    size_t whole = 0;        // typed words that are whole words of the name
    size_t own = 0, all = 0;
    std::string name;        // lower case
    std::vector<std::string> words;
};

// A typed word in an item's words: a number as a whole number word of the same value ("torch 3" is not T3); another
// word anywhere in the name with its separators removed ("woodlog" and "wood log" both match Material_T1_Wood_Logs).
bool word_in(const std::string& typed, const std::vector<std::string>& words, const std::string& compact) {
    if (!digits(typed)) return compact.find(typed) != std::string::npos;
    const std::string v = number_value(typed);
    return std::any_of(words.begin(), words.end(),
                       [&](const std::string& w) { return digits(w) && number_value(w) == v; });
}

// The names searched: the game's items, or plain names (the console's list, whose ids are index + 1).
struct NameList {
    const std::vector<NamedItem>* items = nullptr;
    const std::vector<std::string>* names = nullptr;
    size_t size() const { return items ? items->size() : names->size(); }
    const std::string& name(size_t i) const { return items ? (*items)[i].name : (*names)[i]; }
    const std::string& shown(size_t i) const {
        static const std::string none;
        return items ? (*items)[i].shown : none;
    }
    uint32_t id(size_t i) const { return items ? (*items)[i].id : static_cast<uint32_t>(i + 1); }
};

std::vector<Scored> scored_matches(const NameList& items, const std::vector<std::string>& typed) {
    std::vector<Scored> out;
    for (size_t i = 0; i < items.size(); ++i) {
        auto words = item_words(items.name(i));
        if (words.empty()) continue;
        std::string compact;
        for (const auto& w : words) compact += w;
        if (!std::all_of(typed.begin(), typed.end(), [&](const std::string& t) { return word_in(t, words, compact); }))
            continue;
        Scored s;
        s.index = i;
        s.name = lower(items.name(i));
        s.all = words.size();
        s.own = own_words(words).size();
        s.meant = own_words_typed(words, typed);
        for (const auto& t : typed)  // (a typed number matches whole number words only)
            s.whole += digits(t) || std::find(words.begin(), words.end(), t) != words.end() ? 1 : 0;
        s.whole_all = s.whole == typed.size();
        s.words = std::move(words);
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

std::string trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return std::string(s);
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
        if (!word.empty()) {
            const char prev = word.back();
            const bool next_letter = i + 1 < text.size() && is_alpha(text[i + 1]);
            if (prev >= 'a' && prev <= 'z' && c >= 'A' && c <= 'Z') flush();                             // FishingBait
            else if (is_digit(c) && letters(word) && word.size() >= 2 && lower(word) != "lvl") flush();  // Torch01
            else if (is_alpha(c) && digits(word) && next_letter) flush();                                // 01COMMON
        }
        word += c;
    }
    flush();
    return out;
}

namespace {
// The items whose shown name (the game's name, in the player's language) holds every typed word (a part of a word is
// enough), and those with exactly the typed words.
void shown_matches(const NameList& items, const std::vector<std::string>& typed, std::vector<size_t>* hits,
                   std::vector<size_t>* same) {
    const auto typed_values = by_value(typed);
    for (size_t i = 0; i < items.size(); ++i) {
        if (items.shown(i).empty()) continue;
        const auto words = item_words(items.shown(i));
        if (words.empty()) continue;
        std::string compact;
        for (const auto& w : words) compact += w;
        if (!std::all_of(typed.begin(), typed.end(), [&](const std::string& t) { return word_in(t, words, compact); }))
            continue;
        hits->push_back(i);
        if (same && by_value(words) == typed_values) same->push_back(i);
    }
}

ItemMatch match_in(const NameList& items, std::string_view typed_in) {
    ItemMatch m;
    const std::string typed = trimmed(typed_in);
    if (typed.empty()) return m;
    const auto typed_words = item_words(typed);
    const auto found = [&](size_t index, ItemHow how) {
        m.id = items.id(index);
        m.how = how;
        m.own_words_typed = own_words_typed(item_words(items.name(index)), typed_words);
    };
    for (size_t i = 0; i < items.size(); ++i)  // the exact debug name
        if (items.name(i) == typed) {
            found(i, ItemHow::Exact);
            return m;
        }
    const std::string want = lower(typed);
    for (size_t i = 0; i < items.size(); ++i)  // in any case
        if (lower(items.name(i)) == want) {
            found(i, ItemHow::Exact);
            return m;
        }
    std::vector<size_t> shown_exact;  // the name the game shows, in any case: one item wins; a shared one goes on
    for (size_t i = 0; i < items.size(); ++i)
        if (!items.shown(i).empty() && lower(items.shown(i)) == want) shown_exact.push_back(i);
    if (shown_exact.size() == 1) {
        m.id = items.id(shown_exact[0]);
        m.how = ItemHow::Exact;
        m.own_words_typed = true;
        return m;
    }
    if (typed_words.empty()) return m;
    const auto scored = scored_matches(items, typed_words);
    for (const auto& s : scored) m.matches.push_back(s.index);
    const auto typed_values = by_value(typed_words);
    std::vector<const Scored*> same, meant, whole;
    for (const auto& s : scored) {
        if (by_value(s.words) == typed_values) same.push_back(&s);
        if (s.meant) meant.push_back(&s);
        if (s.whole_all) whole.push_back(&s);
    }
    if (same.size() == 1) {
        found(same[0]->index, ItemHow::SameWords);
    } else if (scored.size() == 1) {
        found(scored[0].index, ItemHow::Only);
    } else if (meant.size() == 1) {
        // not when it has a typed word only as a part ("wolf" in Wolfsbane) while another match has every typed
        // word whole
        const bool part_only = !meant[0]->whole_all &&
                               std::any_of(whole.begin(), whole.end(), [&](const Scored* s) { return s != meant[0]; });
        if (!part_only) found(meant[0]->index, ItemHow::Meant);
    } else if (meant.empty() && whole.size() == 1) {
        found(whole[0]->index, ItemHow::Whole);
    }
    if (m.id) return m;
    // the debug names decided nothing: the shown names' words (as typed in the player's language)
    std::vector<size_t> shown_hits, shown_same;
    shown_matches(items, typed_words, &shown_hits, &shown_same);
    for (size_t h : shown_hits)
        if (std::find(m.matches.begin(), m.matches.end(), h) == m.matches.end()) m.matches.push_back(h);
    if (shown_hits.size() == 1) {
        m.id = items.id(shown_hits[0]);
        m.how = ItemHow::Only;
        m.own_words_typed = !shown_same.empty();
    } else if (shown_same.size() == 1) {
        m.id = items.id(shown_same[0]);
        m.how = ItemHow::SameWords;
        m.own_words_typed = true;
    }
    return m;
}

std::vector<size_t> search_in(const NameList& items, std::string_view typed) {
    std::vector<size_t> out;
    const auto words = item_words(typed);
    for (const auto& s : scored_matches(items, words)) out.push_back(s.index);
    if (words.empty()) return out;
    std::vector<size_t> hits;  // then the items only their shown name matches
    shown_matches(items, words, &hits, nullptr);
    for (size_t h : hits)
        if (std::find(out.begin(), out.end(), h) == out.end()) out.push_back(h);
    return out;
}
}  // namespace

ItemMatch match_item(const std::vector<NamedItem>& items, std::string_view typed) {
    return match_in(NameList{&items, nullptr}, typed);
}

ItemMatch match_item(const std::vector<std::string>& names, std::string_view typed) {
    return match_in(NameList{nullptr, &names}, typed);
}

std::vector<size_t> search_items(const std::vector<NamedItem>& items, std::string_view typed) {
    return search_in(NameList{&items, nullptr}, typed);
}

std::vector<size_t> search_items(const std::vector<std::string>& names, std::string_view typed) {
    return search_in(NameList{nullptr, &names}, typed);
}

namespace {
struct Reading {
    std::string name;
    std::vector<std::string> numbers;
    ItemMatch match;
    bool strong() const { return match.id && (match.how == ItemHow::Exact || match.how == ItemHow::SameWords); }
    bool same_outcome(const Reading& o) const {  // the same item and numbers (no count is a count of 1)
        const auto n = [](const std::vector<std::string>& v) {
            return v.empty() ? std::vector<std::string>{"1"} : by_value(v);
        };
        return match.id == o.match.id && n(numbers) == n(o.numbers);
    }
};
}  // namespace

ItemArgs read_item_args(const std::vector<NamedItem>& items, const std::vector<std::string>& words, size_t max_numbers) {
    ItemArgs a;
    if (words.empty()) return a;
    std::vector<size_t> splits;  // how many words at the end are the command's numbers
    if (words[0].find(' ') != std::string::npos) {  // "quoted name" [numbers]
        for (size_t i = 1; i < words.size(); ++i) a.bad = a.bad || !digits(words[i]);
        a.bad = a.bad || words.size() - 1 > max_numbers;
        splits.push_back(words.size() - 1);
    } else {
        // a number with a leading zero (01) is never one of the command's numbers
        const auto plain_number = [](const std::string& w) { return digits(w) && (w.size() == 1 || w[0] != '0'); };
        splits.push_back(0);
        for (size_t k = 1; k <= max_numbers && k < words.size() && plain_number(words[words.size() - k]); ++k)
            splits.push_back(k);
    }
    std::vector<Reading> readings;
    for (size_t k : splits) {
        Reading r;
        for (size_t i = 0; i + k < words.size(); ++i) r.name += (i ? " " : "") + words[i];
        r.numbers.assign(words.end() - static_cast<std::ptrdiff_t>(k), words.end());
        r.match = match_item(items, r.name);
        readings.push_back(std::move(r));
    }
    const auto take = [&](const Reading& r) {
        a.id = r.match.id;
        a.name = r.name;
        a.numbers = r.numbers;
        a.match = r.match;
    };
    const auto unclear = [&](const std::vector<const Reading*>& rs) {
        take(*rs.back());
        a.id = 0;
        for (const Reading* r : rs) a.readings.emplace_back(r->match.id, r->numbers);
    };
    const auto all_agree = [](const std::vector<const Reading*>& rs) {
        return std::all_of(rs.begin(), rs.end(), [&](const Reading* r) { return r->same_outcome(*rs[0]); });
    };
    // a split whose name is an item's debug name or has exactly its words
    std::vector<const Reading*> strong;
    for (const auto& r : readings)
        if (r.strong()) strong.push_back(&r);
    if (!strong.empty()) {
        if (all_agree(strong)) take(*strong[0]);
        else unclear(strong);
        return a;
    }
    // else the split with the most numbers, unless one with fewer names something else by all its own words
    const Reading& most = readings.back();
    std::vector<const Reading*> others;
    for (size_t i = 0; i + 1 < readings.size(); ++i)
        if (readings[i].match.id && readings[i].match.own_words_typed) others.push_back(&readings[i]);
    if (most.match.id) {
        std::vector<const Reading*> differ;
        for (const Reading* r : others)
            if (!r->same_outcome(most)) differ.push_back(r);
        if (differ.empty()) {
            take(most);
        } else {
            differ.push_back(&most);
            unclear(differ);
        }
    } else if (!others.empty() && all_agree(others)) {
        take(*others[0]);
    } else {
        take(most);  // no item: the candidates of the name with the numbers taken off
    }
    return a;
}

}  // namespace emf::core
