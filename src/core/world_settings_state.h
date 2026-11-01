#pragma once
// World settings: changes queued from any thread, applied on the server world's step to its ecs::GameSettings in one
// write, the session's own values kept for reset, the overrides that persist, and the pacing of the game's resync.
#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "world_settings.h"

namespace emf::core::ws {

struct Change {
    const Field* field;  // nullptr with reset: every field
    double value;        // ignored for reset
    bool reset;          // back to the session's own value
    bool persist;        // set: keep for this world (re-applied after a load); reset: forget it
};

class State {
public:
    void queue(const Change& c, uint32_t session);

    struct Step {
        bool wrote = false;  // the live struct changed
        bool sync = false;   // set the game's send-settings flag now
        std::vector<std::string> lines;  // for the log
        std::vector<std::pair<std::string, double>> changed;  // key, new value (for change callbacks)
    };
    // On the server world's thread after its step. The first call of a session keeps `live` as the base and applies the
    // persisted overrides; changes queued for another session are dropped. At most one sync per 1000 ms (carried over).
    Step on_server_step(uint8_t* live, uint32_t session, uint64_t now_ms);

    bool has_base() const;
    double base_value(const Field& f) const;
    std::map<std::string, double> persisted() const;
    void set_persisted(std::map<std::string, double> values);

private:
    struct Queued {
        Change change;
        uint32_t session;
    };
    mutable std::mutex m_;
    std::vector<Queued> queue_;
    std::map<std::string, double> persisted_;
    std::array<uint8_t, kStructSize> base_{};
    uint32_t base_session_ = 0;
    bool pending_sync_ = false;
    bool synced_once_ = false;
    uint64_t last_sync_ = 0;
};

}  // namespace emf::core::ws
