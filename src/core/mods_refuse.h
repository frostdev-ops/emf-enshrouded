#pragma once
// Refusing a join ([net] mods_check = refuse, mods_policy.h): on the server world's thread, about once a second, the
// players in the world are checked once each is ready for gameplay. One whose game sent its mod list (ModsCheck) and
// lacks a required mod, or has another version of one, is refused at once; one with no list 60 s after being ready has
// no EMF (or an older one) and is refused then. Refused: a chat line that says why (every game shows it), then 5 s later
// the game's own kick; 10 s after it a player still there is logged (the game did not take the kick). The host's own
// player is never checked and admins are never refused (the game's kick skips them too). A player who leaves is
// forgotten: back, they are checked again. Pure: the deps do the reading, the telling and the kicking.
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
#include "core/mods_policy.h"
#include "core/player_roster.h"

namespace emf::core {

constexpr uint64_t kRefuseNoListMs = 60000;    // ready this long without a list: no EMF
constexpr uint64_t kRefuseKickDelayMs = 5000;  // from the chat line to the kick
constexpr uint64_t kRefuseGoneCheckMs = 10000; // from the kick to "still here"
constexpr size_t kRefuseMaxNamed = 8;          // the chat line names at most this many, then "and N more"

// A mod a player's game must have and does not (or has in another version).
struct Needed {
    char kind = 'u';
    std::string id;
    std::string version;  // "" = any version (a required id the server does not run itself)
    std::string theirs;   // the player's version, "" = none (or no list)
};

// What `theirs` lacks: the server's content (c) and side-both (b) entries at their versions, then each required id (the
// server's version when it runs that mod, else any). theirs == nullptr: no list, so all of them. EMF itself is never one.
inline std::vector<Needed> refusal_needs(const std::vector<ModEntry>& server, const std::vector<std::string>& required,
                                         const std::vector<ModEntry>* theirs) {
    std::vector<Needed> want;
    const auto wanted = [&](const std::string& id) {
        for (const auto& w : want)
            if (w.id == id) return true;
        return false;
    };
    for (const auto& m : server)
        if (mod_kind_compared(m.kind) && !wanted(m.id)) want.push_back({m.kind, m.id, m.version, ""});
    for (const auto& id : required) {
        if (wanted(id)) continue;
        const ModEntry* run = nullptr;
        bool is_emf = false;
        for (const auto& m : server)
            if (m.id == id) {
                if (m.kind == 'e') is_emf = true;
                else if (!run) run = &m;
            }
        if (is_emf || id == "emf") continue;
        want.push_back(run ? Needed{run->kind, id, run->version, ""} : Needed{'u', id, "", ""});
    }
    if (!theirs) return want;
    std::vector<Needed> out;
    for (auto& w : want) {
        const ModEntry* have = nullptr;
        for (const auto& m : *theirs)
            if (m.kind != 'e' && m.id == w.id) {
                have = &m;
                break;
            }
        if (!have) {
            out.push_back(w);
        } else if (!w.version.empty() && have->version != w.version) {
            w.theirs = have->version;
            out.push_back(w);
        }
    }
    return out;
}

// The chat line of a refusal (the words are the core's own call; every game shows them, EMF or not).
inline std::string refuse_text(const std::vector<Needed>& needs, bool no_emf, const std::string& info) {
    std::string names;
    for (size_t i = 0; i < needs.size() && i < kRefuseMaxNamed; ++i) {
        const Needed& n = needs[i];
        names += (i ? ", " : "") + n.id + (n.version.empty() ? "" : " " + n.version);
        if (!no_emf) names += " (you: " + (n.theirs.empty() ? std::string("none") : n.theirs) + ")";
    }
    if (needs.size() > kRefuseMaxNamed) names += ", and " + std::to_string(needs.size() - kRefuseMaxNamed) + " more";
    std::string t;
    if (no_emf)
        t = "This server needs EMF" + (needs.empty() ? std::string() : " and these mods: " + names) + ".";
    else
        t = "This server needs these mods: " + names + ".";
    if (!info.empty()) {
        const char last = info.back();
        t += std::string(" Get ") + (needs.empty() ? "it" : "them") + " from " + info +
             (last == '.' || last == '!' || last == '?' ? "" : ".");
    } else if (!no_emf) {
        t += " Install them with EMF.exe.";
    }
    return t + " You will be disconnected in 5 seconds.";
}

class ModsRefuse {
public:
    struct Deps {
        // the list `entity` (with this account) sent on its current connection, or nothing
        std::function<std::optional<std::vector<ModEntry>>(uint64_t account, uint32_t entity)> list_of;
        std::function<std::vector<ModEntry>()> mine;  // the server's list
        std::function<bool(uint32_t entity, uint64_t account)> is_admin;
        std::function<bool(uint32_t entity, uint64_t account, const std::string& text)> tell;  // false: not queued
        std::function<bool(uint64_t account)> kick;                                            // false: not sent
        std::function<void(const std::string&)> log;
    };

