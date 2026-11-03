#include "net_service.h"

namespace emf::core::net {

namespace {

bool part_ok(const std::string& s, size_t max) {
    if (s.empty() || s.size() > max) return false;
    for (const char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return false;
    return true;
}

bool fail(std::string* why, const std::string& r) {
    if (why) *why = r;
    return false;
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}  // namespace

NetService::Channel* NetService::find(const std::string& name) {
    const auto it = channels_.find(name);
    return it == channels_.end() ? nullptr : &it->second;
}

bool NetService::add_channel(const std::string& owner, const ChannelDef& def, std::string* why) {
    const size_t slash = def.name.find('/');
    if (slash == std::string::npos) return fail(why, "a channel is named <mod id>/<name>");
    const std::string mod = def.name.substr(0, slash), name = def.name.substr(slash + 1);
    if (!part_ok(mod, 48) || !part_ok(name, 32))
        return fail(why, "a channel's mod id and name are 1-48 and 1-32 of a-z 0-9 _ . -");
    if (lower(owner) != "lua" && lower(owner) != mod)
        return fail(why, "a channel goes under your own mod id (" + lower(owner) + "/...), not " + mod);
    if (def.version == 0 || def.version > 65535) return fail(why, "the version is 1..65535");
    if (!(def.flags & (kToServer | kToPlayers)) || (def.flags & ~(kToServer | kToPlayers)))
        return fail(why, "flags: EMF_NET_TO_SERVER and/or EMF_NET_TO_PLAYERS");
    if (def.max_size > kMaxMessage) return fail(why, "max_size is at most 16384");
    const std::lock_guard<std::mutex> g(m_);
    if (find(def.name)) return fail(why, def.name + " is taken");
    Channel c;
    c.def = def;
    if (!c.def.max_size) c.def.max_size = kDefaultMaxMessage;
    if (!c.def.per_10s) c.def.per_10s = kDefaultPer10s;
    c.owner = lower(owner);
    channels_.emplace(def.name, std::move(c));
    return true;
}

bool NetService::sendable(const std::string& owner, Channel* c, uint32_t flag, const std::string& data,
                          uint64_t sender, std::string* why) {
    if (!c) return fail(why, "no such channel");
    if (lower(owner) != c->owner) return fail(why, "only the mod that registered the channel sends on it");
    if (!(c->def.flags & flag))
        return fail(why, flag == kToServer ? "the channel does not go to the server"
                                           : "the channel does not go to players");
    if (data.size() > c->def.max_size)
        return fail(why, "too large (" + std::to_string(data.size()) + " bytes; the channel takes " +
                             std::to_string(c->def.max_size) + ")");
    auto& rl = c->senders[sender];
    if (!rl) rl = std::make_unique<RateLimit>(c->def.per_10s / 10.0, static_cast<double>(c->def.per_10s));
    if (!rl->take(d_.now_ms()))
        return fail(why, "over the channel's rate (" + std::to_string(c->def.per_10s) + " in 10 s)");
    return true;
}

bool NetService::to_server(const std::string& owner, const std::string& channel, const std::string& data,
                           std::string* why) {
    uint32_t local = 0;
    NetHandler handler;
    NetFrom from;
    {
        const std::lock_guard<std::mutex> g(m_);
        Channel* c = find(channel);
        if (!sendable(owner, c, kToServer, data, 0, why)) return false;
        if (!d_.hosted(&local)) return fail(why, "no server to send to yet (it has not welcomed this game)");
        handler = c->def.on_server;
        from.player = local;
        from.local = true;
        from.admin = true;
        from.peer_version = c->def.version;
    }
    if (handler) d_.post(EMF_WORLD_SERVER, [handler, from, data](EmfWorld* w) { handler(w, from, data); });
    return true;
}

bool NetService::to_player(const std::string& owner, uint32_t player, const std::string& channel,
                           const std::string& data, std::string* why) {
    uint32_t local = 0;
    NetHandler handler;
    NetFrom from;
    {
        const std::lock_guard<std::mutex> g(m_);
        Channel* c = find(channel);
        if (!c) return fail(why, "no such channel");
        if (!d_.hosted(&local) || player != local)
            return fail(why, "that player's game has not said hello with this channel");
        if (!sendable(owner, c, kToPlayers, data, player, why)) return false;
        handler = c->def.on_player;
        from.local = true;
        from.peer_version = c->def.version;
    }
    if (handler) d_.post(EMF_WORLD_CLIENT, [handler, from, data](EmfWorld* w) { handler(w, from, data); });
    return true;
}

uint32_t NetService::to_players(const std::string& owner, const std::string& channel, const std::string& data) {
    uint32_t local = 0;
    if (!d_.hosted(&local)) return 0;  // remote peers come with the chat transport
    return to_player(owner, local, channel, data, nullptr) ? 1u : 0u;
}

uint32_t NetService::peer(uint32_t player, const std::string& channel) {
    const std::lock_guard<std::mutex> g(m_);
    Channel* c = find(channel);
    if (!c) return 0;
    uint32_t local = 0;
    if (!d_.hosted(&local)) return 0;
    return (player == 0 || player == local) ? c->def.version : 0;
}

}  // namespace emf::core::net
