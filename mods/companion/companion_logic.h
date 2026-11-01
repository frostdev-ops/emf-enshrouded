#pragma once
// Companion state shared between the server tick (spawn, stop, destroy, checks) and the companion's ECS
// program (sends StartGuardingEvent / StopGuardingEvent, watches EntityDiedEvent) on a game worker thread.
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

#include "emf/aim.hpp"

namespace companion {

class Companion {
public:
    enum class Phase { None, Active, Stopping, StopSent };
    static constexpr uint64_t kRegardIntervalMs = 3000;

    // Server tick: a companion was spawned for `player` in server world `world`; asks for one StartGuardingEvent.
    void on_spawned(uint32_t entity, uint32_t player, uint64_t now_ms, uintptr_t world) {
        std::lock_guard<std::mutex> l(m_);
        world_ = world;
        entity_ = entity;
        player_ = player;
        guard_target_ = 0;  // a new companion guards the player until an order says otherwise
        phase_ = Phase::Active;
        start_pending_ = true;
        last_start_ms_ = now_ms;
    }
    // Program: true once per start request; the event to send is (guard -> guarded).
    bool take_start(uint32_t* guard, uint32_t* guarded) {
        std::lock_guard<std::mutex> l(m_);
        if (phase_ != Phase::Active || !start_pending_) return false;
        start_pending_ = false;
        *guard = entity_;
        *guarded = guard_target_ ? guard_target_ : player_;
        return true;
    }
    // Server tick: an order changes whom it guards (0 = the player, else e.g. a stay marker); asks for one start.
    void request_guard(uint32_t target, uint64_t now_ms) {
        std::lock_guard<std::mutex> l(m_);
        if (phase_ != Phase::Active) return;
        guard_target_ = target;
        start_pending_ = true;
        last_start_ms_ = now_ms;
    }
    // Server tick, each check: the local player's entity now (emf::local_player()). When it is another one than the
    // companion belongs to, the companion is theirs from now on and, unless it guards a stay marker, guards them (one
    // start). False when nothing changed (the same player, none, or no companion out).
    bool on_player(uint32_t player, uint64_t now_ms) {
        std::lock_guard<std::mutex> l(m_);
        if (phase_ != Phase::Active || !player || player == player_) return false;
        player_ = player;
        if (!guard_target_) {
            start_pending_ = true;
            last_start_ms_ = now_ms;
        }
        return true;
    }
    // Server tick: dismiss the companion (stop guarding, then destroy).
    void request_stop() {
        std::lock_guard<std::mutex> l(m_);
        if (phase_ != Phase::Active) return;
        phase_ = Phase::Stopping;
        start_pending_ = false;
    }
    // Program: true once after request_stop.
    bool take_stop(uint32_t* guard, uint32_t* guarded) {
        std::lock_guard<std::mutex> l(m_);
        if (phase_ != Phase::Stopping) return false;
        phase_ = Phase::StopSent;
        *guard = entity_;
        *guarded = player_;
        return true;
    }
    // Server tick: the entity to destroy once the stop was sent (0 = nothing to do); forgets it.
    uint32_t take_destroy() {
        std::lock_guard<std::mutex> l(m_);
        if (phase_ != Phase::StopSent) return 0;
        const uint32_t id = entity_;
        reset_locked();
        return id;
    }
    // Program (EntityDiedEvent) or server tick: forget the companion if `entity` is it.
    void on_died(uint32_t entity) {
        std::lock_guard<std::mutex> l(m_);
        if (entity && entity == entity_) reset_locked();
    }
    // Server tick, every tick: the core's server session number (emf::server_session). A new number is a new hosted
    // session (a reload reuses the world's address, and entity ids are reused there): forget the companion in every
    // phase, so nothing is guarded, stopped or destroyed in the new session.
    void on_session(uint32_t session) {
        std::lock_guard<std::mutex> l(m_);
        if (session == session_) return;
        session_ = session;
        reset_locked();
    }
    // Server tick: the companion entity no longer exists.
    void on_gone() {
        std::lock_guard<std::mutex> l(m_);
        reset_locked();
    }
    // Server tick: the companion guards `current_guard_target`. If that is not the wanted target, ask for another
    // StartGuardingEvent, at most every kRegardIntervalMs. True when a new start was requested.
    bool wants_regard(uint32_t current_guard_target, uint64_t now_ms) {
        std::lock_guard<std::mutex> l(m_);
        const uint32_t wanted = guard_target_ ? guard_target_ : player_;
        if (phase_ != Phase::Active || start_pending_ || current_guard_target == wanted) return false;
        if (now_ms - last_start_ms_ < kRegardIntervalMs) return false;
        start_pending_ = true;
        last_start_ms_ = now_ms;
        return true;
    }

