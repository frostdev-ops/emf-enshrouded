#pragma once
// Giving and removing buffs (API v32, private research buffs_2026-11-11 section 2): an internal server program
// "emf_buffs" with the access the game's buff programs have (other-write BuffContainer and LifeTime, the buff and
// balancing registries, AppliedBuffEvent and NotifyEvent out) runs queued requests inside the step: a give calls the
// game's own applyBuffCore with a 0xE8-byte ApplyBuffCtx from its iterator, then reads the slot back (the core says
// "ok" without applying for an excluded faction, a dead target and a resimulation); a remove sets the slot's cleansed
// byte through the write accessor (the game's cleanse: update_buffcontainer ends it the next step). The scheduler
// serializes the program with every game program that touches BuffContainer. Results go back after the step.
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "common/log_limiter.h"
#include "common/world_view.h"
#include "core/buffs.h"
#include "emf/emf_api.h"
#include "program_access.h"
#include "system_programs.h"

namespace emf::core {

constexpr const char* kBuffsProgramName = "emf_buffs";
constexpr size_t kMaxPendingBuffs = 64;
constexpr size_t kBuffsPerTick = 16;
constexpr uint64_t kBuffsWatchdogMs = 2000;

// applyBuffCore(u8 out[8], target, source, const BuffType*, ApplyBuffCtx*) (Sig::ApplyBuffCore); out[0] = error.
using ApplyBuffCoreFn = uint8_t* (*)(uint8_t* out, uint32_t target, uint32_t source, const void* type, void* ctx);

struct BuffRequest {
    EmfWorld* world = nullptr;  // the server world the caller got
    bool remove = false;
    uint32_t target = 0, type = 0;
    int64_t duration_ns = 0;  // give: 0 = the type's default
    uint32_t source = 0;      // give: 0 = the target
    uint32_t item = 0;        // give: 0 = none
    EmfBuffCallback done = nullptr;
    void* user = nullptr;
    std::function<void(EmfWorld*, const EmfBuffEvent&)> then;  // EMF's own callers (console, Lua)
};

class BuffService {
public:
    struct Deps {
        ApplyBuffCoreFn apply = nullptr;  // null: giving is refused (a game build without the signature)
        ReadOtherFn read_other = nullptr;
        WriteOtherFn write_other = nullptr;
        GetComponentFn get_component = nullptr;
        std::function<void(const std::string&)> log;
        // Runs fn after the server world's next step, on its thread, with that world.
        std::function<void(std::function<void(EmfWorld*)>)> after_step;
        std::function<uint64_t()> now_ms;
    };
    explicit BuffService(Deps deps) : deps_(std::move(deps)) {}
    BuffService(const BuffService&) = delete;
    BuffService& operator=(const BuffService&) = delete;

    ProgramSpec spec();
    IterLayout layout();
    // Queues a request. False and *why when refused at once.
    bool request(BuffRequest r, std::string* why);
    // The program callback (inside the program call).
    void run(void* program_context);
    // A new server world is being set up: what waits is refused.
    void world_reset();
    // After every server step: requests waiting while emf_buffs has not run in this world for 2 s are refused.
    void on_server_tick(EmfWorld* world);
    bool giving_available() const { return deps_.apply != nullptr; }
    size_t pending() const;

private:
    struct Handles {
        BuffHandles h;
        BuffCollection coll;
        OtherAccess container{}, faction{}, skills{}, wet{};
        bool has_faction = false, has_skills = false, has_wet = false;
    };
    struct Done {
        BuffRequest req;
        EmfBuffEvent ev{};
        std::string why;
    };
    bool gather(const ProgramCall& call, Handles& hs, std::string& why);
    Done one(const ProgramCall& call, const Handles& hs, const BuffRequest& r);
    void deliver(std::vector<Done> done);
    void say(const std::string& line);

    Deps deps_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<BuffRequest> pending_;
    bool giving_on_ = true;  // false after a fault in applyBuffCore (the session)
    SRWLOCK say_lock_ = SRWLOCK_INIT;
    LogLimiter said_;
    void* last_run_world_ = nullptr;
    uint64_t last_run_ms_ = 0;
    EmfWorld* seen_world_ = nullptr;
    uint64_t seen_since_ms_ = 0;
};

}  // namespace emf::core
