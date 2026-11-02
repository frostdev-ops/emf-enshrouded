#pragma once
// Flame Blessings' game side, kept apart from blessings.cpp so tests run it over a fake API (tests/test_blessings.cpp).
// Everything here runs on the server world's thread (its ticks, commands and the wheel's answer), except
// settings_changed and set_switched_off, which any thread may call.
//
// Each server step, for every blessed player:
//   - endless health (refill or god), stamina and mana: attribute_add(max - current) when below the maximum, never for
//     a dead player (no one is brought back); the game picks the change up on its next step;
//   - god mode: CombatFlags.immune set (server only, not saved), again every step; the value it had before is kept per
//     account and put back when god mode ends, the mod is switched off, or the player leaves (then nothing is written:
//     the entity is gone or someone else's);
//   - no fall damage: DynamicFallDamage.fallStartAltitude set to the player's height, so a landing measures no fall.
// The host's blessings are the Mods-tab settings (the local player of a world this game hosts; a dedicated server has
// none). An admin's /blessings on another player is kept by account until that player leaves or the world closes.
#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "blessings_logic.h"
#include "emf/emf.hpp"
#include "emf/generated/components.hpp"
#include "emf/query.hpp"

namespace blessings {

constexpr const char* kMod = "blessings";
constexpr const char* kHudId = "blessings.hud";
constexpr const char* kUnavailable = "not available on this game version";

inline uint32_t corner_of(const std::string& value) {
    if (value == "top_right") return EMF_HUD_TOP_RIGHT;
    if (value == "bottom_right") return EMF_HUD_BOTTOM_RIGHT;
    return EMF_HUD_BOTTOM_LEFT;
}

// mod.json's settings.
struct Settings {
    Blessings host;
    bool hud = true;
    uint32_t corner = EMF_HUD_BOTTOM_LEFT;
    bool announce = true;
    std::string wheel_key = "F4";
};
inline Settings read_settings() {
    Settings s;
    s.host.health = health_of(emf::setting_text(kMod, "health", "off"));
    s.host.stamina = emf::setting_bool(kMod, "stamina", false);
    s.host.mana = emf::setting_bool(kMod, "mana", false);
    s.host.nofall = emf::setting_bool(kMod, "no_fall_damage", false);
    s.wheel_key = emf::setting_text(kMod, "wheel_key", "F4");
    s.hud = emf::setting_bool(kMod, "hud", true);
    s.corner = corner_of(emf::setting_text(kMod, "corner", "bottom_left"));
    s.announce = emf::setting_bool(kMod, "announce", true);
    return s;
}

// Refills work on this game build: the core has attribute_get / attribute_add (API v18) and found their functions (they
// are off on a build EMF has not checked, after a game update). The same test emf::kill makes.
inline bool attributes_on() { return emf::detail::attributes_with(emf::api()); }

// Who typed the command: a player in the game's chat (their entity), else the console or the host.
struct Sender {
    bool from_chat = false;
    uint32_t entity = 0;
};

class Glue {
public:
    void load() { s_ = read_settings(); }
    void settings_changed() { dirty_ = true; }
    void set_switched_off(bool off) { off_ = off; }
    bool switched_off() const { return off_; }
    const Settings& settings() const { return s_; }
    std::vector<std::string> wheel_items() const { return blessings::wheel_items(s_.host); }

    void tick(emf::World w) {
        if (!w.is_server()) return;
        refresh();
        new_session_check();
        if (off_) {
            if (!cleaned_) {
                restore_all(w);
                roster_.by_account.clear();
                show("");
                cleaned_ = true;
            }
            return;
        }
        cleaned_ = false;
        if (roster_.by_account.empty() && !s_.host.any() && held_.empty()) {  // nothing to do
            told_ = false;
            show("");
            return;
        }
        const auto players = emf::players(w);
        std::map<uint32_t, uint64_t> account;
        std::set<uint64_t> present;
        for (uint32_t p : players) present.insert(account[p] = account_of(w, p));
        prune(roster_, present);
        const bool attrs = attributes_on();
        bool wants_attrs = false, host_here = false;
        std::set<uint64_t> god;
        for (uint32_t p : players) {
            const bool host = is_host(p);
            host_here = host_here || host;
            const Blessings b = host ? s_.host : of(roster_, account[p]);
            if (!b.any()) continue;
            const bool health = b.health != Health::Off;
            wants_attrs = wants_attrs || b.health == Health::Refill || b.stamina || b.mana;
            if (attrs && (health || b.stamina || b.mana)) fill(w, p, health, b.stamina, b.mana);
            if (b.health == Health::God) {
                if (!hold_immune(w, p, account[p])) missing(p, "CombatFlags", "god mode", &warned_flags_);
                god.insert(account[p]);
            }
            if (b.nofall) {
                auto* fall = w.get<emf::gen::ecs::DynamicFallDamage>(p);
                const auto at = emf::position(w, p);
                if (fall && at) fall->fallStartAltitude = static_cast<float>(at->y);
                else if (!fall) missing(p, "DynamicFallDamage", "no fall damage", &warned_fall_);
            }
        }
        for (auto it = held_.begin(); it != held_.end();) {
            if (god.count(it->first)) {
                ++it;
                continue;
            }
            restore(w, it->first, it->second);
            it = held_.erase(it);
        }
        if (wants_attrs && !attrs) {
            if (!told_) {
                const std::string text = std::string("Flame Blessings: endless health, stamina and mana are ") + kUnavailable +
                                         " (god mode and no fall damage still work)";
                EMF_NOTIFY("%s", text.c_str());
                EMF_LOG(kMod, "%s", text.c_str());
                told_ = true;
            }
        } else if (!wants_attrs) {
            told_ = false;
        }
        show(host_here && s_.hud ? hud_text(s_.host) : "");
    }

