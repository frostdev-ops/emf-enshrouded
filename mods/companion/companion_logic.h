#pragma once
// Companion state shared between the server tick (spawn, stop, destroy, checks) and the companion's ECS
// program (sends StartGuardingEvent / StopGuardingEvent, watches EntityDiedEvent) on a game worker thread.
#include <windows.h>

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

// When to summon a companion without F6 (server tick only, not thread-safe).
// - A new session (emf::server_session) wants a summon when auto_summon is on or a companion was active when the
//   previous session ended (it comes back after a reload); it is due kSettleMs after that session's save has loaded
//   (on_loaded), or kLoadFallbackMs into the session if no load notice comes.
// - A death of a companion that was out arms one respawn_ms later when respawn_ms is set (a death read after an F6
//   dismissal does not).
// - An F6 dismissal disarms until the next session.
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
    void on_summoned() {
        active_ = true;
        armed_ = false;
        wanted_ = false;
        backoff_ms_ = kFirstBackoffMs;
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
    }
    // The companion vanished without dying while it was out: the game removes an enemy body that is away from every
    // player within a second (docs/engine_parts/re_teleport.md: 150 m away, removed 0.6 s later), so a fast travel, a
    // respawn at an altar or a long run leaves it behind and gone. A new one is due kLostSettleMs later, whatever
    // respawn_ms says (it did not die). False (nothing armed) when it was not out, or was lost kMaxLost times within
    // kLostWindowMs already: something keeps removing it, and a summon by hand starts over.
    bool on_lost(uint64_t now_ms) {
        const bool was_out = active_;
        active_ = false;
        if (!was_out) return false;
        while (!lost_.empty() && now_ms - lost_.front() > kLostWindowMs) lost_.pop_front();
        if (static_cast<int>(lost_.size()) >= kMaxLost) {
            armed_ = false;
            return false;
        }
        lost_.push_back(now_ms);
        armed_ = true;
        due_ms_ = now_ms + kLostSettleMs;
        return true;
    }
    // True when a new companion is now due respawn_ms later.
    bool on_died(uint64_t now_ms) {
        const bool was_out = active_;
        active_ = false;
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
};

// Keeps the companion with the player while it follows (the Follow and Attack orders; never while it stays): it is
// moved next to the player when it fell far behind or got stuck. The guard AI only walks, and the game removes an enemy
// body away from every player within a second, so after a teleport, a respawn at an altar, a long glide or a path it
// cannot find it would stay behind and vanish. Not while the player is on the move or in the air (it would land in the
// air): the player must have stood within 3 m for 2 s. One look a second (server tick).
class CatchUp {
public:
    static constexpr double kFar = 40.0;  // metres: farther than this for kFarMs (not in a fight): catch up
    static constexpr uint64_t kFarMs = 4000;
    static constexpr double kVeryFar = 60.0;  // farther than this: catch up even in a fight (the game would remove it)
    static constexpr double kStuckFar = 20.0;  // farther than this and not kStuckMove of way in kStuckMs: stuck
    static constexpr double kStuckMove = 2.0;
    static constexpr uint64_t kStuckMs = 8000;
    static constexpr double kSettled = 3.0;  // the player stood within this for kSettleMs: on the ground
    static constexpr uint64_t kSettleMs = 2000;
    static constexpr uint64_t kCooldownMs = 10000;  // between two catch-ups (a teleport takes a few steps)

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
        if (!l.following || l.now_ms < cooldown_until_ms_) {
            far_since_ms_ = 0;
            companion_.since_ms = l.now_ms;
            return false;
        }
        if (d > kFar) {
            if (!far_since_ms_) far_since_ms_ = l.now_ms;
        } else {
            far_since_ms_ = 0;
        }
        if (!player_settled()) return false;
        const bool behind = far_since_ms_ && l.now_ms - far_since_ms_ >= kFarMs;
        const bool stuck = d > kStuckFar && l.now_ms - companion_.since_ms >= kStuckMs;
        if (!(d > kVeryFar || (!l.fighting && (behind || stuck)))) return false;
        cooldown_until_ms_ = l.now_ms + kCooldownMs;
        far_since_ms_ = 0;
        return true;
    }
    bool player_settled() const { return player_.seen && last_ms_ - player_.since_ms >= kSettleMs; }
    // The player's position without a companion out (for player_settled before a lost companion comes back).
    void watch_player(uint64_t now_ms, const emf::aim::Vec3& p) { track(player_, p, kSettled, now_ms); }
    // A new companion or session.
    void reset() { *this = CatchUp{}; }

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
    Anchor player_, companion_;
    uint64_t last_ms_ = 0;
    uint64_t far_since_ms_ = 0;
    uint64_t cooldown_until_ms_ = 0;
};

// What the log says after the companion died.
inline std::string death_message(bool respawns, uint64_t respawn_seconds) {
    if (respawns) return "a new one comes in " + std::to_string(respawn_seconds) + " s";
    return "F6 summons a new one";
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
