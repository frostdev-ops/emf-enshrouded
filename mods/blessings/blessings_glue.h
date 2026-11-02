#pragma once
// Flame Blessings' game side, kept apart from blessings.cpp so tests run it over a fake API (tests/test_blessings.cpp).
// Everything here runs on the server world's thread (its ticks, commands and the wheel's answer), except
// settings_changed, set_switched_off and a client world's tick, which may come from any thread.
//
// Each server step, for every blessed player:
//   - endless health (refill or god), stamina and mana: attribute_add(max - current) when below the maximum, never for
//     a dead player (no one is brought back); the game picks the change up on its next step;
//   - god mode: the minimum of the player's Health held at its maximum, so a hit cannot take health below it (the
//     game's attribute formulas keep health within minimum and maximum), plus the no-fall-damage reset (fall damage
//     writes Health directly). Players have no CombatFlags (seen in game), so its immune flag cannot be used. Health
//     is laid out [0] health, [1] minimum, [2] base maximum, [3] maximum adder, ..., [7] maximum
//     (mods/beastmaster/health.lua);
//     slots 0 and 7 are checked against the game's own attribute reads before slot 1 is written. The minimum it had
//     before is kept per entity and put back when god mode ends, the mod is switched off or the player leaves (then
//     nothing is written: the entity is gone or someone else's). Health is not saved (an attribute component:
//     server_only, do_not_save; docs/engine_parts/combat.md 4.1); a minimum above 0 found the first time EMF looks at
//     a player in a session is set back to 0 anyway (logged), in case one was left behind;
//   - no fall damage: DynamicFallDamage.fallStartAltitude lowered to the player's height (never raised), so a landing
//     measures no fall.
// The host's blessings are the Mods-tab settings (the local player of a world this game hosts; a dedicated server has
// none). An admin's /blessings on another player is kept by account until that player leaves or the world closes.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
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
constexpr int kMissingTicks = 60;      // a component missing this many steps in a row is said (a new player first loads)
constexpr int kScanTicks = 60;         // with nobody blessed, players are looked at this often (the left-behind check)
constexpr uint64_t kHudStaleMs = 2000; // no server step for this long (someone else's game): the screen line goes

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
    std::string wheel_key = "F11";
};
inline Settings read_settings() {
    Settings s;
    s.host.health = health_of(emf::setting_text(kMod, "health", "off"));
    s.host.stamina = emf::setting_bool(kMod, "stamina", false);
    s.host.mana = emf::setting_bool(kMod, "mana", false);
    s.host.nofall = emf::setting_bool(kMod, "no_fall_damage", false);
    s.wheel_key = emf::setting_text(kMod, "wheel_key", "F11");
    s.hud = emf::setting_bool(kMod, "hud", true);
    s.corner = corner_of(emf::setting_text(kMod, "corner", "bottom_left"));
    s.announce = emf::setting_bool(kMod, "announce", true);
    return s;
}

// The game's attribute functions work on this game build: the core has attribute_get / attribute_add (API v18) and
// found their functions (they are off on a build EMF has not checked, after a game update). The same test emf::kill
// makes. Refills and god mode need them.
inline bool attributes_on() { return emf::detail::attributes_with(emf::api()); }

// Who typed the command: a player in the game's chat (their entity; admin as the core decided), else the console or
// the host.
struct Sender {
    bool from_chat = false;
    bool admin = true;
    uint32_t entity = 0;
};

// What a one-off fill did.
enum class Fill { Filled, Full, Dead, Unreadable };

class Glue {
public:
    std::function<uint64_t()> clock;  // milliseconds (the DLL: GetTickCount64)

    void load() { s_ = read_settings(); }
    void settings_changed() { dirty_ = true; }
    void set_switched_off(bool off) { off_ = off; }
    bool switched_off() const { return off_; }
    const Settings& settings() const { return s_; }
    std::vector<std::string> wheel_items() const { return blessings::wheel_items(s_.host); }

