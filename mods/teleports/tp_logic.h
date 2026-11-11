#pragma once
// Waypoint Teleports: the decisions, kept apart from the game so tests can run them over a fake (tests/test_teleports.cpp).
// The game side (positions, the shroud, spawn points, the item cost, EMF's teleport) is behind `Game`; tp_glue.h
// implements it over the EMF API.
//
// /unstuck: a countdown (standing still), then a hop a few metres up with the game's ground search; when that found no
//           walkable ground (the raw target) or left the player where they stood, the player's spawn point instead.
//           Not within combat_seconds of a hit on them, not while dead, a cooldown. Allowed in the shroud.
// /home:    a countdown, then the player's spawn point (the game lands them on its pads), else the nearest flame altar.
//           A cooldown, an optional item cost (taken when it happens, given back when the teleport fails), not in the
//           shroud unless home_in_shroud. Not while dead.
// Admins:   tp <#k|name> [<#m|name>], bring <#k|name>, tp.pos [#k] x y z [ground], tp.back [#k], tp.list. No countdown.
//           tp.back: where the player stood before their last teleport by this mod (per account, for the session).
// The map:  EMF's map action (Shift + hold on the map) and /tp.mark (the player's own waypoint): the ground under the
//           spot (EMF's ground_at), then a teleport just above it with the game's ground search. A place nobody is near
//           gets a hop high over it first (it loads around the player), the ground again, else back where they stood.
//           map_teleport: admins (default), everyone (/home's checks, its own cooldown, no cost) or off.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace tp {

struct Vec {
    double x = 0, y = 0, z = 0;
};
inline double dist(const Vec& a, const Vec& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

constexpr double kCancelMetres = 2.0;   // moving this far during the countdown cancels it
constexpr double kStuckMetres = 1.5;    // an /unstuck hop that ended this close to the start did not help
constexpr double kBesideMetres = 1.0;   // tp/bring: land this far beside the other player
constexpr uint64_t kSecond = 1000;

enum class Kind { Unstuck, Home, Tp, Bring, TpPos, TpBack, MapTp, Mark };
inline const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Unstuck: return "unstuck";
        case Kind::Home: return "home";
        case Kind::Tp: return "tp";
        case Kind::Bring: return "bring";
        case Kind::TpPos: return "tp.pos";
        case Kind::TpBack: return "tp.back";
        case Kind::MapTp: return "map teleport";
        case Kind::Mark: return "tp.mark";
    }
    return "?";
}

inline std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// "unstuck home, tp bring" -> the known command names in it (lower case); words that are no command are ignored.
inline std::set<std::string> parse_enabled(const std::string& text) {
    static const char* const kAll[] = {"unstuck", "home", "tp", "bring", "tp.pos", "tp.back", "tp.list", "tp.mark"};
    std::set<std::string> out;
    std::string word;
    auto flush = [&] {
        const std::string w = lower(word);
        for (const char* k : kAll)
            if (w == k) out.insert(w);
        word.clear();
    };
    for (char c : text) {
        if (c == ' ' || c == ',' || c == ';' || c == '\t') flush();
        else word += c;
    }
    flush();
    return out;
}
inline const char* kDefaultEnabled = "unstuck home tp bring tp.pos tp.back tp.list tp.mark";

// Who may teleport by the map (and /tp.mark): the setting map_teleport.
enum class MapWho { Admins, Everyone, Off };
inline MapWho parse_map_who(const std::string& s) {
    const std::string v = lower(s);
    if (v == "everyone") return MapWho::Everyone;
    if (v == "off") return MapWho::Off;
    return MapWho::Admins;
}

struct Settings {
    bool unstuck = true;
    double unstuck_height = 4.0;  // m above the player where the ground search starts
    int unstuck_cooldown_s = 60;
    int combat_s = 10;
    bool home = true;
    int home_combat_s = 10;  // /home is no escape from a fight either
    int home_cooldown_min = 10;
    std::string home_cost_item;  // "" = free
    int home_cost_count = 1;
    bool home_in_shroud = false;
    int countdown_s = 3;
    MapWho map_teleport = MapWho::Admins;
    int map_cooldown_s = 60;  // everyone: between map teleports (and /tp.mark) of one player
    std::set<std::string> enabled = parse_enabled(kDefaultEnabled);
    bool on(const std::string& cmd) const { return enabled.count(cmd) != 0; }
};

// "2 min 5 s", "45 s", "1 s"
inline std::string duration(uint64_t ms) {
    const uint64_t s = (ms + 999) / 1000;
    if (s < 60) return std::to_string(s) + " s";
    const uint64_t m = s / 60, r = s % 60;
    return std::to_string(m) + " min" + (r ? " " + std::to_string(r) + " s" : "");
}

