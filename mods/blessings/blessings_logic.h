#pragma once
// Flame Blessings' rules, without the game (tested in tests/test_blessings.cpp): the blessings one player has, the
// /blessings command's words, the line on screen, the chat lines, and the roster of players an admin blessed.
//
// Health is one of three: off, refill (hits land, health refills after the step) or god (no damage). God wins: refill
// asked while god mode is on changes nothing, and taking refill away leaves god mode alone.
// Players an admin blessed are kept by their account (emf::player_account), never by their entity id: a player who
// leaves loses their blessings, and a new player who gets the same entity id starts with none.
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace blessings {

enum class Health { Off, Refill, God };

struct Blessings {
    Health health = Health::Off;
    bool stamina = false, mana = false, nofall = false;
    bool any() const { return health != Health::Off || stamina || mana || nofall; }
    bool operator==(const Blessings& o) const {
        return health == o.health && stamina == o.stamina && mana == o.mana && nofall == o.nofall;
    }
    bool operator!=(const Blessings& o) const { return !(*this == o); }
};

inline std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// The five words a blessing goes by in the command (and the wheel).
inline bool is_blessing(const std::string& what) {
    return what == "god" || what == "refill" || what == "stamina" || what == "mana" || what == "nofall";
}

// What players read for each word: chat lines and replies.
inline std::string label(const std::string& what) {
    if (what == "god") return "god mode";
    if (what == "refill") return "endless health";
    if (what == "stamina") return "endless stamina";
    if (what == "mana") return "endless mana";
    if (what == "nofall") return "no fall damage";
    return what;
}

constexpr const char* kUsage =
    "usage: blessings <god|refill|stamina|mana|nofall> <on|off> [#k|name], blessings heal [#k|name], "
    "blessings off [#k|name], blessings list";

inline std::string help_text() {
    return "Flame Blessings (the host and admins):\n"
           "blessings <god|refill|stamina|mana|nofall> <on|off> [#k|name]: one blessing on or off (yours without a name)\n"
           "blessings heal [#k|name]: fill up health, stamina and mana once\n"
           "blessings off [#k|name]: every blessing off\n"
           "blessings list: who has blessings\n"
           "#k is the k-th player, lowest id first. EMF's other cheats: give, put, time, goto, stack";
}

struct Command {
    enum Kind { Help, List, Set, Heal, AllOff, Bad } kind = Bad;
    std::string what;    // Set: god, refill, stamina, mana or nofall
    bool on = false;     // Set
    std::string target;  // "#k", a name, or "" (the sender)
    std::string error;   // Bad: what to tell the sender
};

// The words after "blessings".
inline Command parse_command(const std::vector<std::string>& words) {
    Command c;
    auto rest_from = [&words](size_t i) {
        std::string t;
        for (; i < words.size(); ++i) t += (t.empty() ? "" : " ") + words[i];
        return t;
    };
    auto bad = [&c](const std::string& why) {
        c.kind = Command::Bad;
        c.error = (why.empty() ? "" : why + "\n") + kUsage;
        return c;
    };
    if (words.empty()) {
        c.kind = Command::Help;
        return c;
    }
    const std::string first = lower(words[0]);
    if (first == "help" && words.size() == 1) {
        c.kind = Command::Help;
        return c;
    }
    if (first == "list") {
        if (words.size() != 1) return bad("");
        c.kind = Command::List;
        return c;
    }
    if (first == "heal" || first == "off") {
        c.kind = first == "heal" ? Command::Heal : Command::AllOff;
        c.target = rest_from(1);
        return c;
    }
    if (!is_blessing(first)) return bad("there is no blessing called " + words[0]);
    if (words.size() < 2) return bad("");
    const std::string onoff = lower(words[1]);
    if (onoff != "on" && onoff != "off") return bad("");
    c.kind = Command::Set;
    c.what = first;
    c.on = onoff == "on";
    c.target = rest_from(2);
    return c;
}

// One blessing on or off. God wins over refill.
inline Blessings apply(Blessings b, const std::string& what, bool on) {
    if (what == "god") {
        if (on) b.health = Health::God;
        else if (b.health == Health::God) b.health = Health::Off;
    } else if (what == "refill") {
        if (on && b.health == Health::Off) b.health = Health::Refill;
        else if (!on && b.health == Health::Refill) b.health = Health::Off;
    } else if (what == "stamina") {
        b.stamina = on;
    } else if (what == "mana") {
        b.mana = on;
    } else if (what == "nofall") {
        b.nofall = on;
    }
    return b;
}

// The short names, in the screen line's order ("endless health" for refill).
inline std::vector<std::string> short_names(const Blessings& b) {
    std::vector<std::string> out;
    if (b.health == Health::God) out.push_back("god mode");
    if (b.health == Health::Refill) out.push_back("endless health");
    if (b.stamina) out.push_back("stamina");
    if (b.mana) out.push_back("mana");
    if (b.nofall) out.push_back("no fall damage");
    return out;
}

inline std::string joined(const std::vector<std::string>& parts) {
    std::string t;
    for (const auto& p : parts) t += (t.empty() ? "" : ", ") + p;
    return t;
}

// The host's line on screen: "Blessings: god mode, stamina"; empty with none.
inline std::string hud_text(const Blessings& b) {
    const auto names = short_names(b);
    return names.empty() ? std::string() : "Blessings: " + joined(names);
}

// The chat line for every player. `what` is a blessing word, "heal" or "off" (all off).
inline std::string announce_text(const std::string& sender, const std::string& what, bool on, const std::string& target) {
    std::string act;
    if (what == "heal") act = "full heal for " + target;
    else if (what == "off") act = "all blessings off for " + target;
    else act = label(what) + (on ? " on for " : " off for ") + target;
    return "[Blessings] " + sender + ": " + act;
}

// The "health" setting's values (mod.json): off, refill, god.
inline Health health_of(const std::string& value) {
    if (value == "god") return Health::God;
    if (value == "refill") return Health::Refill;
    return Health::Off;
}
inline const char* health_value(Health h) { return h == Health::God ? "god" : h == Health::Refill ? "refill" : "off"; }

// Whether a blessing word is on.
inline bool has(const Blessings& b, const std::string& what) {
    if (what == "god") return b.health == Health::God;
    if (what == "refill") return b.health == Health::Refill;
    if (what == "stamina") return b.stamina;
    if (what == "mana") return b.mana;
    if (what == "nofall") return b.nofall;
    return false;
}

// The wheel: the five switches (with on or off on the second line), then Fill me up and All off.
constexpr const char* kWheelWords[] = {"god", "refill", "stamina", "mana", "nofall", "heal", "off"};
constexpr int kWheelSwitches = 5, kWheelHeal = 5, kWheelAllOff = 6;
inline std::vector<std::string> wheel_items(const Blessings& b) {
    static const char* const names[] = {"God mode", "Endless health", "Endless stamina", "Endless mana", "No fall damage"};
    std::vector<std::string> out;
    for (int i = 0; i < kWheelSwitches; ++i)
        out.push_back(std::string(names[i]) + (has(b, kWheelWords[i]) ? " (on)" : " (off)"));
    out.push_back("Fill me up");
    out.push_back("All off");
    return out;
}
// A switch picked on the wheel (0-4): it turns over. The wheel picks the health mode, so Endless health while god mode
// is on switches to endless health.
inline Blessings wheel_toggle(Blessings b, int choice) {
    if (choice < 0 || choice >= kWheelSwitches) return b;
    const std::string what = kWheelWords[choice];
    if (has(b, what)) return apply(b, what, false);
    if (what == "refill") {
        b.health = Health::Refill;
        return b;
    }
    return apply(b, what, true);
}

// The players an admin blessed (the host's own blessings are settings), by account.
struct Roster {
    std::map<uint64_t, Blessings> by_account;
};
inline Blessings of(const Roster& r, uint64_t account) {
    auto it = r.by_account.find(account);
    return it == r.by_account.end() ? Blessings{} : it->second;
}
inline void set(Roster& r, uint64_t account, const Blessings& b) {
    if (b.any()) r.by_account[account] = b;
    else r.by_account.erase(account);
}
// Forgets the players who are not here any more.
inline void prune(Roster& r, const std::set<uint64_t>& present) {
    for (auto it = r.by_account.begin(); it != r.by_account.end();)
        it = present.count(it->first) ? std::next(it) : r.by_account.erase(it);
}

// A player in the world: entity id and name ("" when not known).
struct PlayerRef {
    uint32_t entity = 0;
    std::string name;
};
// "#k" (the k-th player, lowest entity id first, as goto counts) or a name (ignoring case). 0 and *err when none fits.
inline uint32_t find_player(const std::string& target, std::vector<PlayerRef> players, std::string* err) {
    std::sort(players.begin(), players.end(), [](const PlayerRef& a, const PlayerRef& b) { return a.entity < b.entity; });
    if (target.size() > 1 && target[0] == '#') {
        const std::string digits = target.substr(1);
        const bool number = digits.size() <= 4 && std::all_of(digits.begin(), digits.end(), [](char c) {
            return c >= '0' && c <= '9';
        });
        const int k = number ? std::stoi(digits) : 0;
        if (k >= 1 && static_cast<size_t>(k) <= players.size()) return players[static_cast<size_t>(k) - 1].entity;
        if (err) *err = "there is no player " + target + " (blessings list shows who is here)";
        return 0;
    }
    std::vector<uint32_t> named;
    for (const auto& p : players)
        if (!p.name.empty() && lower(p.name) == lower(target)) named.push_back(p.entity);
    if (named.size() == 1) return named[0];
    if (err)
        *err = named.empty() ? "there is no player called " + target + " (use #k: #1 is the first player)"
                             : std::to_string(named.size()) + " players are called " + target + ": use #k";
    return 0;
}

}  // namespace blessings