    uint32_t entity() const {
        std::lock_guard<std::mutex> l(m_);
        return entity_;
    }
    uint32_t player() const {
        std::lock_guard<std::mutex> l(m_);
        return player_;
    }
    Phase phase() const {
        std::lock_guard<std::mutex> l(m_);
        return phase_;
    }

private:
    void reset_locked() {
        world_ = 0;
        entity_ = player_ = 0;
        guard_target_ = 0;
        phase_ = Phase::None;
        start_pending_ = false;
    }
    mutable std::mutex m_;
    uintptr_t world_ = 0;
    uint32_t session_ = 0;
    uint32_t entity_ = 0, player_ = 0;
    uint32_t guard_target_ = 0;  // 0 = the player
    Phase phase_ = Phase::None;
    bool start_pending_ = false;
    uint64_t last_start_ms_ = 0;
};

// The order wheel key's default (mod.json "key"): F6, which the game leaves free (G tilts in build mode and sorts the
// backpack in menus). It is the one key that calls the companion by hand; the order keys start empty.
constexpr const char* kWheelKeyDefault = "F6";

// What a press of the order wheel key does: with no companion out it summons one, with one out it opens the order
// wheel (Dismiss is on it), while one is being dismissed nothing. One press, one thing.
enum class WheelPress { Nothing, Summon, OpenWheel };
inline WheelPress wheel_press(Companion::Phase phase) {
    if (phase == Companion::Phase::Active) return WheelPress::OpenWheel;
    if (phase == Companion::Phase::None) return WheelPress::Summon;
    return WheelPress::Nothing;
}

// The key notes tell the player to press to call the companion: the order wheel key's name, or where to set one when
// the player cleared it.
inline std::string call_key_text(const std::string& wheel_key_name) {
    return wheel_key_name.empty() ? "the order wheel key (set one in the Mods tab)" : wheel_key_name;
}

// Turns "key is down" samples into single presses.
struct KeyEdge {
    bool was_down = false;
    bool pressed(bool down) {
        const bool p = down && !was_down;
        was_down = down;
        return p;
    }
};

// The faction setting as typed in companion.ini: -1..15 is taken (ok); anything else (empty, a name, out of range)
// falls back to 11 (PlayerAlly) with ok = false, so a typo never turns the companion into a hostile.
struct FactionSetting {
    int value;
    bool ok;
};
inline FactionSetting parse_faction(const wchar_t* text) {
    if (!text) return {11, false};
    while (*text == L' ' || *text == L'\t') ++text;
    if (!*text) return {11, false};
    wchar_t* end = nullptr;
    const long v = std::wcstol(text, &end, 10);
    if (end == text) return {11, false};
    while (*end == L' ' || *end == L'\t') ++end;
    if (*end || v < -1 || v > 15) return {11, false};
    return {static_cast<int>(v), true};
}

// faction (from companion.ini): 0..15 is a keen FactionType to write on the companion (11 = PlayerAlly, the faction of
// the game's own summons: it fights at full damage; 7 = Player hits for 0). Returns the value to write when it is valid
// and differs from the companion's current faction; -1 (or anything else) keeps the template's faction.
inline std::optional<uint8_t> faction_to_write(int configured, uint8_t current) {
    if (configured < 0 || configured > 15 || static_cast<uint8_t>(configured) == current) return std::nullopt;
    return static_cast<uint8_t>(configured);
}

constexpr uint64_t kSettleMs = 5000;  // after a world appears, wait this long before summoning on our own

// When to summon a companion without the order wheel key (server tick only, not thread-safe).
// - A new session (emf::server_session) wants a summon when auto_summon is on or a companion was active when the
//   previous session ended (it comes back after a reload); it is due kSettleMs after that session's save has loaded
//   (on_loaded), or kLoadFallbackMs into the session if no load notice comes.
// - A death of a companion that was out arms one respawn_ms later when respawn_ms is set (a death read after a
//   dismissal does not).
// - A dismissal (the wheel's Dismiss, or the dismiss key) disarms until the next session.
// - A summon that could not spawn is tried again after a back-off: 5 s, doubling to 60 s (a success resets it).
class SummonPolicy {
public:
    static constexpr uint64_t kLoadFallbackMs = 60000;
    static constexpr uint64_t kFirstBackoffMs = 5000;
    static constexpr uint64_t kMaxBackoffMs = 60000;
    static constexpr uint64_t kLostSettleMs = 2000;    // a lost companion comes back this long after it was missed
    static constexpr int kMaxLost = 3;                 // lost more often than this within kLostWindowMs: it stays away
    static constexpr uint64_t kLostWindowMs = 120000;
    bool auto_summon = false;
    uint64_t respawn_ms = 0;