inline Vec unstuck_target(const Vec& at, double height) { return {at.x, at.y + height, at.z}; }
// The hop did not help: the ground search found nothing (the player hangs at the raw target and falls back), or it put
// the player back where they stood (the same hole).
inline bool unstuck_needs_fallback(bool raw_target, const Vec& start, const Vec& final_pos) {
    return raw_target || dist(start, final_pos) < kStuckMetres;
}

// ---- admin arguments ----
struct Who {
    enum Type { None, Index, Name } type = None;
    int index = 0;     // #k: the k-th player, lowest entity id first
    std::string name;  // a player's name (needs EMF's player_name)
};
// One player argument: "#2" (or "2") is the 2nd player; anything else is a name.
inline Who parse_who(const std::string& a) {
    Who w;
    if (a.empty()) return w;
    const std::string digits = a[0] == '#' ? a.substr(1) : a;
    if (!digits.empty() && digits.size() <= 4 && std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        w.type = Who::Index;
        w.index = std::atoi(digits.c_str());
        if (w.index < 1) w.type = Who::None;
        return w;
    }
    w.type = Who::Name;
    w.name = a;
    return w;
}
inline bool parse_number(const std::string& s, double* out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (!end || *end || !std::isfinite(v)) return false;
    *out = v;
    return true;
}
// tp.pos [#k] x y z [ground]
struct PosArgs {
    bool ok = false;
    Who who;
    Vec at;
    bool ground = false;
};
inline PosArgs parse_pos_args(std::vector<std::string> a) {
    PosArgs p;
    if (!a.empty() && lower(a.back()) == "ground") {
        p.ground = true;
        a.pop_back();
    }
    if (a.size() == 4) {
        p.who = parse_who(a[0]);
        if (p.who.type == Who::None) return p;
        a.erase(a.begin());
    }
    if (a.size() != 3) return p;
    double v[3];
    for (int i = 0; i < 3; ++i)
        if (!parse_number(a[static_cast<size_t>(i)], &v[i])) return p;
    p.at = {v[0], v[1], v[2]};
    p.ok = true;
    return p;
}

// ---- small state ----
class Cooldowns {
public:
    // Time left (ms) before `account` may use `kind` again; 0 = ready.
    uint64_t left(uint64_t account, Kind kind, uint64_t now_ms, uint64_t cooldown_ms) const {
        auto it = last_.find({account, kind});
        if (it == last_.end() || now_ms >= it->second + cooldown_ms) return 0;
        return it->second + cooldown_ms - now_ms;
    }
    void start(uint64_t account, Kind kind, uint64_t now_ms) { last_[{account, kind}] = now_ms; }
    void clear(uint64_t account, Kind kind) { last_.erase({account, kind}); }
    void clear() { last_.clear(); }

private:
    std::map<std::pair<uint64_t, Kind>, uint64_t> last_;
};

class Combat {
public:
    void hit(uint32_t player, uint64_t now_ms) { last_[player] = now_ms; }
    // Time left (ms) of the combat window after the last hit on `player`; 0 = out of combat.
    uint64_t left(uint32_t player, uint64_t now_ms, uint64_t window_ms) const {
        auto it = last_.find(player);
        if (it == last_.end() || now_ms >= it->second + window_ms) return 0;
        return it->second + window_ms - now_ms;
    }
    void clear() { last_.clear(); }

private:
    std::map<uint32_t, uint64_t> last_;
};

// ---- the game, as the mod sees it ----
struct TeleportAsk {
    Vec to;
    bool ground = false;
    uint32_t spawn_point = 0;  // land on this spawn point's pads (`to` is not used)
};
struct Outcome {
    bool moved = false;
    Vec pos;
    bool raw_target = false;
    std::string reason;
    bool session_ended = false;  // the session ended first: touch nothing, give nothing back
};
// EMF's ground_at: the ground under a spot, or nobody is near it (hop_y: a height over it where a player may wait while
// it loads), or no ground.
enum class GroundStatus { Ok, NotLoaded, Failed };
struct Ground {
    GroundStatus status = GroundStatus::Failed;
    double y = 0, hop_y = 0;
    std::string why;
};
constexpr double kAboveGround = 0.5;
constexpr uint64_t kDeadlyWatchMs = 3000;  // after a map teleport: landed in the deadly shroud -> back  // the teleport lands this far above the ground found (the game's search settles it)

