#pragma once
// Fall, shroud, cold and drowning damage for mods (feature 4b, private research env_damage_2026-11-11): four server
// programs write Health directly (no hit, no HitEvent). EMF's pre and post hooks on them see exactly that program's
// change (every program that reads or writes Health conflicts with them, and each runs its slices one at a time), turn
// each drop into an env hit (EmfHit.kind 1-4) for the rules and the before-callbacks, put Health right when the amount
// shrank, and queue the after-events for the server step. This part is game-free; api.cpp feeds it the game.
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "core/hit_hook.h"
#include "core/hits.h"
#include "emf/emf_api.h"

namespace emf::core {

// The entities of one slice of a program's job: 16 bytes each, the EntityId first.
struct EnvSlice {
    const uint8_t* list = nullptr;
    uint64_t count = 0;
};
uint32_t env_slice_entity(const EnvSlice& s, uint64_t i);  // 0 past the end

// HealthFn's "not known". A real Health may be below 0: shroud, cold and drowning subtract without a floor.
constexpr int32_t kEnvNoHealth = INT32_MIN;

struct EnvDrop {
    uint32_t entity = 0;
    int32_t before = 0;
    int32_t after = 0;
};

// The slice's Health before the program ran (pre), and the drops after it (post), for one job context at a time
// (thread-local in the hooks: a program's slices run one after another, so pre and post never interleave).
class EnvSnapshot {
public:
    using HealthFn = int32_t (*)(uint32_t entity, void* ctx);  // kEnvNoHealth: not known
    void pre(const void* job_ctx, const EnvSlice& s, HealthFn health, void* hctx);
    std::vector<EnvDrop> post(const void* job_ctx, const EnvSlice& s, HealthFn health, void* hctx);
    bool holds(const void* job_ctx) const { return ctx_ && ctx_ == job_ctx; }
    void clear() {
        ctx_ = nullptr;
        before_.clear();
    }

private:
    const void* ctx_ = nullptr;
    std::vector<std::pair<uint32_t, int32_t>> before_;
};

// What env_apply needs from the game.
struct EnvEnv {
    EmfWorld* world = nullptr;
    void* ctx = nullptr;
    HitClassify classify = nullptr;
    bool (*add_health)(uint32_t entity, int32_t delta, void* ctx, int32_t* now) = nullptr;  // false: not done
    bool (*deadly_fog)(uint32_t entity, void* ctx) = nullptr;  // in deadly shroud (EnvironmentSensor.fogType 3)
};

// One drop as an env hit: the rules (with the carry for per-step kinds) and the before-callbacks run; Health is put
// right by the difference between the drop and the final amount. True when it is to be delivered to on_hit (a
// cancelled one is not, as a cancelled entity hit sends no HitEvent); *out is the final hit. When Health cannot be put
// right, the drop stands: a fault, delivered as it happened.
bool env_apply(uint8_t kind, const EnvDrop& d, const EnvEnv& env, const HitRules& rules, HitCallbacks& cbs,
               HitStats& st, HitCarry& carry, EmfHit* out);

// The after-events waiting for the server world's step (filled on the program's worker thread).
class EnvQueue {
public:
    static constexpr size_t kMax = 4096;
    void push(const EmfHit& h);
    std::vector<EmfHit> take();
    size_t size() const;
    uint64_t dropped() const;

private:
    mutable std::mutex m_;
    std::vector<EmfHit> q_;
    uint64_t dropped_ = 0;
};

}  // namespace emf::core
