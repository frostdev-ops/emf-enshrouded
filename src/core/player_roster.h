#pragma once
// Who joined, left or died on the server world (API v28 on_player): fed each server step with the players there, it
// answers the changes. Pure: the core reads the world (core.cpp) and hands the events to the mods (api.cpp).
//   joined  in the list with a known account (not 0) and ready for gameplay (the host's own player at once); once per
//           arrival. Nothing before the roster is armed (1 s after a world's load event: the mods' world data first);
//           then everyone there joins, the host first, then by entity id.
//   left    that account's entity is no longer there (with the last name seen); back as another entity: left, then
//           joined. A new world (reset) forgets everyone without a "left".
//   died    a joined player's death; the killer is the root source of the last hit on them within 10 s (not
//           themselves), else 0.
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

struct RosterSeen {
    uint32_t entity = 0;
    uint64_t account = 0;  // 0: not known yet
    bool ready = false;    // ready for gameplay
    bool host = false;     // the host's own player
    std::string name;      // the chat name, "" where not known
};

struct RosterEvent {
    uint32_t kind = 0;  // EMF_PLAYER_JOINED / LEFT / DIED
    uint32_t entity = 0;
    uint64_t account = 0;
    bool host = false;
    std::string name;
    uint32_t killer = 0;  // died only
};

constexpr uint64_t kRosterKillerMs = 10000;

class PlayerRoster {
public:
    void reset(uint64_t armed_at_ms) {
        armed_at_ = armed_at_ms;
        known_.clear();
        hits_.clear();
    }

    std::vector<RosterEvent> update(const std::vector<RosterSeen>& seen, uint64_t now_ms) {
        std::vector<RosterEvent> out;
        if (now_ms < armed_at_) return out;
        std::vector<const RosterSeen*> by_entity;
        for (const auto& s : seen) by_entity.push_back(&s);
        std::sort(by_entity.begin(), by_entity.end(), [](const RosterSeen* a, const RosterSeen* b) {
            return a->host != b->host ? a->host : a->entity < b->entity;
        });
        // left: a known account whose entity is not there any more (or is another one now)
        std::vector<uint64_t> gone;
        for (const auto& [account, k] : known_) {
            bool here = false;
            for (const RosterSeen* s : by_entity) here |= s->account == account && s->entity == k.entity;
            if (!here) gone.push_back(account);
        }
        std::sort(gone.begin(), gone.end(), [&](uint64_t a, uint64_t b) { return known_[a].entity < known_[b].entity; });
        for (uint64_t account : gone) {
            const Known k = known_[account];
            known_.erase(account);
            hits_.erase(k.entity);
            out.push_back({EMF_PLAYER_LEFT, k.entity, account, k.host, k.name, 0});
        }
        // joined: ready, account known, not known yet
        for (const RosterSeen* s : by_entity) {
            if (s->account == 0 || !s->ready || known_.count(s->account)) continue;
            known_[s->account] = Known{s->entity, s->host, s->name};
            out.push_back({EMF_PLAYER_JOINED, s->entity, s->account, s->host, s->name, 0});
        }
        // a name that became known later is kept for "left"
        for (const RosterSeen* s : by_entity) {
            auto it = known_.find(s->account);
            if (it != known_.end() && it->second.entity == s->entity && !s->name.empty()) it->second.name = s->name;
        }
        return out;
    }

    // The players joined now, as joined events (the host first, then by entity id): for a mod that starts watching later.
    std::vector<RosterEvent> present() const {
        std::vector<RosterEvent> out;
        for (const auto& [account, k] : known_) out.push_back({EMF_PLAYER_JOINED, k.entity, account, k.host, k.name, 0});
        std::sort(out.begin(), out.end(), [](const RosterEvent& a, const RosterEvent& b) {
            return a.host != b.host ? a.host : a.entity < b.entity;
        });
        return out;
    }

    void hit(uint32_t target, uint32_t source, uint64_t now_ms) {
        if (source == 0 || source == target) {
            hits_.erase(target);  // its own (a fall) or no source: the last hitter no longer counts
            return;
        }
        hits_[target] = Hit{source, now_ms};
    }

    std::optional<RosterEvent> died(uint32_t entity, uint64_t now_ms) {
        for (const auto& [account, k] : known_) {
            if (k.entity != entity) continue;
            uint32_t killer = 0;
            auto h = hits_.find(entity);
            if (h != hits_.end() && now_ms >= h->second.at && now_ms - h->second.at <= kRosterKillerMs) killer = h->second.source;
            hits_.erase(entity);
            return RosterEvent{EMF_PLAYER_DIED, entity, account, k.host, k.name, killer};
        }
        return std::nullopt;
    }

private:
    struct Known {
        uint32_t entity = 0;
        bool host = false;
        std::string name;
    };
    struct Hit {
        uint32_t source = 0;
        uint64_t at = 0;
    };
    uint64_t armed_at_ = UINT64_MAX;  // not armed until the first reset
    std::map<uint64_t, Known> known_;
    std::map<uint32_t, Hit> hits_;
};

}  // namespace emf::core