struct Game {
    virtual ~Game() = default;
    virtual uint64_t now_ms() = 0;
    virtual uint32_t session() = 0;  // the hosted session number (a reload or another world changes it)
    virtual std::vector<uint32_t> players() = 0;              // lowest entity id first
    virtual std::vector<uint32_t> players_named(const std::string&) = 0;  // case-insensitive; empty: none
    virtual std::string name_of(uint32_t player) = 0;                   // "" when not known
    virtual bool names_available() = 0;
    virtual std::optional<Vec> position(uint32_t player) = 0;
    virtual uint64_t account(uint32_t player) = 0;
    virtual bool dead(uint32_t player) = 0;
    virtual bool in_shroud(uint32_t player) = 0;
    virtual bool in_deadly_shroud(uint32_t player) = 0;  // fog Death, not in a spawn point's safe zone
    virtual uint32_t spawn_point(uint32_t player) = 0;  // the player's own, else the nearest flame altar; 0 = none
    // EMF's teleport: false when refused at once; else done() runs once, later, on the server thread.
    virtual bool teleport(uint32_t player, const TeleportAsk& ask, std::function<void(const Outcome&)> done) = 0;
    // Takes the /home cost from the player (all or nothing); done(true) when taken. False when it could not be asked.
    virtual bool take_cost(uint32_t player, const std::string& item, int count, std::function<void(bool)> done) = 0;
    virtual void give_back(uint32_t player, const std::string& item, int count) = 0;
    // The ground under (x, z): false when it cannot be asked; else done() once, later, on the server thread.
    virtual bool ground_at(double x, double z, std::function<void(const Ground&)> done) = 0;
    // The player's map waypoint (set and enabled), else nothing.
    virtual std::optional<Vec> waypoint(uint32_t player) = 0;
    // A line for a player after their command returned (the host: on screen; a guest: as EMF can reach them).
    virtual void tell(uint32_t player, const std::string& text) = 0;
    virtual void log(const std::string& text) = 0;
};

// ---- the mod ----
class Teleports {
public:
    explicit Teleports(Game& g) : g_(g) {}
    void set(const Settings& s) { s_ = s; }
    const Settings& settings() const { return s_; }

    // A new world or session: countdowns, cooldowns, combat and tp.back positions are forgotten.
    void new_session() {
        pending_.clear();
        cooldowns_.clear();
        combat_.clear();
        back_.clear();
        moving_.clear();
        landed_.clear();
        ++epoch_;  // answers for the old session's map teleports are dropped
    }
    void on_hit(uint32_t target) {
        if (target >= 1 && target <= 1023) combat_.hit(target, g_.now_ms());
    }

    // /unstuck and /home for `player` (the sender). The reply for the command.
    std::string unstuck(uint32_t player) { return ask(player, Kind::Unstuck); }
    std::string home(uint32_t player) { return ask(player, Kind::Home); }

    // Admin commands. `sender`: the admin's player entity (0 = the console of a dedicated server).
    std::string tp(uint32_t sender, const std::vector<std::string>& args) {
        if (!s_.on("tp")) return "tp is switched off here";
        if (args.empty() || args.size() > 2) return "usage: tp <#k|name> (you to that player) or tp <#k|name> <#m|name>";
        uint32_t who = sender, to = 0;
        std::string err;
        if (args.size() == 2) {
            who = resolve(args[0], &err);
            if (!who) return err;
        } else if (!sender) {
            return "tp: from the console name both players: tp <#k> <#m>";
        }
        to = resolve(args.back(), &err);
        if (!to) return err;
        if (to == who) return "tp: that is the same player";
        const auto at = g_.position(to);
        if (!at) return "tp: player " + std::to_string(to) + " has no position";
        return admin_move(sender, who, {{at->x + kBesideMetres, at->y, at->z}, true, 0}, Kind::Tp);
    }
    std::string bring(uint32_t sender, const std::vector<std::string>& args) {
        if (!s_.on("bring")) return "bring is switched off here";
        if (args.size() != 1) return "usage: bring <#k|name>";
        if (!sender) return "bring: there is no \"you\" on the console: tp <#k> <#m> moves player k to player m";
        std::string err;
        const uint32_t who = resolve(args[0], &err);
        if (!who) return err;
        if (who == sender) return "bring: that is you";
        const auto at = g_.position(sender);
        if (!at) return "bring: you have no position";
        return admin_move(sender, who, {{at->x + kBesideMetres, at->y, at->z}, true, 0}, Kind::Bring);
    }
    std::string tp_pos(uint32_t sender, const std::vector<std::string>& args) {
        if (!s_.on("tp.pos")) return "tp.pos is switched off here";
        const PosArgs p = parse_pos_args(args);
        if (!p.ok) return "usage: tp.pos [#k|name] <x> <y> <z> [ground]";
        uint32_t who = sender;
        std::string err;
        if (p.who.type != Who::None) {
            who = resolve_who(p.who, &err);
            if (!who) return err;
        }
        if (!who) return "tp.pos: from the console name the player: tp.pos #k x y z";
        return admin_move(sender, who, {p.at, p.ground, 0}, Kind::TpPos);
    }
    std::string tp_back(uint32_t sender, const std::vector<std::string>& args) {
        if (!s_.on("tp.back")) return "tp.back is switched off here";
        if (args.size() > 1) return "usage: tp.back [#k|name]";
        uint32_t who = sender;
        std::string err;
        if (args.size() == 1) {
            who = resolve(args[0], &err);
            if (!who) return err;
        }
        if (!who) return "tp.back: from the console name the player: tp.back #k";
        const uint64_t acct = g_.account(who);
        auto it = back_.find(acct);
        if (!acct || it == back_.end()) return "tp.back: no earlier position for player " + std::to_string(who) + " this session";
        return admin_move(sender, who, {it->second, false, 0}, Kind::TpBack);
    }
    std::string tp_list() const {
        std::string out;
        const auto list = g_.players();
        const bool names = g_.names_available();
        for (size_t i = 0; i < list.size(); ++i) {
            const auto p = g_.position(list[i]);
            const std::string name = names ? g_.name_of(list[i]) : std::string();
            char b[200];
            std::snprintf(b, sizeof b, "#%zu = %s%splayer %u%s at (%.0f, %.0f, %.0f)", i + 1, name.c_str(),
                          name.empty() ? "" : " (", list[i], name.empty() ? "" : ")", p ? p->x : 0.0, p ? p->y : 0.0,
                          p ? p->z : 0.0);
            out += (out.empty() ? "" : "\n") + std::string(b);
        }
        return out.empty() ? "no players" : out;
    }

