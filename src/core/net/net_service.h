#pragma once
// Mod messages (API v27): the channels mods register and the delivery of their messages. Loopback in a game this
// process hosts (or an offline one): the local game's messages reach the server's handler, the server's messages to the
// host's own player reach the local handler, each after the next step of the world it is for. Remote peers come with
// the chat transport. Testable: the game is reached only through NetDeps.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "emf/emf_api.h"
#include "net_peers.h"

namespace emf::core::net {

constexpr uint32_t kToServer = 0x01u;   // players' games send it, the server receives it
constexpr uint32_t kToPlayers = 0x02u;  // the server sends it, players' games receive it
constexpr uint32_t kMaxMessage = 16384u;
constexpr uint32_t kDefaultMaxMessage = 4096u;
constexpr uint32_t kDefaultPer10s = 100u;

struct NetFrom {
    uint32_t player = 0;       // server: the sender's player entity; a player's game: 0
    uint64_t account = 0;      // server: the sender's account (the server's own records); 0 in process
    uint32_t peer_version = 0; // the channel version the other side has
    bool admin = false;        // server: the host's own game (or a [chat] admin)
    bool local = false;        // same process (the host's own game, an offline game)
};
using NetHandler = std::function<void(EmfWorld*, const NetFrom&, const std::string& data)>;

struct ChannelDef {
    std::string name;          // "<mod id>/<name>"
    uint32_t version = 1;      // 1..65535
    uint32_t flags = 0;        // kToServer | kToPlayers
    uint32_t max_size = 0;     // 0 = kDefaultMaxMessage
    uint32_t per_10s = 0;      // messages one sender may send in 10 s; 0 = kDefaultPer10s
    NetHandler on_server;      // the server world's thread, after its step
    NetHandler on_player;      // the client world's thread, after its step
};

struct NetDeps {
    std::function<bool(uint32_t* local_player)> hosted;  // this process hosts the server world with a local player
    std::function<void(uint8_t world_kind, std::function<void(EmfWorld*)>)> post;  // run after that world's next step
    std::function<uint64_t()> now_ms;
    std::function<void(const std::string&)> log;
};

class NetService {
public:
    explicit NetService(NetDeps deps) : d_(std::move(deps)) {}

    // `owner`: the calling mod (its module name); the script host "lua" may register its scripts' channels.
    bool add_channel(const std::string& owner, const ChannelDef& def, std::string* why);
    // A player's game -> the server. True when it is on its way.
    bool to_server(const std::string& owner, const std::string& channel, const std::string& data, std::string* why);
    // The server -> one player's game (player: its entity in the server world).
    bool to_player(const std::string& owner, uint32_t player, const std::string& channel, const std::string& data,
                   std::string* why);
    // The server -> every player whose game has the channel; how many it went to.
    uint32_t to_players(const std::string& owner, const std::string& channel, const std::string& data);
    // The channel version `player`'s game has (server side), or the server's (player 0, a player's game); 0 = none.
    uint32_t peer(uint32_t player, const std::string& channel);

private:
    struct Channel {
        ChannelDef def;
        std::string owner;
        std::map<uint64_t, std::unique_ptr<RateLimit>> senders;  // by sender (0 = the local game)
    };
    Channel* find(const std::string& name);
    bool sendable(const std::string& owner, Channel* c, uint32_t flag, const std::string& data, uint64_t sender,
                  std::string* why);

    NetDeps d_;
    std::mutex m_;
    std::map<std::string, Channel> channels_;
};

}  // namespace emf::core::net