    // The /blessings command: the words after its name. Returns the reply.
    std::string command(emf::World w, const std::vector<std::string>& words, const Sender& from) {
        if (off_) return "Flame Blessings is switched off";
        if (!w.is_server()) return "Flame Blessings runs where the world runs (the host or a dedicated server)";
        refresh();
        new_session_check();
        const Command c = parse_command(words);
        if (c.kind == Command::Help) return help_text();
        if (c.kind == Command::Bad) return c.error;
        if (c.kind == Command::List) return list(w);
        std::string err;
        const uint32_t target = resolve(w, c.target, from, &err);
        if (!target) return "Flame Blessings: " + err;
        const std::string who = name_of(w, target);
        const std::string sender = from.from_chat ? name_of(w, from.entity) : "Host";
        if (c.kind == Command::Heal) {
            if (!attributes_on()) return std::string("Flame Blessings: a full heal is ") + kUnavailable;
            fill(w, target, true, true, true);
            announce(w, sender, "heal", true, who);
            return "Flame Blessings: " + who + " is filled up";
        }
        const Blessings before = blessings_of(w, target);
        if (c.kind == Command::AllOff) {
            store(w, target, Blessings{});
            if (before.any()) announce(w, sender, "off", false, who);
            return "Flame Blessings: all blessings off for " + who;
        }
        if (c.on && needs_attributes(c.what) && !attributes_on())
            return "Flame Blessings: " + label(c.what) + " is " + kUnavailable;
        const Blessings after = apply(before, c.what, c.on);
        store(w, target, after);
        if (after != before) announce(w, sender, c.what, c.on, who);
        return "Flame Blessings: " + label(c.what) + (c.on ? " on" : " off") + " for " + who +
               " (now: " + (after.any() ? joined(short_names(after)) : std::string("none")) + ")";
    }

    // The wheel's answer (on the server world): choice 0-6 of wheel_items(), -1 = cancelled. Acts on the host.
    void wheel_pick(emf::World w, int choice) {
        if (off_ || !w.is_server()) return;
        refresh();
        new_session_check();
        const uint32_t host = emf::local_player();
        if (!host || choice < 0 || choice > kWheelAllOff) return;
        const std::string who = name_of(w, host);
        const Blessings before = s_.host;
        if (choice == kWheelHeal) {
            if (!attributes_on()) {
                EMF_NOTIFY("Flame Blessings: Fill me up is %s", kUnavailable);
                return;
            }
            fill(w, host, true, true, true);
            announce(w, "Host", "heal", true, who);
            return;
        }
        if (choice == kWheelAllOff) {
            set_host(Blessings{});
            if (before.any()) announce(w, "Host", "off", false, who);
            return;
        }
        const std::string what = kWheelWords[choice];
        const Blessings after = wheel_toggle(before, choice);
        const bool on = has(after, what);
        if (on && needs_attributes(what) && !attributes_on()) {
            EMF_NOTIFY("Flame Blessings: %s is %s", label(what).c_str(), kUnavailable);
            return;
        }
        set_host(after);
        if (after != before) announce(w, "Host", what, on, who);
    }

private:
    struct Held {  // a player's CombatFlags.immune before god mode set it
        uint32_t entity = 0;
        bool before = false;
    };

    static bool needs_attributes(const std::string& what) { return what == "refill" || what == "stamina" || what == "mana"; }