    // A player finished EMF's map gesture at (x, z); `admin`: the server's own view (the host's game or a [chat] admin).
    // What happens is told to the player.
    void map_teleport(uint32_t player, bool admin, double x, double z) {
        const std::string no = map_refusal(player, admin, Kind::MapTp);
        if (!no.empty()) {
            g_.tell(player, no);
            return;
        }
        spot_start(Spot{player, player, admin, Kind::MapTp, x, z});
    }

    // /tp.mark: the sender to their own waypoint, with the map teleport's rules; admins: tp.mark #k moves player k to
    // k's waypoint. The reply for the command.
    std::string tp_mark(uint32_t sender, bool sender_admin, const std::vector<std::string>& args) {
        if (!s_.on("tp.mark")) return "tp.mark is switched off here";
        if (args.size() > 1) return "usage: tp.mark (you to your waypoint) or tp.mark #k (admins)";
        uint32_t who = sender;
        if (args.size() == 1) {
            if (!sender_admin) return "tp.mark #k is for admins";
            std::string err;
            who = resolve(args[0], &err);
            if (!who) return err;
        } else if (!sender) {
            return "tp.mark: from the console name the player: tp.mark #k";
        }
        const bool self = who == sender;
        const std::string no = map_refusal(who, sender_admin, Kind::Mark, self);
        if (!no.empty()) return no;
        const auto w = g_.waypoint(who);
        if (!w) return self ? "Set a waypoint on your map first" : "tp.mark: player " + std::to_string(who) + " has no waypoint";
        if (!spot_start(Spot{who, sender, sender_admin, Kind::Mark, w->x, w->z})) return "";  // told already
        return self ? "To your waypoint: finding the ground there"
                    : "tp.mark: player " + std::to_string(who) + " to their waypoint: finding the ground there";
    }
    bool moving(uint32_t player) const { return moving_.count(player) != 0; }

    // Every server tick: the countdowns, and a map teleport's first seconds (the deadly shroud).
    void tick() {
        const uint64_t now = g_.now_ms();
        watch_landed(now);
        std::vector<Pending> go;
        for (auto it = pending_.begin(); it != pending_.end();) {
            Pending& p = it->second;
            const auto at = g_.position(p.player);
            if (!at) {
                it = pending_.erase(it);  // left the game
                continue;
            }
            if (dist(*at, p.start) > kCancelMetres) {
                g_.tell(p.player, std::string("/") + kind_name(p.kind) + " cancelled: you moved");
                it = pending_.erase(it);
                continue;
            }
            if (now >= p.due_ms) {
                go.push_back(p);
                it = pending_.erase(it);
                continue;
            }
            const int left = static_cast<int>((p.due_ms - now + 999) / 1000);
            if (left < p.said) {
                p.said = left;
                g_.tell(p.player, std::string(p.kind == Kind::Home ? "Home" : "Unstuck") + " in " + std::to_string(left) + "...");
            }
            ++it;
        }
        for (const auto& p : go) start(p);
    }
    bool pending(uint32_t player) const { return pending_.count(player) != 0; }
    size_t pending_count() const { return pending_.size(); }

private:
    struct Pending {
        uint32_t player = 0;
        Kind kind = Kind::Unstuck;
        Vec start;
        uint64_t due_ms = 0;
        int said = 0;  // the last whole second announced
    };

