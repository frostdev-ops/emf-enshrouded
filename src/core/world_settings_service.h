#pragma once
// World settings, the entry point the console commands, the C API and Lua share: read the running world's values,
// queue changes and resets (validated), list the settings, and pass changes on to callbacks.
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "world_settings_state.h"

namespace emf::core::ws {

struct ServiceDeps {
    std::function<uint8_t*()> live;     // the server world's ecs::GameSettings, or null (no world hosted here)
    std::function<uint32_t()> session;  // the hosted session
    std::function<bool()> can_sync;     // the game's resync works (null: yes)
    std::function<bool()> can_persist;  // this world's kept settings can be saved now (null: yes)
};

class Service {
public:
    Service(State& state, ServiceDeps deps);

    bool get(std::string_view key, double* value, std::string* why) const;
    bool set(std::string_view key, double value, bool persist, std::string* why);
    bool set_text(std::string_view key, std::string_view value, bool persist, std::string* why);  // enum names too
    bool reset(std::string_view key, bool persist, std::string* why);  // "" or "all": every setting
    bool can_sync() const;  // false: players' games keep the old values of the settings they read until they reconnect

    struct Row {
        const Field* field;
        double value;  // now
        double world;  // the world's own (the session's base)
        bool changed;
    };
    std::vector<Row> rows() const;  // empty without a world
    bool snapshot(uint8_t (&out)[kStructSize]) const;  // the live struct, read guarded (false without a world)

    void on_changed(std::function<void(const std::string& key, double value)> cb);
    void notify(const State::Step& step);  // after the server step's write

private:
    bool queue_checked(const Field& f, double value, bool persist, std::string* why);
    bool may_queue(bool persist, std::string* why) const;
    State& state_;
    ServiceDeps deps_;
    std::mutex cb_m_;
    std::vector<std::function<void(const std::string&, double)>> callbacks_;
};

}  // namespace emf::core::ws
