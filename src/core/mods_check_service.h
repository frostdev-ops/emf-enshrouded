#pragma once
// The mod check on join between an EMF game and an EMF server (the core channel emf/mods, mods_check.h): a joined game
// sends its list once per session when the server has the channel; the server keeps it per account, logs the
// differences and answers with its own list; the game compares and shows a note once per session when it lacks or has
// another version of a mod that must match. The host's own game never sends (its server is this process).
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/mods_check.h"

namespace emf::core {

constexpr const char* kModsChannel = "emf/mods";
constexpr uint32_t kModsChannelVersion = 1;
constexpr size_t kModsCheckMaxPlayers = 256;  // accounts kept on the server (the oldest goes)

class ModsCheck {
public:
    struct Deps {
        std::function<std::vector<ModEntry>()> mine;  // this game's list (with its "e emf <version>" entry)
        std::function<bool()> joined;                 // this game is in a world another process runs
        std::function<uint32_t()> server_version;     // the server's emf/mods version (0: none, or no welcome yet)
        std::function<uint32_t()> client_session;     // +1 per client world: a new session sends and notes again
        std::function<bool(const std::string&)> to_server;
        std::function<bool(uint32_t player, const std::string&)> to_player;
        std::function<void(const std::string&)> notify;  // the player's note
        std::function<void(const std::string&)> log;
    };
    struct Seen {
        uint32_t player = 0;
        std::string emf;  // the player's EMF version ("" when the list did not say)
        std::vector<ModEntry> mods;
        std::string diff;  // diff_text against the server's list
    };

    explicit ModsCheck(Deps d) : d_(std::move(d)) {}

    // A joined game, once a second (client world).
    void client_tick() {
        if (!d_.joined || !d_.joined()) return;
        if (!d_.server_version || d_.server_version() == 0) return;
        const uint32_t session = d_.client_session ? d_.client_session() : 0;
        if (sent_ && sent_session_ == session) return;
        if (d_.to_server && d_.to_server(encode_mods(d_.mine ? d_.mine() : std::vector<ModEntry>{}))) {
            sent_ = true;
            sent_session_ = session;
        }
    }

    // The server: a player's game sent its list.
    void on_server_message(uint32_t player, uint64_t account, const std::string& data) {
        std::string why;
        const auto theirs = decode_mods(data, &why);
        if (!theirs) {
            told("a mod list from " + acct(account) + " was not taken: " + why);
            return;
        }
        const auto mine = d_.mine ? d_.mine() : std::vector<ModEntry>{};
        Seen s;
        s.player = player;
        for (const auto& m : *theirs)
            if (m.kind == 'e' && m.id == "emf") s.emf = m.version;
        s.mods = *theirs;
        s.diff = diff_text(compare_mods(mine, *theirs));
        if (d_.log)
            d_.log("mods check: player " + acct(account) + " (entity " + std::to_string(player) + ", EMF " +
                   (s.emf.empty() ? std::string("?") : s.emf) + "): " + s.diff);
        {
            std::lock_guard<std::mutex> l(m_);
            if (!seen_.count(account) && seen_.size() >= kModsCheckMaxPlayers) {
                seen_.erase(order_.front());
                order_.erase(order_.begin());
            }
            if (!seen_.count(account)) order_.push_back(account);
            seen_[account] = std::move(s);
        }
        if (d_.to_player) d_.to_player(player, encode_mods(mine));
    }

    // The joined game: the server's list came.
    void on_player_message(const std::string& data) {
        std::string why;
        const auto server = decode_mods(data, &why);
        if (!server) {
            told("the server's mod list was not taken: " + why);
            return;
        }
        const ModDiff d = compare_mods(*server, d_.mine ? d_.mine() : std::vector<ModEntry>{});
        if (d_.log) d_.log("mods check: against the server's mods: " + diff_text(d));
        const std::string note = player_note(d);
        const uint32_t session = d_.client_session ? d_.client_session() : 0;
        if (note.empty() || (noted_ && noted_session_ == session)) return;
        noted_ = true;
        noted_session_ = session;
        if (d_.notify) d_.notify(note);
    }

    std::optional<Seen> player(uint64_t account) const {
        std::lock_guard<std::mutex> l(m_);
        const auto it = seen_.find(account);
        return it == seen_.end() ? std::nullopt : std::optional<Seen>(it->second);
    }

    // Whether this game has mods its players must match (content or side "both"): only then does the core's channel
    // alone make the server greet new connections.
    bool has_shared_mods() const {
        for (const auto& m : d_.mine ? d_.mine() : std::vector<ModEntry>{})
            if (mod_kind_compared(m.kind)) return true;
        return false;
    }

    bool server_has_content() const {
        for (const auto& m : d_.mine ? d_.mine() : std::vector<ModEntry>{})
            if (m.kind == 'c') return true;
        return false;
    }

private:
    static std::string acct(uint64_t a) {
        char b[24];
        std::snprintf(b, sizeof b, "0x%016llx", static_cast<unsigned long long>(a));
        return b;
    }
    void told(const std::string& line) {  // once per kind of problem
        const std::string key = line.substr(line.rfind(": ") + 2);
        {
            std::lock_guard<std::mutex> l(m_);
            if (!told_.insert(key).second) return;
        }
        if (d_.log) d_.log("mods check: " + line + " (logged once)");
    }

    Deps d_;
    mutable std::mutex m_;
    std::map<uint64_t, Seen> seen_;
    std::vector<uint64_t> order_;
    std::set<std::string> told_;
    bool sent_ = false, noted_ = false;  // the client world's thread
    uint32_t sent_session_ = 0, noted_session_ = 0;
};

}  // namespace emf::core