    void tick(emf::World w) {
        if (!w.is_server()) {
            client_tick();
            return;
        }
        last_server_ms_ = now();
        if (redraw_.exchange(false)) hud_force_ = true;
        begin(w);
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
        ++session_ticks_;
        if (roster_.by_account.empty() && !s_.host.any() && held_.empty()) {  // nobody blessed
            told_ = false;
            missing_health_ = missing_fall_ = 0;
            if (session_ticks_ % kScanTicks == 1)
                for (uint32_t p : emf::players(w)) first_look(w, p);
            show("");
            return;
        }
        const auto players = emf::players(w);
        std::map<uint32_t, uint64_t> account;
        std::set<uint64_t> present;
        for (uint32_t p : players) present.insert(account[p] = account_of(w, p));
        prune(roster_, present);
        const bool attrs = attributes_on();
        bool wants_attrs = false, host_here = false, health_failed = false, fall_failed = false;
        std::set<uint32_t> god;
        for (uint32_t p : players) {
            first_look(w, p);
            const bool host = is_host(p);
            host_here = host_here || host;
            const Blessings b = host ? s_.host : of(roster_, account[p]);
            if (!b.any()) continue;
            const bool health = b.health != Health::Off;
            wants_attrs = wants_attrs || health || b.stamina || b.mana;
            if (attrs && (health || b.stamina || b.mana)) fill(w, p, health, b.stamina, b.mana);
            if (b.health == Health::God && attrs) {
                if (hold_minimum(w, p)) god.insert(p);
                else health_failed = true;
            }
            if (b.nofall || b.health == Health::God)
                if (!lower_fall_start(w, p)) fall_failed = true;
        }
        for (auto it = held_.begin(); it != held_.end();) {
            if (god.count(it->first)) {
                ++it;
                continue;
            }
            restore(w, it->first, it->second);
            it = held_.erase(it);
        }
        missing_health_ = health_failed ? missing_health_ + 1 : 0;
        missing_fall_ = fall_failed ? missing_fall_ + 1 : 0;
        if (missing_health_ == kMissingTicks && !warned_health_) {
            warned_health_ = true;
            say_missing("a blessed player's Health is not laid out as EMF knows it", "god mode");
        }
        if (missing_fall_ == kMissingTicks && !warned_fall_) {
            warned_fall_ = true;
            say_missing("a blessed player has no DynamicFallDamage", "no fall damage");
        }
        if (wants_attrs && !attrs) {
            if (!told_) {
                const std::string text = std::string("Flame Blessings: god mode, endless health, stamina and mana are ") +
                                         kUnavailable + " (no fall damage still works)";
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
        if (from.from_chat && !from.admin) return "Flame Blessings is for the host and admins";
        if (!w.is_server()) return "Flame Blessings runs where the world runs (the host or a dedicated server)";
        begin(w);
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
            switch (fill(w, target, true, true, true)) {
                case Fill::Filled:
                    announce(w, sender, "heal", true, who);
                    return "Flame Blessings: " + who + " is filled up";
                case Fill::Full: return "Flame Blessings: " + who + " is already full";
                case Fill::Dead: return "Flame Blessings: " + who + " is dead: nothing to fill up";
                case Fill::Unreadable: break;
            }
            return "Flame Blessings: EMF could not read " + who + "'s health: nothing filled up";
        }
        const Blessings before = blessings_of(w, target);
        if (c.kind == Command::AllOff) {
            if (!store(w, target, Blessings{})) return kNotSaved;
            if (before.any()) announce(w, sender, "off", false, who);
            return "Flame Blessings: all blessings off for " + who;
        }
        if (c.on && needs_attributes(c.what) && !attributes_on())
            return "Flame Blessings: " + label(c.what) + " is " + kUnavailable;
        const Blessings after = apply(before, c.what, c.on);
        if (!store(w, target, after)) return kNotSaved;
        if (after != before) announce(w, sender, c.what, c.on, who);
        return "Flame Blessings: " + label(c.what) + (c.on ? " on" : " off") + " for " + who +
               " (now: " + (after.any() ? joined(short_names(after)) : std::string("none")) + ")";
    }

    // The wheel's answer (on the server world): choice 0-6 of wheel_items(), -1 = cancelled. Acts on the host.
    void wheel_pick(emf::World w, int choice) {
        if (off_ || !w.is_server()) return;
        begin(w);
        if (!host_ || choice < 0 || choice > kWheelAllOff) return;
        const std::string who = name_of(w, host_);
        const Blessings before = s_.host;
        if (choice == kWheelHeal) {
            if (!attributes_on()) {
                EMF_NOTIFY("Flame Blessings: Fill me up is %s", kUnavailable);
                return;
            }
            const Fill done = fill(w, host_, true, true, true);
            if (done == Fill::Filled) announce(w, "Host", "heal", true, who);
            else if (done == Fill::Dead) EMF_NOTIFY("%s", "Flame Blessings: you are dead: nothing to fill up");
            else if (done == Fill::Unreadable) EMF_NOTIFY("%s", "Flame Blessings: EMF could not read your health");
            return;
        }
        if (choice == kWheelAllOff) {
            if (!set_host(Blessings{})) EMF_NOTIFY("%s", kNotSaved);
            else if (before.any()) announce(w, "Host", "off", false, who);
            return;
        }
        const std::string what = kWheelWords[choice];
        const Blessings after = wheel_toggle(before, choice);
        const bool on = has(after, what);
        if (on && needs_attributes(what) && !attributes_on()) {
            EMF_NOTIFY("Flame Blessings: %s is %s", label(what).c_str(), kUnavailable);
            return;
        }
        if (!set_host(after)) EMF_NOTIFY("%s", kNotSaved);
        else if (after != before) announce(w, "Host", what, on, who);
    }

private:
    static constexpr const char* kNotSaved =
        "Flame Blessings: the host's setting could not be changed (the log says why); nothing changed";

    struct Held {             // a player's Health minimum from before god mode, by entity
        uint64_t account = 0;  // emf::player_account then (0: not known yet; filled in once known)
        int32_t before = 0;
    };

    static bool needs_attributes(const std::string& what) {
        return what == "god" || what == "refill" || what == "stamina" || what == "mana";
    }
    uint64_t now() const { return clock ? clock() : 0; }

    // The start of every server-world callback: a new session, the host, the settings.
    void begin(emf::World w) {
        new_session_check();
        host_ = find_host(w);
        refresh(w);
    }
    // Settings changed (the Mods tab, the console's set, or this mod's own set_setting): read them again. A change of
    // the host's blessings that did not come from this mod (its own changes are in s_ already) is told in chat.
    void refresh(emf::World w) {
        if (!dirty_.exchange(false)) return;
        const Blessings old = s_.host;
        s_ = read_settings();
        if (off_ || !host_) return;
        const Blessings& now = s_.host;
        const std::string who = name_of(w, host_);
        if (now.health != old.health) {
            if (now.health == Health::God) announce(w, "Host", "god", true, who);
            else if (now.health == Health::Refill) announce(w, "Host", "refill", true, who);
            else announce(w, "Host", old.health == Health::God ? "god" : "refill", false, who);
        }
        if (now.stamina != old.stamina) announce(w, "Host", "stamina", now.stamina, who);
        if (now.mana != old.mana) announce(w, "Host", "mana", now.mana, who);
        if (now.nofall != old.nofall) announce(w, "Host", "nofall", now.nofall, who);
    }
    // Another world, or a reload: entities and their Health are new (never saved), the admins' blessings end.
    void new_session_check() {
        const uint32_t session = emf::server_session();
        if (session == session_) return;
        session_ = session;
        session_ticks_ = 0;
        roster_.by_account.clear();
        held_.clear();
        looked_.clear();
        missing_health_ = missing_fall_ = 0;
        warned_health_ = warned_fall_ = false;
    }
    // The host: the local player; for a step where the game reports none, the player with the host's account as last
    // seen. A dedicated server never has one.
    uint32_t find_host(emf::World w) {
        if (const uint32_t local = emf::local_player()) {
            if (const uint64_t a = emf::player_account(w, local)) host_account_ = a;
            return local;
        }
        if (host_account_)
            for (uint32_t p : emf::players(w))
                if (emf::player_account(w, p) == host_account_) return p;
        return 0;
    }
    bool is_host(uint32_t p) const { return host_ && p == host_; }
    static uint64_t account_of(emf::World w, uint32_t p) {
        const uint64_t a = emf::player_account(w, p);
        return a ? a : 0x100000000ull + p;  // no account known (a test world): the entity stands in
    }
    Blessings blessings_of(emf::World w, uint32_t p) const { return is_host(p) ? s_.host : of(roster_, account_of(w, p)); }
    bool store(emf::World w, uint32_t p, const Blessings& b) {
        if (is_host(p)) return set_host(b);
        set(roster_, account_of(w, p), b);
        return true;
    }
    // The host's blessings are settings: changed the way the Mods tab changes them, so the tab shows them. A field the
    // core refuses to write keeps its old value; false then.
    bool set_host(const Blessings& b) {
        Blessings now = s_.host;
        bool ok = true;
        auto put = [&ok](const char* key, const char* value) {
            if (emf::set_setting(kMod, key, value)) return true;
            EMF_LOG(kMod, "the setting %s could not be set to %s", key, value);
            ok = false;
            return false;
        };
        if (b.health != now.health && put("health", health_value(b.health))) now.health = b.health;
        if (b.stamina != now.stamina && put("stamina", b.stamina ? "1" : "0")) now.stamina = b.stamina;
        if (b.mana != now.mana && put("mana", b.mana ? "1" : "0")) now.mana = b.mana;
        if (b.nofall != now.nofall && put("no_fall_damage", b.nofall ? "1" : "0")) now.nofall = b.nofall;
        s_.host = now;
        return ok;
    }
    std::string name_of(emf::World w, uint32_t p) const {
        if (const auto n = emf::player_name(w, p)) return *n;
        return is_host(p) ? "Host" : "player #" + std::to_string(p);
    }
    // A chat line for every player, when that is on and someone else is there to read it.
    void announce(emf::World w, const std::string& sender, const std::string& what, bool on, const std::string& who) {
        const std::string text = announce_text(sender, what, on, who);
        EMF_LOG(kMod, "%s", text.c_str());
        if (s_.announce && emf::players(w).size() > 1) emf::broadcast(text);
    }
    static Fill fill(emf::World w, uint32_t p, bool health, bool stamina, bool mana) {
        const auto hp = emf::attribute(w, p, EMF_ATTR_HEALTH);
        if (!hp) return Fill::Unreadable;
        if (*hp <= 0) return Fill::Dead;
        bool added = false;
        auto top = [&](uint32_t cur_id, uint32_t max_id) {
            const auto cur = emf::attribute(w, p, cur_id), max = emf::attribute(w, p, max_id);
            if (cur && max && *max > *cur && emf::attribute_add(w, p, cur_id, *max - *cur)) added = true;
        };
        if (health) top(EMF_ATTR_HEALTH, EMF_ATTR_MAX_HEALTH);
        if (stamina) top(EMF_ATTR_STAMINA, EMF_ATTR_MAX_STAMINA);
        if (mana) top(EMF_ATTR_MANA, EMF_ATTR_MAX_MANA);
        return added ? Fill::Filled : Fill::Full;
    }
    // The player's Health when it is laid out as EMF knows it: initialised, values from +8, slot 0 the current health
    // and slot 7 the maximum as the game's own attribute reads give them. Else null: nothing is written.
    static emf::gen::ecs::Health* known_health(emf::World w, uint32_t p) {
        auto* h = w.get<emf::gen::ecs::Health>(p);
        if (!h || !(h->flags & 1) || h->storageOffset != 8) return nullptr;
        const auto cur = emf::attribute(w, p, EMF_ATTR_HEALTH), max = emf::attribute(w, p, EMF_ATTR_MAX_HEALTH);
        if (!cur || !max || *cur != h->dataStorage[0] || *max != h->dataStorage[7]) return nullptr;
        return h;
    }
    // God mode for one step: the minimum up to the maximum, the earlier minimum kept (once per entity).
    bool hold_minimum(emf::World w, uint32_t p) {
        auto* h = known_health(w, p);
        if (!h) return false;
        const uint64_t account = emf::player_account(w, p);
        auto it = held_.find(p);
        if (it != held_.end() && it->second.account && account && it->second.account != account) {
            held_.erase(it);  // someone else took the entity in this step: their Health is new, theirs is kept now
            it = held_.end();
        }
        if (it == held_.end()) held_[p] = Held{account, h->dataStorage[1]};
        else if (!it->second.account) it->second.account = account;  // the account showed up: the same player
        if (h->dataStorage[1] != h->dataStorage[7]) h->dataStorage[1] = h->dataStorage[7];
        return true;
    }
    // Puts the minimum back, only on the same player still in the world (a known account that changed is someone
    // else).
    static void restore(emf::World w, uint32_t entity, const Held& held) {
        if (!w.get<emf::gen::ecs::PlayerState>(entity)) return;
        const uint64_t account = emf::player_account(w, entity);
        if (held.account && account && held.account != account) return;
        auto* h = w.get<emf::gen::ecs::Health>(entity);
        if (h && (h->flags & 1) && h->storageOffset == 8) h->dataStorage[1] = held.before;
    }
    void restore_all(emf::World w) {
        for (const auto& [entity, held] : held_) restore(w, entity, held);
        held_.clear();
    }
    // The first look at a player in this session: a minimum health above 0 there was left behind (Health is not
    // saved, so only a bug could leave one): back to 0, logged. Players' minimum is 0 in the game.
    void first_look(emf::World w, uint32_t p) {
        if (looked_.count(p)) return;
        auto* h = known_health(w, p);
        if (!h) return;  // not set up yet: next time
        looked_.insert(p);
        if (h->dataStorage[1] > 0 && !held_.count(p)) {
            EMF_LOG(kMod, "player %u's minimum health was %d without god mode (left behind?): set back to 0", p,
                    h->dataStorage[1]);
            h->dataStorage[1] = 0;
        }
    }
    // No fall damage for one step: the fall's start no higher than where the player is now.
    static bool lower_fall_start(emf::World w, uint32_t p) {
        auto* fall = w.get<emf::gen::ecs::DynamicFallDamage>(p);
        if (!fall) return false;
        if (const auto at = emf::position(w, p)) {
            const float y = static_cast<float>(at->y);
            if (y < fall->fallStartAltitude) fall->fallStartAltitude = y;
        }
        return true;
    }
    static void say_missing(const char* what, const char* blessing) {
        EMF_LOG(kMod, "%s: %s does nothing on this game build", what, blessing);
        EMF_NOTIFY("Flame Blessings: %s does not work on this game version", blessing);
    }
    // A client world's step: with no server step for a while (someone else's game), the host's line goes; the next
    // server step draws it again.
    void client_tick() {
        if (!hud_up_) return;
        if (now() - last_server_ms_ < kHudStaleMs) return;
        emf::hud(kHudId, "", EMF_HUD_BOTTOM_LEFT);
        hud_up_ = false;
        redraw_ = true;
    }
    void show(const std::string& text) {
        if (!hud_force_ && text == hud_text_ && (text.empty() || s_.corner == hud_corner_)) return;
        hud_force_ = false;
        emf::hud(kHudId, text, s_.corner);
        hud_text_ = text;
        hud_corner_ = s_.corner;
        hud_up_ = !text.empty();
    }
    uint32_t resolve(emf::World w, const std::string& target, const Sender& from, std::string* err) const {
        if (target.empty()) {
            const uint32_t p = from.from_chat ? from.entity : host_;
            if (!p)
                *err = from.from_chat ? "EMF could not tell which player you are: name one (#k or a name)"
                                      : "there is no \"you\" here: name a player (#k or a name)";
            return p;
        }
        std::vector<PlayerRef> list;
        for (uint32_t p : emf::players(w)) list.push_back({p, emf::player_name(w, p).value_or("")});
        return find_player(target, list, err);
    }
    // Every player, #k in order (lowest entity id first, as #k counts), with their blessings.
    std::string list(emf::World w) const {
        const auto players = emf::players(w);
        if (players.empty()) return "Flame Blessings: nobody is here";
        std::string out = "Flame Blessings:";
        for (size_t k = 0; k < players.size(); ++k) {
            const Blessings b = blessings_of(w, players[k]);
            out += "\n#" + std::to_string(k + 1) + " " + name_of(w, players[k]) + ": " +
                   (b.any() ? joined(short_names(b)) : std::string("none"));
        }
        return out;
    }

    Settings s_;
    Roster roster_;
    std::map<uint32_t, Held> held_;  // by entity
    std::set<uint32_t> looked_;      // players looked at in this session (the left-behind check)
    std::atomic<bool> dirty_{false}, off_{false};
    bool cleaned_ = false;  // switched off: minimums put back and the line removed
    bool told_ = false;     // "not available" said since something last needed the attributes
    int missing_health_ = 0, missing_fall_ = 0;        // steps in a row a blessing found no usable component
    bool warned_health_ = false, warned_fall_ = false;  // said (this world session)
    uint32_t session_ = 0;
    uint64_t session_ticks_ = 0;
    uint32_t host_ = 0;          // the host this callback (0: none)
    uint64_t host_account_ = 0;  // the host's account, once seen
    std::string hud_text_;
    uint32_t hud_corner_ = EMF_HUD_BOTTOM_LEFT;
    bool hud_force_ = false;  // draw again even when unchanged (a client step removed it)
    std::atomic<bool> hud_up_{false}, redraw_{false};
    std::atomic<uint64_t> last_server_ms_{0};
};

}  // namespace blessings