    uint32_t resolve_who(const Who& w, std::string* err) {
        const auto list = g_.players();
        if (w.type == Who::Index) {
            if (w.index >= 1 && static_cast<size_t>(w.index) <= list.size()) return list[static_cast<size_t>(w.index) - 1];
            *err = "there is no player #" + std::to_string(w.index) + " (tp.list shows them)";
            return 0;
        }
        if (w.type == Who::Name) {
            if (!g_.names_available()) {
                *err = "player names need a newer EMF (API v19): use #k (tp.list shows them)";
                return 0;
            }
            const auto ids = g_.players_named(w.name);
            if (ids.size() == 1) return ids[0];
            *err = ids.empty() ? "no player named " + w.name + " (tp.list shows them)"
                               : std::to_string(ids.size()) + " players are named " + w.name + ": use #k (tp.list shows them)";
            return 0;
        }
        *err = "name a player: #k or a name (tp.list shows them)";
        return 0;
    }
    uint32_t resolve(const std::string& a, std::string* err) { return resolve_who(parse_who(a), err); }

    std::string admin_move(uint32_t sender, uint32_t who, const TeleportAsk& ask, Kind kind) {
        const auto before = g_.position(who);
        if (!before) return std::string(kind_name(kind)) + ": player " + std::to_string(who) + " has no position";
        if (g_.dead(who)) return std::string(kind_name(kind)) + ": player " + std::to_string(who) + " is dead";
        const uint64_t acct = g_.account(who);
        const Vec from = *before;
        const bool ok = g_.teleport(who, ask, [this, sender, who, kind, acct, from](const Outcome& o) {
            if (o.session_ended) return;  // the world is gone: nothing to tell, nothing to remember
            if (!o.moved) {
                if (sender) g_.tell(sender, std::string(kind_name(kind)) + ": not moved: " + o.reason);
                g_.log(std::string(kind_name(kind)) + ": player " + std::to_string(who) + " not moved: " + o.reason);
                return;
            }
            if (acct) back_[acct] = from;
            char b[160];
            std::snprintf(b, sizeof b, "%s: player %u is at (%.1f, %.1f, %.1f)", kind_name(kind), who, o.pos.x, o.pos.y, o.pos.z);
            g_.log(b);
            if (sender) g_.tell(sender, b);
        });
        if (!ok) return std::string(kind_name(kind)) + ": refused (see the log)";
        char b[160];
        std::snprintf(b, sizeof b, "%s: moving player %u to (%.1f, %.1f, %.1f)", kind_name(kind), who, ask.to.x, ask.to.y,
                      ask.to.z);
        return b;
    }

    uint64_t combat_left(uint32_t player, Kind kind) const {
        const int window = kind == Kind::Unstuck ? s_.combat_s : s_.home_combat_s;
        return combat_.left(player, g_.now_ms(), static_cast<uint64_t>(std::max(0, window)) * kSecond);
    }

    // The checks of /unstuck and /home, then the countdown (or at once with countdown 0).
    std::string ask(uint32_t player, Kind kind) {
        const char* name = kind_name(kind);
        const bool on = kind == Kind::Unstuck ? s_.unstuck : s_.home;
        if (!on || !s_.on(name)) return std::string("/") + name + " is switched off here";
        if (!player) return std::string("/") + name + " is for players (type it in the game's chat)";
        if (!g_.position(player)) return std::string("/") + name + ": you have no position yet";
        if (g_.dead(player)) return "You are dead: respawn first";
        const uint64_t now = g_.now_ms();
        if (const uint64_t left = combat_left(player, kind))
            return std::string("You were hit just now: /") + name + " works " + duration(left) + " after the last hit";
        if (kind == Kind::Home && !s_.home_in_shroud && g_.in_shroud(player))
            return "Not inside the shroud: leave it first (as with fast travel)";
        const uint64_t acct = g_.account(player);
        if (const uint64_t left = cooldowns_.left(acct, kind, now, cooldown_ms(kind)))
            return std::string("/") + name + " again in " + duration(left);
        if (kind == Kind::Home && !g_.spawn_point(player)) return "No spawn point known: rest at a flame altar first";
        if (pending_.count(player)) return "You are already about to teleport: stand still";
        Pending p;
        p.player = player;
        p.kind = kind;
        p.start = *g_.position(player);
        p.due_ms = now + static_cast<uint64_t>(std::max(0, s_.countdown_s)) * kSecond;
        p.said = s_.countdown_s;
        if (s_.countdown_s <= 0) {
            start(p);
            return std::string(kind == Kind::Home ? "Teleporting home" : "Unsticking you");
        }
        pending_[player] = p;
        std::string cost;
        if (kind == Kind::Home && !s_.home_cost_item.empty() && s_.home_cost_count > 0)
            cost = " It costs " + std::to_string(s_.home_cost_count) + " x " + s_.home_cost_item + ".";
        return std::string(kind == Kind::Home ? "Home" : "Unstuck") + " in " + std::to_string(s_.countdown_s) +
               " s: stand still (moving 2 m cancels it)." + cost;
    }

