#pragma once
// Markers step 2 (API v34): map actions over the core channel emf/mapact. The server end keeps the actions mods
// registered (16 at most), announces them to players' games ({"a": [{"i", "l", "h"}]}) and hands a player's finished
// gesture ({"i", "x", "z"}) to the mod after its checks (a known action, x and z inside the world, 20 per player in
// 10 s). The client end keeps the announced list: its first action is what Shift + hold does. Game-free.
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

constexpr const char* kMapActionsChannel = "emf/mapact";
constexpr uint32_t kMapActionsChannelVersion = 1;
constexpr size_t kMapActionsMax = 16;
constexpr size_t kMapDonePer10s = 20;

class MapActions {
public:
    using LogFn = std::function<void(const std::string&)>;
    explicit MapActions(LogFn log) : log_(std::move(log)) {}
    // false: a bad id (1-48 of A-Z a-z 0-9 _ . - :) or 16 already (the same id again replaces it). A null cb removes the
    // owner's action with that id (false: it had none).
    bool add(const std::string& owner, const char* id, const char* label, uint32_t hold_ms, EmfMapActionCallback cb, void* user);
    bool remove(const std::string& owner, const char* id);
    size_t remove_owner(const std::string& owner);
    bool empty() const;
    std::string announce_message() const;
    // A player's finished gesture (the server world's thread): true when it reached a mod.
    bool on_done(EmfWorld* world, uint32_t player, const std::string& data, uint64_t now_ms);
    void forget(uint32_t player);

private:
    struct Action {
        std::string owner, id, label;
        uint32_t hold_ms = 0;
        EmfMapActionCallback cb = nullptr;
        void* user = nullptr;
    };
    void log_once(const std::string& kind, const std::string& line);
    LogFn log_;
    mutable std::mutex m_;
    std::vector<Action> actions_;
    std::map<uint32_t, std::deque<uint64_t>> recent_;  // player -> times of their last gestures
    std::map<std::string, bool> told_;
};

class MapActionsClient {
public:
    bool on_announce(const std::string& data);  // false: not this channel's message (the list is kept)
    bool announced() const;
    std::string first_id() const;
    uint32_t hold_ms() const;
    // The message for a finished gesture at (x, z): "" when no action is announced.
    std::string done(double x, double z) const;
    static std::string done_message(const char* id, double x, double z);
    void reset();  // a new session: the server's list goes

private:
    mutable std::mutex m_;
    std::string first_;
    uint32_t hold_ms_ = 1000;
};

}  // namespace emf::core