    void refresh() {
        if (dirty_.exchange(false)) s_ = read_settings();
    }
    // Another world, or a reload: entities and their CombatFlags are new (never saved), the admins' blessings end.
    void new_session_check() {
        const uint32_t session = emf::server_session();
        if (session == session_) return;
        session_ = session;
        roster_.by_account.clear();
        held_.clear();
        warned_flags_ = warned_fall_ = false;
    }
    // A blessed player lacks the component a blessing writes (a game update?): said once per world session, never
    // silently skipped.
    static void missing(uint32_t p, const char* component, const char* blessing, bool* warned) {
        if (*warned) return;
        *warned = true;
        EMF_LOG(kMod, "player %u has no %s: %s does nothing for them on this game build", p, component, blessing);
        EMF_NOTIFY("Flame Blessings: %s does not work on this game version", blessing);
    }
    static bool is_host(uint32_t p) {
        const uint32_t host = emf::local_player();
        return host && p == host;
    }
    static uint64_t account_of(emf::World w, uint32_t p) {
        const uint64_t a = emf::player_account(w, p);
        return a ? a : 0x100000000ull + p;  // no account known (a test world): the entity stands in
    }
    Blessings blessings_of(emf::World w, uint32_t p) const { return is_host(p) ? s_.host : of(roster_, account_of(w, p)); }
    void store(emf::World w, uint32_t p, const Blessings& b) {
        if (is_host(p)) set_host(b);
        else set(roster_, account_of(w, p), b);
    }
    // The host's blessings are settings: changed the way the Mods tab changes them, so the tab shows them.
    void set_host(const Blessings& b) {
        const Blessings old = s_.host;
        if (b.health != old.health) emf::set_setting(kMod, "health", health_value(b.health));
        if (b.stamina != old.stamina) emf::set_setting(kMod, "stamina", b.stamina ? "1" : "0");
        if (b.mana != old.mana) emf::set_setting(kMod, "mana", b.mana ? "1" : "0");
        if (b.nofall != old.nofall) emf::set_setting(kMod, "no_fall_damage", b.nofall ? "1" : "0");
        s_.host = b;
    }
    static std::string name_of(emf::World w, uint32_t p) {
        if (const auto n = emf::player_name(w, p)) return *n;
        return is_host(p) ? "Host" : "player #" + std::to_string(p);
    }
    // A chat line for every player, when that is on and someone else is there to read it.
    void announce(emf::World w, const std::string& sender, const std::string& what, bool on, const std::string& who) {
        const std::string text = announce_text(sender, what, on, who);
        EMF_LOG(kMod, "%s", text.c_str());
        if (s_.announce && emf::players(w).size() > 1) emf::broadcast(text);
    }
    static void fill(emf::World w, uint32_t p, bool health, bool stamina, bool mana) {
        const auto hp = emf::attribute(w, p, EMF_ATTR_HEALTH);
        if (hp && *hp <= 0) return;  // dead
        auto top = [&](uint32_t cur_id, uint32_t max_id) {
            const auto cur = emf::attribute(w, p, cur_id), max = emf::attribute(w, p, max_id);
            if (cur && max && *max > *cur) emf::attribute_add(w, p, cur_id, *max - *cur);
        };
        if (health) top(EMF_ATTR_HEALTH, EMF_ATTR_MAX_HEALTH);
        if (stamina) top(EMF_ATTR_STAMINA, EMF_ATTR_MAX_STAMINA);
        if (mana) top(EMF_ATTR_MANA, EMF_ATTR_MAX_MANA);
    }
    bool hold_immune(emf::World w, uint32_t p, uint64_t account) {
        auto* flags = w.get<emf::gen::ecs::CombatFlags>(p);
        if (!flags) return false;
        if (!held_.count(account)) held_[account] = Held{p, flags->immune};
        flags->immune = true;
        return true;
    }
    // Puts immune back, only on the same player (entity and account) still in the world.
    static void restore(emf::World w, uint64_t account, const Held& h) {
        if (!w.get<emf::gen::ecs::PlayerState>(h.entity) || account_of(w, h.entity) != account) return;
        if (auto* flags = w.get<emf::gen::ecs::CombatFlags>(h.entity)) flags->immune = h.before;
    }
    void restore_all(emf::World w) {
        for (const auto& [account, h] : held_) restore(w, account, h);
        held_.clear();
    }
    void show(const std::string& text) {
        if (text == hud_text_ && (text.empty() || s_.corner == hud_corner_)) return;
        emf::hud(kHudId, text, s_.corner);
        hud_text_ = text;
        hud_corner_ = s_.corner;
    }
    static uint32_t resolve(emf::World w, const std::string& target, const Sender& from, std::string* err) {
        if (target.empty()) {
            const uint32_t p = from.from_chat ? from.entity : emf::local_player();
            if (!p)
                *err = from.from_chat ? "EMF could not tell which player you are: name one (#k or a name)"
                                      : "there is no \"you\" here: name a player (#k or a name)";
            return p;
        }
        std::vector<PlayerRef> list;
        for (uint32_t p : emf::players(w)) list.push_back({p, emf::player_name(w, p).value_or("")});
        return find_player(target, list, err);
    }
    std::string list(emf::World w) const {
        std::string out;
        for (uint32_t p : emf::players(w)) {
            const Blessings b = blessings_of(w, p);
            if (b.any()) out += (out.empty() ? "" : "; ") + name_of(w, p) + " (" + joined(short_names(b)) + ")";
        }
        return out.empty() ? "Flame Blessings: nobody has blessings right now" : "Flame Blessings: " + out;
    }

    Settings s_;
    Roster roster_;
    std::map<uint64_t, Held> held_;
    std::atomic<bool> dirty_{false}, off_{false};
    bool cleaned_ = false;  // switched off: immune put back and the line removed
    bool told_ = false;     // "not available" said since something last needed the attributes
    bool warned_flags_ = false, warned_fall_ = false;  // a missing component said (this world session)
    uint32_t session_ = 0;
    std::string hud_text_;
    uint32_t hud_corner_ = EMF_HUD_BOTTOM_LEFT;
};

}  // namespace blessings
