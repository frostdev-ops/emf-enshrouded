#pragma once
// World settings, the game side: on the server world's step, apply the queued changes to its ecs::GameSettings and ask
// the game to send the struct to every player (the GameServer instance's send-settings flag; the next ServerInfo carries
// it, then the game clears the flag). Also hands the world's kept overrides in and saves them when they change.
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "world_settings_state.h"

namespace emf::core::ws {

struct GlueDeps {
    uint32_t settings_ptr_off = 0;  // GameServer instance: the session's settings pointer (0: not found on this build)
    uint32_t flag_off = 0;          // GameServer instance: the send-settings flag (0: not found on this build)
    std::function<void(const std::string&)> log;
    std::function<bool(const std::string&)> save_file;  // the world's kept overrides (false: not written)
};

class Glue {
public:
    Glue(State& state, GlueDeps deps);
    // On the server world's thread after its step. `live` is the server world's ecs::GameSettings, `instance` the
    // GameServer instance of this session (may be null), `file_text` the world's kept overrides when they arrived.
    State::Step server_step(uint8_t* live, uint8_t* instance, uint32_t session, uint64_t now_ms, const std::string* file_text);
    bool can_sync() const;
    // The resync works now: the offsets are known and the last attempt found this world's struct (any thread).
    bool sync_ok() const;

private:
    State& state_;
    GlueDeps deps_;
    uint32_t warned_session_ = 0;
    std::atomic<bool> pointer_mismatch_{false};
};

}  // namespace emf::core::ws
