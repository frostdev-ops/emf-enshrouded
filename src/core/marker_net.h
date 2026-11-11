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

#include "core/markers.h"

namespace emf::core {

constexpr const char* kMarkersChannel = "emf/markers";
constexpr uint32_t kMarkersChannelVersion = 1;
constexpr uint32_t kMarkersPerSend = 256;      // one marker_send call
constexpr size_t kMarkersPerNetMessage = 64;   // one message (16 KB is the channel's limit)
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
    void forget(uint32_t player);  // a player left
    void reset();                  // a new world
    bool any_sent() const;         // the core greets players once a mod sent markers

private:
    struct Follow {
        uint32_t entity = 0;
        double sent[3] = {};
    };
    using Key = std::tuple<uint32_t, std::string, std::string>;  // player, owner, id
    SendFn send_;
    LogFn log_;
    mutable std::mutex m_;
    std::map<Key, Follow> follow_;
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