    uint64_t cooldown_ms(Kind kind) const {
        return kind == Kind::Unstuck ? static_cast<uint64_t>(s_.unstuck_cooldown_s) * kSecond
                                     : static_cast<uint64_t>(s_.home_cooldown_min) * 60 * kSecond;
    }

    // The countdown ended: the checks that may have changed, then the teleport.
    void start(const Pending& p) {
        const uint32_t pl = p.player;
        if (g_.dead(pl)) {
            g_.tell(pl, "You are dead: no teleport");
            return;
        }
        if (combat_left(pl, p.kind)) {
            g_.tell(pl, std::string("You were hit: no /") + kind_name(p.kind));
            return;
        }
        if (p.kind == Kind::Unstuck) go_unstuck(pl);
        else home_now(pl, false);
    }

    // Whether /unstuck may fall back to a /home now: /home's switch, the shroud rule, its combat window and cooldown.
    // *why: what to add to "/unstuck could not help here" when not ("" when /home is switched off).
    bool home_allowed(uint32_t pl, std::string* why) {
        why->clear();
        if (!s_.home || !s_.on("home")) return false;
        if (!s_.home_in_shroud && g_.in_shroud(pl)) {
            *why = "; /home does not work inside the shroud";
            return false;
        }
        if (const uint64_t left = combat_left(pl, Kind::Home)) {
            *why = "; /home works " + duration(left) + " after the last hit";
            return false;
        }
        if (const uint64_t left = cooldowns_.left(g_.account(pl), Kind::Home, g_.now_ms(), cooldown_ms(Kind::Home))) {
            *why = "; /home again in " + duration(left);
            return false;
        }
        return true;
    }

    void go_unstuck(uint32_t pl) {
        const auto at = g_.position(pl);
        if (!at) return;
        const Vec from = *at;
        const uint64_t acct = g_.account(pl);
        const bool ok = g_.teleport(pl, {unstuck_target(from, s_.unstuck_height), true, 0}, [this, pl, from, acct](const Outcome& o) {
            if (o.session_ended) return;
            if (!o.moved) {
                g_.tell(pl, "/unstuck did not work: " + o.reason);
                return;
            }
            if (acct) back_[acct] = from;
            if (!unstuck_needs_fallback(o.raw_target, from, o.pos)) {
                cooldowns_.start(acct, Kind::Unstuck, g_.now_ms());
                g_.tell(pl, "Unstuck");
                return;
            }
            // The hop did not help: the way out is the spawn point, and that is a /home, with all of /home's limits.
            std::string why;
            if (!home_allowed(pl, &why)) {
                g_.tell(pl, "/unstuck could not help here (no free ground nearby)" + why);
                return;
            }
            home_now(pl, true);
        });
        if (!ok) g_.tell(pl, "/unstuck was refused (see the log)");
    }

    struct Paid {
        std::string item;  // exactly what was taken: a refund never reads the settings again
        int count = 0;
    };

    // A /home's teleport (also /unstuck's way out): the shroud rule, the spawn point, the cost, then its pads.
    void home_now(uint32_t pl, bool from_unstuck) {
        if (!s_.home_in_shroud && g_.in_shroud(pl)) {
            g_.tell(pl, "Not inside the shroud: no teleport");
            return;
        }
        const uint32_t sp = g_.spawn_point(pl);
        if (!sp) {
            g_.tell(pl, from_unstuck ? "No free ground found nearby, and no spawn point known"
                                     : "No spawn point known: rest at a flame altar first");
            return;
        }
        if (s_.home_cost_item.empty() || s_.home_cost_count <= 0) {
            go_home(pl, sp, std::nullopt, from_unstuck);
            return;
        }
        const Paid paid{s_.home_cost_item, s_.home_cost_count};
        const uint32_t session = g_.session();
        const bool asked = g_.take_cost(pl, paid.item, paid.count, [this, pl, sp, paid, session, from_unstuck](bool taken) {
            if (g_.session() != session) {  // answered in another session: nothing more (and nothing to give back)
                g_.log("/home of player " + std::to_string(pl) + ": the session ended while its cost was taken");
                return;
            }
            if (!taken) {
                g_.tell(pl, "/home costs " + std::to_string(paid.count) + " x " + paid.item + ": you do not carry that many");
                return;
            }
            go_home(pl, sp, paid, from_unstuck);
        });
        if (!asked) g_.tell(pl, "/home: the cost could not be taken (see the log)");
    }

