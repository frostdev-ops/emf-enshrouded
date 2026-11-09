#pragma once
// API v28 on_player: the server world's players go through the roster (player_roster.h) each step, and the changes go
// to the mods' callbacks on that world's thread after its step (a callback that faults is switched off and named in
// the log, as on_chat's are). The list is read at most 4 times a second, and only while a mod watches; deaths (from
// the event relay's EntityDiedEvent, the killer from HitEvent) go out with the next step, their killers worked out then
// (the relay hands a step's deaths before its hits). A new server session waits for its world's load (world_loaded) and
// joins everyone 1 s after the first step that follows it. A mod that starts watching later is told about the players
// already there (as joined, to it alone) at its first step.
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/player_roster.h"
#include "emf/emf_api.h"

namespace emf::core {

constexpr uint64_t kPlayerArmDelayMs = 1000;  // after a world's load event: the mods' world data first
constexpr uint64_t kPlayerReadMs = 250;       // the player list is read at most this often

// The server world's players (entities 1..1023 with PlayerState and a position) through the API table: account from
// BasicPlayerInfos (0: not known yet), ready (PlayerInput.fromClientIsReadyForGameplay; without PlayerInput, or the
// host's own player: ready), host (the local player), name (player_name, "" when none).
std::vector<RosterSeen> read_players(const EmfApi* api, EmfWorld* w);

class PlayerEvents {
public:
    struct Deps {
        std::function<std::vector<RosterSeen>(EmfWorld*)> read;
        std::function<uint64_t()> now_ms;
        std::function<void(const std::string&)> log;
        // calls a mod's callback; false when it faulted
        std::function<bool(EmfPlayerCallback, EmfWorld*, const EmfPlayerEvent*, void*)> call;
        std::function<std::string(const void*)> module_of;  // the module a callback is in (for the log)
    };
    explicit PlayerEvents(Deps d) : d_(std::move(d)) {}

    bool watch(EmfPlayerCallback cb, void* user);  // any thread; false for a null cb
    bool watched() const { return watched_.load(); }
    // The server world's thread:
    void world_loaded();                          // a world's save loaded: everyone joins kPlayerArmDelayMs after the next step
    void hit(uint32_t target, uint32_t source);   // a HitEvent (its root source); only players' (ids 1..1023) count
    void died(uint32_t entity);                   // an EntityDiedEvent: a joined player's goes out with the next step
    void server_step(EmfWorld* w, uint32_t session);

private:
    struct Watcher {
        EmfPlayerCallback cb = nullptr;
        void* user = nullptr;
        bool on = true;
        bool intro = true;  // not told about the players already there yet
    };
    void deliver(EmfWorld* w, const std::vector<RosterEvent>& events);
    void introduce(EmfWorld* w);
    bool call(EmfWorld* w, const Watcher& x, const RosterEvent& ev);  // false: it faulted (switched off, logged)

    Deps d_;
    std::mutex m_;  // watchers_
    std::vector<Watcher> watchers_;
    std::atomic<bool> watched_{false};
    // the server world's thread
    PlayerRoster roster_;
    uint32_t session_ = 0;
    bool arm_pending_ = false;
    uint64_t arm_at_ = 0;
    bool armed_ = false;
    uint64_t last_read_ = 0;
    bool read_once_ = false;
    std::vector<uint32_t> deaths_;  // this step's deaths (their killers once the step's hits are in)
};

}  // namespace emf::core
