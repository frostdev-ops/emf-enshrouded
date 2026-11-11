#pragma once
// Markers (API v33) from a server for players' games, over the core channel emf/markers (mod messages, API v27). The
// sender (the server world's side) batches a mod's markers into messages of at most 64, cuts a call at 256, and keeps
// the entities of followed markers per player so that refresh() can send the positions of those that moved more than
// 4 m (a far entity is not in that player's game). The receiver (a player's game) puts them into the store as received
// markers. Game-free: the core gives the send function (NetService) and the positions.
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "core/markers.h"

namespace emf::core {

constexpr const char* kMarkersChannel = "emf/markers";
constexpr uint32_t kMarkersChannelVersion = 1;
constexpr uint32_t kMarkersPerSend = 256;      // one marker_send call
constexpr size_t kMarkersPerNetMessage = 64;   // one message at most this many
constexpr size_t kMarkerNetBudget = 15000;     // ... and at most this many bytes (16 KB is the channel's limit)
constexpr size_t kMarkersStoredMax = 16384;    // what players should have, kept for replays
constexpr double kMarkerRefreshMoveM = 4.0;    // a followed entity's position is sent again after it moved this far
constexpr uint64_t kMarkerRefreshMs = 2000;    // how often the core calls refresh

class MarkerSender {
public:
    // player 0 = every player with the channel; returns how many players it went to
    using SendFn = std::function<uint32_t(uint32_t player, const std::string& data)>;
    using LogFn = std::function<void(const std::string&)>;
    MarkerSender(SendFn send, LogFn log) : send_(std::move(send)), log_(std::move(log)) {}

    // How many players got them (0: refused, nobody, or every marker was invalid).
    uint32_t send(const std::string& owner, uint32_t player, const EmfMarker* markers, uint32_t count);
    uint32_t unsend(const std::string& owner, uint32_t player, const char* id);
    // The server world's thread: positions of followed entities that moved.
    void refresh(const std::function<bool(uint32_t entity, double out[3])>& pos_of);
    // A player's game linked up (its hello came): everything that player should have (theirs and every player's) again.
    void replay(uint32_t player);
    void forget(uint32_t player);  // a player left
    void reset();                  // a new world
    bool any_sent() const;         // the core greets players once a mod sent markers

private:
    struct Follow {
        uint32_t entity = 0;
        double sent[3] = {};
    };
    struct Stored {  // a sent marker, kept for a replay (its id and text pointers set when it is sent)
        EmfMarker m{};
        std::string text;
    };
    using Key = std::tuple<uint32_t, std::string, std::string>;  // player, owner, id
    // owner's markers as messages of at most kMarkersPerNetMessage and kMarkerNetBudget bytes
    static std::vector<std::string> batches(const std::string& owner, const std::vector<EmfMarker>& markers);
    void log_once(bool* told, const std::string& line);
    SendFn send_;
    LogFn log_;
    mutable std::mutex m_;
    std::map<Key, Follow> follow_;
    std::map<Key, Stored> stored_;  // what each player (0: every player) should have
    bool told_bad_ = false, told_full_ = false, told_refused_ = false;
    bool sent_ = false;
    bool told_cut_ = false;
};

class MarkerReceiver {
public:
    using LogFn = std::function<void(const std::string&)>;
    explicit MarkerReceiver(LogFn log) : log_(std::move(log)) {}
    // false: not a message of this channel (logged once); the markers it holds go through the store's own checks
    bool on_message(const std::string& data, MarkerStore& store);
    void reset(MarkerStore& store);  // the world ended or the server link dropped: received markers go

private:
    LogFn log_;
    bool told_ = false;
};

}  // namespace emf::core