    void go_home(uint32_t pl, uint32_t sp, std::optional<Paid> paid, bool from_unstuck) {
        const auto at = g_.position(pl);
        const Vec from = at ? *at : Vec{};
        const bool known = at.has_value();
        const uint64_t acct = g_.account(pl);
        const bool ok = g_.teleport(pl, {{}, false, sp}, [this, pl, acct, paid, from, known, from_unstuck](const Outcome& o) {
            // The session ended first: the take may not have been saved, so a refund could double the items. Nothing.
            if (o.session_ended) return;
            if (!o.moved) {
                if (paid) g_.give_back(pl, paid->item, paid->count);
                g_.tell(pl, std::string(from_unstuck ? "/unstuck" : "/home") + " did not work: " + o.reason +
                                (paid ? " (the cost was given back)" : ""));
                return;
            }
            if (acct && known) back_[acct] = from;
            cooldowns_.start(acct, Kind::Home, g_.now_ms());
            if (from_unstuck) cooldowns_.start(acct, Kind::Unstuck, g_.now_ms());
            g_.tell(pl, from_unstuck ? "No free ground nearby: you are at your spawn point" : "Home");
        });
        if (!ok) {
            if (paid) g_.give_back(pl, paid->item, paid->count);
            g_.tell(pl, std::string(from_unstuck ? "/unstuck" : "/home") + " was refused (see the log)");
        }
    }

    // ---- the map teleport ----
    struct Spot {
        uint32_t who = 0, sender = 0;  // the player moved; who asked (0: the console)
        bool admin = false;            // the asker is an admin: no player checks but position and death, no cooldown
        Kind kind = Kind::MapTp;
        double x = 0, z = 0;
        Vec from;                      // where `who` stood (the way back, and tp.back)
        uint64_t acct = 0;
        uint64_t epoch = 0;
        bool hopped = false;
    };

    // "" when `who` may go; else the line why not. `self`: the asker is the player moved.
    std::string map_refusal(uint32_t who, bool admin, Kind kind, bool self = true) {
        if (s_.map_teleport == MapWho::Off) return "Map teleport is switched off here";
        if (s_.map_teleport == MapWho::Admins && !admin) return "Map teleport is for admins here";
        const std::string label = self ? "You" : "Player " + std::to_string(who);
        if (!g_.position(who)) return self ? "Map teleport: you have no position yet" : label + " has no position";
        if (g_.dead(who)) return self ? "You are dead: respawn first" : label + " is dead";
        if (moving_.count(who)) return self ? "You are already on your way" : label + " is already on the way";
        if (admin) return "";
        if (const uint64_t left = combat_left(who, Kind::Home))
            return "You were hit just now: the map teleport works " + duration(left) + " after the last hit";
        if (!s_.home_in_shroud && g_.in_shroud(who)) return "Not inside the shroud: leave it first (as with fast travel)";
        if (const uint64_t left = cooldowns_.left(g_.account(who), Kind::MapTp, g_.now_ms(),
                                                  static_cast<uint64_t>(std::max(0, s_.map_cooldown_s)) * kSecond))
            return "Map teleport again in " + duration(left);
        (void)kind;
        return "";
    }

    void tell_both(const Spot& s, const std::string& to_player, const std::string& to_sender) {
        g_.tell(s.who, to_player);
        if (s.sender && s.sender != s.who) g_.tell(s.sender, to_sender);
    }

    // The checks passed: the ground under the spot. False (told) when it could not be asked.
    bool spot_start(Spot s) {
        s.from = g_.position(s.who).value_or(Vec{});
        s.acct = g_.account(s.who);
        s.epoch = epoch_;
        moving_.insert(s.who);
        if (spot_ground(s)) return true;
        moving_.erase(s.who);
        return false;
    }

    bool spot_ground(const Spot& s) {
        const bool asked = g_.ground_at(s.x, s.z, [this, s](const Ground& gr) { on_ground(s, gr); });
        if (!asked) {
            g_.log(std::string(kind_name(s.kind)) + ": ground_at was refused (EMF API v34 needed, or the spot is outside "
                                                     "the world)");
            tell_both(s, "Map teleport: the ground cannot be read here (see the log)",
                      std::string(kind_name(s.kind)) + ": the ground cannot be read here (see the log)");
        }
        return asked;
    }

    void on_ground(Spot s, const Ground& gr) {
        if (s.epoch != epoch_) return;  // another session: the player and the world mean nothing now
        if (gr.status == GroundStatus::Ok) {
            land(s, gr.y);
            return;
        }
        if (s.hopped) {  // the place loaded around the player and still has no ground: back
            go_back(s);
            return;
        }
        if (gr.status == GroundStatus::NotLoaded) {
            hop(s, gr.hop_y);
            return;
        }
        moving_.erase(s.who);
        tell_both(s, "Map teleport: no ground there (" + gr.why + ")",
                  std::string(kind_name(s.kind)) + ": no ground there (" + gr.why + ")");
    }

