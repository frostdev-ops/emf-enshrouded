#pragma once
#include <windows.h>

#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/perf_book.h"
#include "emf/emf_api.h"

namespace emf::core {

using ErrorSink = std::function<void(const std::string&)>;

// Runs mod callbacks right after a world's simulation step.
//  - post(): one-shot work for the next step of a world of that kind (any thread).
//  - subscribe_tick(): called after every step of every world.
// A callback that faults (SEH) is reported; a faulting tick subscriber is disabled.
class Dispatcher {
public:
    void subscribe_tick(EmfWorldCallback cb, void* user);
    void post(uint8_t kind, EmfWorldCallback cb, void* user);
    void run(EmfWorld* world, uint8_t kind, const ErrorSink& on_error);
    size_t active_subscribers() const;

    // Timing (the `perf` command): each step adds every module's time there (its tick callbacks and posted work
    // together) to `book` as "<module> (<world>)"; `slow(module, ms)` hears each single call over kSlowCallMs.
    // `clock_ms` is for tests (default: QueryPerformanceCounter in ms). Call before the first run.
    static constexpr double kSlowCallMs = 25.0;
    void set_perf(PerfBook* book, std::function<void(const std::string&, double)> slow, double (*clock_ms)() = nullptr);

private:
    struct Item {
        EmfWorldCallback cb;
        void* user;
        uint8_t kind;
        bool enabled;
        const std::string* module;  // the callback's DLL ("lua.dll") in names_, for timing (null: not timed)
    };
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<Item> queue_;
    std::vector<Item> subscribers_;
    // Module names, each kept once (a deque never moves them): items point at them, so a step copies no strings, and
    // a callback's module is looked up once (GetModuleFileNameW takes the loader lock) on the thread that posts it.
    std::deque<std::string> names_;
    std::unordered_map<const void*, const std::string*> name_of_cb_;
    const std::string* module_of(EmfWorldCallback cb);  // takes lock_ itself
    PerfBook* perf_ = nullptr;
    std::function<void(const std::string&, double)> slow_;
    double (*clock_ms_)() = nullptr;
};

// The world whose callbacks Dispatcher::run is running on this thread right now, or nullptr.
EmfWorld* dispatching_world();

// Calls cb(world, user) under an SEH guard. Returns false if it faulted.
bool guarded_world_call(EmfWorldCallback cb, EmfWorld* world, void* user);

// "server", "client" or "prediction" (EMF_WORLD_*), else "world <n>".
std::string world_kind_name(uint8_t kind);

}  // namespace emf::core