    ModsRefuse(ModsPolicy policy, Deps d) : p_(std::move(policy)), d_(std::move(d)) {}

    // The server world's thread: the players in the world now (RosterSeen as read_players gives them).
    void tick(const std::vector<RosterSeen>& players, uint64_t now_ms) {
        std::lock_guard<std::mutex> l(m_);
        std::set<uint64_t> present;
        for (const auto& p : players)
            if (p.account) present.insert(p.account);
        for (auto it = w_.begin(); it != w_.end();) it = present.count(it->first) ? std::next(it) : w_.erase(it);
        for (const auto& p : players) {
            if (p.host || !p.account || !p.ready) continue;
            auto it = w_.find(p.account);
            if (it != w_.end() && it->second.entity != p.entity) {  // back as another entity: starts over
                w_.erase(it);
                it = w_.end();
            }
            if (it == w_.end()) {
                Watch w;
                w.entity = p.entity;
                w.since = now_ms;
                it = w_.emplace(p.account, w).first;
            }
            step(p, it->second, now_ms);
        }
    }

    // For players.mods: "" (nothing to say), "admin: not refused (...)" or "refused (...)...".
    std::string state_of(uint64_t account) const {
        std::lock_guard<std::mutex> l(m_);
        const auto it = w_.find(account);
        if (it == w_.end()) return "";
        const Watch& w = it->second;
        switch (w.phase) {
            case Phase::Admin: return "admin: not refused (" + w.what + ")";
            case Phase::Telling: return "refused (" + w.what + "): the kick follows";
            case Phase::Kicked: return "refused (" + w.what + "): kicked";
            case Phase::Done: return "refused (" + w.what + "): still here after the kick";
            default: return "";
        }
    }

private:
    enum class Phase { Watching, Ok, Admin, Telling, Kicked, Done };
    struct Watch {
        uint32_t entity = 0;
        uint64_t since = 0;  // ready since
        Phase phase = Phase::Watching;
        uint64_t decided = 0, kicked = 0;
        bool told = false, tell_failed = false;
        std::string what, text;
    };

    static std::string who(const RosterSeen& p) {
        char b[24];
        std::snprintf(b, sizeof b, "0x%016llx", static_cast<unsigned long long>(p.account));
        return std::string("player ") + b + " (entity " + std::to_string(p.entity) +
               (p.name.empty() ? std::string() : ", " + p.name) + ")";
    }
    void log(const std::string& s) {
        if (d_.log) d_.log("mods check: " + s);
    }

    void step(const RosterSeen& p, Watch& w, uint64_t now) {
        if (w.phase == Phase::Watching) {
            const auto list = d_.list_of ? d_.list_of(p.account, p.entity) : std::nullopt;
            if (!list && now - w.since < kRefuseNoListMs) return;
            const auto needs = refusal_needs(d_.mine ? d_.mine() : std::vector<ModEntry>{}, p_.required,
                                             list ? &*list : nullptr);
            if (list && needs.empty()) {
                w.phase = Phase::Ok;
                return;
            }
            std::string what = list ? "lacks " : "no EMF (no mod list within 60 s)";
            if (list)
                for (size_t i = 0; i < needs.size(); ++i)
                    what += (i ? ", " : "") + needs[i].id + (needs[i].version.empty() ? "" : " " + needs[i].version) +
                            " (" + (needs[i].theirs.empty() ? std::string("none") : needs[i].theirs) + ")";
            w.what = what;
            if (d_.is_admin && d_.is_admin(p.entity, p.account)) {
                w.phase = Phase::Admin;
                log(who(p) + " is an admin: not refused (" + what + ")");
                return;
            }
            w.text = refuse_text(needs, !list, p_.info);
            w.decided = now;
            w.phase = Phase::Telling;
            log("refusing " + who(p) + ": " + what + "; the kick in 5 s");
        }
        if (w.phase == Phase::Telling) {
            if (!w.told) {
                w.told = d_.tell && d_.tell(p.entity, p.account, w.text);
                if (!w.told && !w.tell_failed) {
                    w.tell_failed = true;
                    log("the line for " + who(p) + " could not be queued (tried again until the kick)");
                }
            }
            if (now - w.decided < kRefuseKickDelayMs) return;
            const bool sent = d_.kick && d_.kick(p.account);
            log(sent ? "kicked " + who(p) + " (the game's kick)" : "the kick of " + who(p) + " could not be sent");
            w.phase = Phase::Kicked;
            w.kicked = now;
            return;
        }
        if (w.phase == Phase::Kicked && now - w.kicked >= kRefuseGoneCheckMs) {
            log(who(p) + " is still here 10 s after the kick (the game did not take it)");
            w.phase = Phase::Done;
        }
    }

    ModsPolicy p_;
    Deps d_;
    mutable std::mutex m_;
    std::map<uint64_t, Watch> w_;  // by account
};

}  // namespace emf::core