    void on_session(uint32_t session, uint64_t now_ms) {
        if (session == session_) return;
        session_ = session;
        wanted_ = auto_summon || active_;
        armed_ = false;
        lost_pending_ = false;
        loaded_ = false;
        session_start_ms_ = now_ms;
        backoff_ms_ = kFirstBackoffMs;
    }
    // Call between on_session and on_loaded: this world's save says a companion was out (after a game restart too).
    void want_on_load() {
        if (!loaded_) wanted_ = true;
    }
    void on_loaded(uint64_t now_ms) {
        if (!session_ || loaded_) return;
        loaded_ = true;
        if (!wanted_) return;
        wanted_ = false;
        armed_ = true;
        due_ms_ = now_ms + kSettleMs;
    }
    // A companion is out. `by_hand`: the player called it (the order wheel key), which starts the lost count over.
    void on_summoned(bool by_hand = false) {
        active_ = true;
        armed_ = false;
        wanted_ = false;
        lost_pending_ = false;
        backoff_ms_ = kFirstBackoffMs;
        if (by_hand) lost_.clear();
    }
    // A due summon could not spawn: try again after the back-off.
    void on_spawn_failed(uint64_t now_ms) {
        due_ms_ = now_ms + backoff_ms_;
        backoff_ms_ = backoff_ms_ * 2 > kMaxBackoffMs ? kMaxBackoffMs : backoff_ms_ * 2;
    }
    void on_dismissed() {
        active_ = false;
        armed_ = false;
        wanted_ = false;
        lost_pending_ = false;
    }
    // The companion vanished without dying while it was out: the game removes an enemy body that is away from every
    // player within a second (docs/engine_parts/re_teleport.md: 150 m away, removed 0.6 s later), so a fast travel, a
    // respawn at an altar or a long run leaves it behind and gone. A new one is due kLostSettleMs later, whatever
    // respawn_ms says (it did not die); where it comes back is come_back()'s. It comes back kMaxLost times within
    // kLostWindowMs; the next loss returns false (nothing armed): something keeps removing it, and a summon by hand
    // starts the count over.
    bool on_lost(uint64_t now_ms) {
        const bool was_out = active_;
        active_ = false;
        lost_pending_ = false;
        if (!was_out) return false;
        while (!lost_.empty() && now_ms - lost_.front() > kLostWindowMs) lost_.pop_front();
        if (static_cast<int>(lost_.size()) >= kMaxLost) {
            armed_ = false;
            return false;
        }
        lost_.push_back(now_ms);
        armed_ = true;
        lost_pending_ = true;
        due_ms_ = now_ms + kLostSettleMs;
        return true;
    }
    // The summon now due brings back a lost companion (come_back() decides where and when).
    bool lost_pending() const { return armed_ && lost_pending_; }
    // True when a new companion is now due respawn_ms later.
    bool on_died(uint64_t now_ms) {
        const bool was_out = active_;
        active_ = false;
        lost_pending_ = false;
        armed_ = was_out && respawn_ms > 0;
        due_ms_ = now_ms + respawn_ms;
        return armed_;
    }
    // A summon is wanted or armed (the companion is coming back).
    bool coming() const { return wanted_ || armed_; }
    bool due(uint64_t now_ms) {
        if (wanted_ && !loaded_ && now_ms - session_start_ms_ >= kLoadFallbackMs) {  // no load notice: go anyway
            loaded_ = true;
            wanted_ = false;
            armed_ = true;
            due_ms_ = now_ms;
        }
        return session_ && armed_ && now_ms >= due_ms_;
    }

private:
    uint32_t session_ = 0;
    bool wanted_ = false;  // a summon waits for this session's save to load
    bool loaded_ = false;
    uint64_t session_start_ms_ = 0;
    bool armed_ = false;
    bool active_ = false;  // a companion is out (summoned, not dismissed, not dead)
    uint64_t due_ms_ = 0;
    uint64_t backoff_ms_ = kFirstBackoffMs;
    std::deque<uint64_t> lost_;  // when it was lost, within the last kLostWindowMs
    bool lost_pending_ = false;  // the armed summon brings back a lost one
};

// Where a due summon happens now. A companion lost while it followed comes to the player once they have stood still
// (CatchUp::player_settled: never into the air beside a gliding player). One lost while it stayed comes back at its
// stay spot, and only once the player is within kStayReturnM of it: brought to the player it would walk back to its spot
// and be removed there again. Any other summon (auto, respawn, after a reload) comes to the player at once, as before.
constexpr double kStayReturnM = 50.0;
enum class ComeBack { Wait, AtPlayer, AtStaySpot };
inline ComeBack come_back(bool lost, bool staying, bool player_settled, double player_to_spot_m) {
    if (!lost) return ComeBack::AtPlayer;
    if (staying) return player_to_spot_m <= kStayReturnM ? ComeBack::AtStaySpot : ComeBack::Wait;
    return player_settled ? ComeBack::AtPlayer : ComeBack::Wait;
}

// The note for the player when the companion is lost or cannot come back. `armed`: it comes back by itself; else it
// stays away until called with `key`.
enum class LostNotice { Lost, ReplaceFailed };
inline std::string lost_notice(LostNotice what, bool armed, bool staying, const std::string& key) {
    if (!armed) return "Companion: it keeps getting lost; press " + key + " to call it again";
    if (what == LostNotice::ReplaceFailed)
        return "Companion: it could not be brought to you right now; it comes back when you stand still";
    if (staying) return "Companion: it waits at its stay spot; it shows up again when you come back there";
    return "Companion: it got lost; it comes back to you when you stand still";
}

// Where a lost companion comes back, and where a catch-up teleport aims: the player's own spot (the ground they stand
// on), not 2 m beside them (over a ledge, or beyond a wall). The teleport's target is 1 m up, for the game's ground
// search; a spawn goes half a metre up.
inline emf::aim::Vec3 catch_up_target(const emf::aim::Vec3& player) { return {player.x, player.y + 1.0, player.z}; }
inline emf::aim::Vec3 return_spot(const emf::aim::Vec3& player) { return {player.x, player.y + 0.5, player.z}; }

// What the server world shows at the companion's id, and what that means. Ids are reused once an entity is gone, so the
// template (its Enemy01BehaviorState template data, shared by every creature of the template) and the faction written
// at the spawn tell whether it is still ours.
struct BodySeen {
    bool exists = false;  // it has a CurrentTransform
    uintptr_t tpl = 0;    // its template data now (0: none)
    int faction = -1;     // its Faction now (-1: none)
    bool ai = false;      // DynamicEnemy01BehaviorState
    int64_t health = -1;  // -1: unknown
};
enum class Presence {
    Ours,   // there and well
    Dead,   // our body, health 0 (the death event follows)
    Gone,   // nothing there: the game removed it
    Taken,  // another creature has the id now: never touched, never adopted
    Body,   // our body without its AI, health left: destroy it before a new one comes
};
// `tpl_spawned`: the template data at the spawn (0: not readable, then not checked); `faction_expected`: the faction
// written at the spawn (-1: the template's own was kept, not checked).
inline Presence presence(const BodySeen& b, uintptr_t tpl_spawned, int faction_expected) {
    if (!b.exists) return Presence::Gone;
    if ((tpl_spawned && b.tpl != tpl_spawned) || (faction_expected >= 0 && b.faction != faction_expected))
        return Presence::Taken;
    if (b.ai) return Presence::Ours;
    return b.health == 0 ? Presence::Dead : Presence::Body;
}

// Keeps the companion with the player while it follows (the Follow and Attack orders; never while it stays): it is
// moved next to the player when it fell far behind or got stuck. The guard AI only walks, and the game removes an enemy
// body away from every player within a second, so after a teleport, a respawn at an altar, a long glide or a path it
// cannot find it would stay behind and vanish. Not while the player is on the move or in the air (it would land in the
// air): the player must have stood within 3 m for 2 s. One look a second (server tick).
class CatchUp {
public:
    // The limits for follow_distance 0 (the body's own guard distances); a longer follow_distance d moves them out to
    // d + 20, d + 30 and d + 10 m.
    static constexpr double kFar = 40.0;  // metres: farther than this for kFarMs (not in a fight): catch up
    static constexpr uint64_t kFarMs = 4000;
    static constexpr double kVeryFar = 60.0;  // farther than this: catch up even in a fight (the game would remove it)
    static constexpr double kStuckFar = 20.0;  // farther than this and not kStuckMove of way in kStuckMs: stuck
    static constexpr double kStuckMove = 2.0;
    static constexpr uint64_t kStuckMs = 8000;
    static constexpr double kSettled = 3.0;  // the player stood within this for kSettleMs: on the ground
    static constexpr uint64_t kSettleMs = 2000;
    static constexpr uint64_t kCooldownMs = 10000;  // between two catch-ups (a teleport takes a few steps)