    void hop(Spot s, double y) {
        // with the ground search: the game streams the place first, the player waits where they stand; with nothing
        // within 10 m it uses the raw height
        const bool ok = g_.teleport(s.who, {{s.x, y, s.z}, true, 0}, [this, s](const Outcome& o) mutable {
            if (s.epoch != epoch_ || o.session_ended) return;
            if (!o.moved) {
                moving_.erase(s.who);
                tell_both(s, "Map teleport did not work: " + o.reason, std::string(kind_name(s.kind)) + ": not moved: " + o.reason);
                return;
            }
            s.hopped = true;
            if (!spot_ground(s)) go_back(s);
        });
        if (!ok) refused(s);
    }

    void land(const Spot& s, double ground_y) {
        const bool ok = g_.teleport(s.who, {{s.x, ground_y + kAboveGround, s.z}, true, 0}, [this, s](const Outcome& o) {
            if (s.epoch != epoch_ || o.session_ended) return;
            if (!o.moved) {
                if (s.hopped) {
                    go_back(s);
                    return;
                }
                moving_.erase(s.who);
                tell_both(s, "Map teleport did not work: " + o.reason, std::string(kind_name(s.kind)) + ": not moved: " + o.reason);
                return;
            }
            moving_.erase(s.who);
            if (s.acct) back_[s.acct] = s.from;
            if (!s.admin) cooldowns_.start(s.acct, Kind::MapTp, g_.now_ms());
            landed_[s.who] = Landed{s, g_.now_ms() + kDeadlyWatchMs};
            char me[96], them[128];
            std::snprintf(me, sizeof me, "You are at (%.0f, %.0f, %.0f)", o.pos.x, o.pos.y, o.pos.z);
            std::snprintf(them, sizeof them, "%s: player %u is at (%.0f, %.0f, %.0f)", kind_name(s.kind), s.who, o.pos.x,
                          o.pos.y, o.pos.z);
            g_.log(them);
            tell_both(s, me, them);
        });
        if (!ok) {
            if (s.hopped) go_back(s);
            else refused(s);
        }
    }

    void go_back(const Spot& s) {
        const bool ok = g_.teleport(s.who, {s.from, false, 0}, [this, s](const Outcome& o) {
            if (s.epoch != epoch_ || o.session_ended) return;
            moving_.erase(s.who);
            if (!o.moved) {
                tell_both(s, "No ground found there, and the way back failed: " + o.reason,
                          std::string(kind_name(s.kind)) + ": no ground there; player " + std::to_string(s.who) +
                              " could not be moved back: " + o.reason);
                return;
            }
            tell_both(s, "No ground found there: you are back where you were",
                      std::string(kind_name(s.kind)) + ": no ground there; player " + std::to_string(s.who) + " is back");
        });
        if (!ok) refused(s);
    }

    // A map teleport that ended in the deadly shroud (the map shows it, the server cannot see it before the player is
    // there): straight back, and no cooldown spent.
    void watch_landed(uint64_t now) {
        std::vector<Spot> back;
        for (auto it = landed_.begin(); it != landed_.end();) {
            if (now >= it->second.until || !g_.position(it->first)) {
                it = landed_.erase(it);
                continue;
            }
            if (g_.in_deadly_shroud(it->first) && !g_.dead(it->first)) {
                back.push_back(it->second.spot);
                it = landed_.erase(it);
                continue;
            }
            ++it;
        }
        for (const Spot& s : back) {
            if (!s.admin) cooldowns_.clear(s.acct, Kind::MapTp);
            moving_.insert(s.who);
            const bool ok = g_.teleport(s.who, {s.from, false, 0}, [this, s](const Outcome& o) {
                if (s.epoch != epoch_ || o.session_ended) return;
                moving_.erase(s.who);
                tell_both(s, o.moved ? "That place is in the deadly shroud: you are back where you were"
                                     : "That place is in the deadly shroud, and the way back failed: " + o.reason,
                          std::string(kind_name(s.kind)) + ": player " + std::to_string(s.who) +
                              (o.moved ? " landed in the deadly shroud and is back" : " is in the deadly shroud: " + o.reason));
            });
            if (!ok) refused(s);
        }
    }

    void refused(const Spot& s) {
        moving_.erase(s.who);
        tell_both(s, "Map teleport was refused (see the log)", std::string(kind_name(s.kind)) + ": refused (see the log)");
    }

    Game& g_;
    Settings s_;
    std::set<uint32_t> moving_;  // players on a map teleport (one at a time)
    struct Landed {
        Spot spot;
        uint64_t until = 0;
    };
    std::map<uint32_t, Landed> landed_;  // just landed by a map teleport: watched for the deadly shroud
    uint64_t epoch_ = 0;         // new_session() moves it on
    std::map<uint32_t, Pending> pending_;
    Cooldowns cooldowns_;
    Combat combat_;
    std::map<uint64_t, Vec> back_;  // account -> where they stood before their last teleport by this mod
};

}  // namespace tp
