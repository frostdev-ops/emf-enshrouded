#include "world_settings_glue.h"

#include <windows.h>

#include <cstring>
#include <vector>

#include "common/seh.h"
#include "world_settings_file.h"

namespace emf::core::ws {
namespace {

bool write_flag(uint8_t* at) {
    __try {
        *at = 1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

Glue::Glue(State& state, GlueDeps deps) : state_(state), deps_(std::move(deps)) {}

bool Glue::can_sync() const { return deps_.settings_ptr_off != 0 && deps_.flag_off != 0; }

State::Step Glue::server_step(uint8_t* live, uint8_t* instance, uint32_t session, uint64_t now_ms,
                              const std::string* file_text) {
    const auto log = [&](const std::string& l) {
        if (deps_.log) deps_.log(l);
    };
    if (file_text) {  // the world's kept overrides: kept from now on, and applied (they may come after the base was taken)
        std::vector<std::string> skipped;
        const auto kept = parse_overrides(*file_text, &skipped);
        for (const auto& s : skipped) log("world settings: a kept setting was left out: " + s);
        state_.set_persisted(kept);
        for (const auto& [key, value] : kept)
            if (const Field* f = find(key)) state_.queue({f, value, false, false}, session);
    }
    const auto before = state_.persisted();
    State::Step step = state_.on_server_step(live, session, now_ms);
    for (const auto& l : step.lines) log(l);
    if (step.sync) {
        void* settings = nullptr;
        const bool ours = can_sync() && instance && seh_read(instance + deps_.settings_ptr_off, &settings, sizeof settings) &&
                          settings == live;
        if (ours && write_flag(instance + deps_.flag_off)) {
            // the next ServerInfo carries the struct to every player, the host's own game included
        } else if (warned_session_ != session) {
            warned_session_ = session;
            log(std::string("world settings: ") +
                (can_sync() ? "the game's settings pointer is not this world's" : "the game's resync is not known on this build") +
                ": players' games keep the old values until they reconnect (the server uses the new ones)");
        }
    }
    const auto after = state_.persisted();
    if (after != before && deps_.save_file && !deps_.save_file(overrides_text(after)))
        log("world settings: the kept settings could not be saved for this world");
    return step;
}

}  // namespace emf::core::ws