    struct Limits {
        double behind, very_far, stuck;
    };
    explicit CatchUp(double follow_distance = 0.0) : follow_(follow_distance > 0.0 ? follow_distance : 0.0) {}
    Limits limits() const {
        return {(std::max)(kFar, follow_ + 20.0), (std::max)(kVeryFar, follow_ + 30.0), (std::max)(kStuckFar, follow_ + 10.0)};
    }
    struct Look {
        uint64_t now_ms = 0;
        emf::aim::Vec3 companion, player;
        bool following = true;  // its order keeps it with the player (Follow, Attack)
        bool fighting = false;  // its AI is in combat (enemy01 state Combat)
    };
    // True when the companion should be moved next to the player now.
    bool tick(const Look& l) {
        track(player_, l.player, kSettled, l.now_ms);
        track(companion_, l.companion, kStuckMove, l.now_ms);
        const double d = emf::aim::length(emf::aim::sub(l.companion, l.player));
        const Limits lim = limits();
        if (!l.following || l.now_ms < cooldown_until_ms_) {
            far_since_ms_ = 0;
            companion_.since_ms = l.now_ms;
            return false;
        }
        if (d > lim.behind) {
            if (!far_since_ms_) far_since_ms_ = l.now_ms;
        } else {
            far_since_ms_ = 0;
        }
        if (!player_settled()) return false;
        const bool behind = far_since_ms_ && l.now_ms - far_since_ms_ >= kFarMs;
        const bool stuck = d > lim.stuck && l.now_ms - companion_.since_ms >= kStuckMs;
        if (!(d > lim.very_far || (!l.fighting && (behind || stuck)))) return false;
        cooldown_until_ms_ = l.now_ms + kCooldownMs;
        far_since_ms_ = 0;
        return true;
    }
    bool player_settled() const { return player_.seen && last_ms_ - player_.since_ms >= kSettleMs; }
    // The player's position without a companion out (for player_settled before a lost companion comes back).
    void watch_player(uint64_t now_ms, const emf::aim::Vec3& p) { track(player_, p, kSettled, now_ms); }
    // A new companion or session (the follow distance stays).
    void reset() { *this = CatchUp{follow_}; }

private:
    struct Anchor {
        bool seen = false;
        emf::aim::Vec3 at;
        uint64_t since_ms = 0;  // when it was last farther than the step from `at`
    };
    void track(Anchor& a, const emf::aim::Vec3& p, double step, uint64_t now_ms) {
        last_ms_ = now_ms;
        if (!a.seen || emf::aim::length(emf::aim::sub(p, a.at)) > step) {
            a.seen = true;
            a.at = p;
            a.since_ms = now_ms;
        }
    }
    double follow_ = 0.0;
    Anchor player_, companion_;
    uint64_t last_ms_ = 0;
    uint64_t far_since_ms_ = 0;
    uint64_t cooldown_until_ms_ = 0;
};

// What the log says after the companion died; `key`: what calls a new one (call_key_text).
inline std::string death_message(bool respawns, uint64_t respawn_seconds, const std::string& key) {
    if (respawns) return "a new one comes in " + std::to_string(respawn_seconds) + " s";
    return key + " summons a new one";
}

// The level to write on the companion: `target` when it differs from the current one (0 = unreadable: never) and its
// faction is confirmed PlayerAlly (a failed faction write must never leave a strong hostile next to the player).
inline std::optional<uint32_t> level_to_write(uint32_t current, uint32_t target, bool ally_confirmed) {
    if (!current || !ally_confirmed || current == target) return std::nullopt;
    return target;
}

// What the per-world file (<game>\emf\config\companion\<slot>.txt) keeps: whether a companion was out, its order
// line (orders.h) and its gear lines (gear.h).
struct SavedCompanion {
    bool out = false;
    std::string order = "follow";
    std::string gear;
};
inline std::string serialize(const SavedCompanion& s) {
    return std::string("out ") + (s.out ? "1" : "0") + "\norder " + s.order + "\n" + s.gear;
}
inline SavedCompanion parse_saved(const std::string& text) {
    SavedCompanion s;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        if (line == "out 1") s.out = true;
        else if (line.rfind("order ", 0) == 0) s.order = line.substr(6);
        else if (line.rfind("piece ", 0) == 0) s.gear += line + "\n";
    }
    return s;
}

// follow_distance (metres, from companion.ini): the guard template's targetDistance becomes d and startDistance d + 2
// (it starts to follow once you are 2 m further away). d <= 0 keeps the template's values (false). Clamped to 50 m.
inline bool apply_follow_distance(float d, float* start, float* target) {
    if (!(d > 0.0f)) return false;
    if (d > 50.0f) d = 50.0f;
    *target = d;
    *start = d + 2.0f;
    return true;
}

// Gear may change hands only while this session's world file is known (on_load gave the slot and the file was read
// for this session); otherwise nothing would be saved and given gear would be lost.
inline bool world_file_known(uint16_t slot, uint32_t file_session, uint32_t session) {
    return slot != 0 && file_session == session;
}

// Writes `text` to `path` through a temporary file that then replaces it, so a crash leaves the old or the new file,
// never an empty one.
inline bool write_text_atomic(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return false;
    const bool wrote = fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = fclose(f) == 0;
    if (!wrote || !closed || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace companion
