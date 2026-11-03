#pragma once
// Adding and removing components (EMF API v23): the emf_components program pushes the engine's "add components"
// (kind 3) or "remove components" (kind 4) record with the game's QueuePush, the way the shipped teleport pushes its
// record (teleport.h). A request lives through four moments:
//   request()          any thread: the policy (components_api.h), the world kind, a player, one change per
//                      (entity, component) at a time, room.
//   on_server_tick()   after the next server step, on its thread: the entity is there (and no player); a component
//                      already there (add) or not there (remove) answers ALREADY without a push.
//   run()              emf_components, inside the next step: the entity is still there, then QueuePush (a full
//                      buffer: again next step, FAILED after 120).
//   on_server_tick()   after that step: the component there (add) or gone (remove) answers DONE, else FAILED after
//                      a few steps. The callback runs on the server world's thread.
// A new world, or back to the menu, answers everything under way FAILED. Per mod and session EMF remembers what it
// added (a mod may remove what it added).
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#define NOMINMAX
#include <windows.h>

#include "common/log_limiter.h"
#include "common/world_view.h"
#include "components_api.h"
#include "emf/emf_api.h"
#include "system_programs.h"
#include "teleport.h"  // QueuePushFn and the command buffer layout

namespace emf::core {

constexpr const char* kComponentsProgramName = "emf_components";
constexpr size_t kMaxComponentChanges = 256;  // queued at once
constexpr int kComponentPushRetries = 120;    // steps a full command buffer is waited for
constexpr int kComponentVerifySteps = 3;      // steps after the push the change may take to show

struct ComponentChangeRequest {
    EmfWorld* world = nullptr;
    uint32_t entity = 0;
    uint32_t hash = 0;
    ComponentOp op = ComponentOp::Add;
    std::vector<uint8_t> data;  // add: the component's bytes (empty: zeros)
    uint32_t flags = 0;         // EMF_COMPONENT_PERSISTENT
    std::string mod;            // who asks (what it added this session, it may remove)
    // EMF_COMPONENT_DONE / _ALREADY / _FAILED and why (FAILED); on the server world's thread
    std::function<void(EmfWorld* world, uint32_t status, const std::string& why)> done;
};

class ComponentService {
public:
    struct Deps {
        QueuePushFn queue_push = nullptr;  // required
        GetComponentFn get_component = nullptr;
        std::function<void(const std::string&)> log;
        std::function<uint64_t()> now_ms;
        std::function<uint32_t()> session;  // the hosted session number; a request belongs to one
    };
    explicit ComponentService(Deps deps);
    ComponentService(const ComponentService&) = delete;
    ComponentService& operator=(const ComponentService&) = delete;

    // The emf_components program (server world, nothing declared: it only pushes records).
    ProgramSpec spec();
    // Queues a change. False and *why when refused at once (the request's callback is not called then).
    bool request(const ComponentChangeRequest& r, std::string* why);
    // The program callback (inside the program call).
    void run(void* program_context);
    // After every step of a server world, on its thread (the checks, the verify, the callbacks).
    void on_server_tick(EmfWorld* world);
    // A new server world is being set up: everything under way is answered FAILED now.
    void world_reset();
    // Back to the menu: everything under way is answered FAILED with `why`. Returns how many.
    size_t end_session(const std::string& why);
    bool enabled() const;
    size_t pending() const;

private:
    enum class Phase { Check, Push, Verify };
    struct Job {
        uint32_t id = 0;
        ComponentChangeRequest req;
        uint32_t session = 0;
        Phase phase = Phase::Check;
        int push_waits = 0;
        int verify_steps = 0;
        bool finished = false;
    };
    struct Result {
        std::function<void(EmfWorld*, uint32_t, const std::string&)> done;
        EmfWorld* world = nullptr;
        uint32_t status = EMF_COMPONENT_FAILED;
        std::string why;
    };
    bool present(uint8_t* world, uint32_t entity, uint32_t hash) const;
    bool exists(uint8_t* world, uint32_t entity) const;
    static bool is_player_entity(uint32_t entity);
    Result answer(const Job& j, uint32_t status, const std::string& why);
    void deliver(std::vector<Result>& results);
    void say(const std::string& line);
    uint32_t session() const { return deps_.session ? deps_.session() : 0; }
    void answer_all_locked(const std::string& why, std::vector<Result>& out);

    Deps deps_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<Job> jobs_;
    std::vector<Result> early_;  // answered inside the program (delivered after the step)
    std::set<std::tuple<std::string, uint32_t, uint32_t>> added_;  // (mod, entity, hash) added this session
    uint32_t ledger_session_ = 0;
    uint32_t next_id_ = 1;
    bool enabled_ = true;
    SRWLOCK say_lock_ = SRWLOCK_INIT;
    LogLimiter said_;
};

}  // namespace emf::core
