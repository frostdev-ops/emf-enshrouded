#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "core/hit_hook.h"

#include <algorithm>
#include <cstring>

#include "common/world_view.h"
#include "core/api.h"
#include "core/system_programs.h"
#include "emf/generated/components.hpp"

namespace emf::core {

namespace {

using HC = emf::gen::ecs::HitCandidate;

template <class T>
T rd(const uint8_t* p, size_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}

// SEH around one mod callback (no C++ objects here: __try cannot unwind them).
bool guarded_before(EmfHitBeforeCallback cb, EmfWorld* w, EmfHit* h, void* user) {
    __try {
        cb(w, h, user);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool guarded_after(EmfHitCallback cb, EmfWorld* w, const EmfHit* h, void* user) {
    __try {
        cb(w, h, user);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void HitStats::reset() {
    seen = 0;
    changed = 0;
    cancelled = 0;
    faults = 0;
    ns = 0;
}

bool HitCallbacks::add_before(EmfHitBeforeCallback cb, void* user, std::string module) {
    if (!cb) return false;
    AcquireSRWLockExclusive(&lock_);
    const bool ok = n_before_ < kMax;
    if (ok) {
        auto& e = before_[n_before_];
        e.cb = cb;
        e.user = user;
        e.module = std::move(module);
        e.on = true;
        ++n_before_;
    }
    ReleaseSRWLockExclusive(&lock_);
    return ok;
}

bool HitCallbacks::add_after(EmfHitCallback cb, void* user, std::string module) {
    if (!cb) return false;
    AcquireSRWLockExclusive(&lock_);
    const bool ok = n_after_ < kMax;
    if (ok) {
        auto& e = after_[n_after_];
        e.cb = cb;
        e.user = user;
        e.module = std::move(module);
        e.on = true;
        ++n_after_;
    }
    ReleaseSRWLockExclusive(&lock_);
    return ok;
}

size_t HitCallbacks::before_count() const {
    AcquireSRWLockShared(&lock_);
    const size_t n = n_before_;
    ReleaseSRWLockShared(&lock_);
    return n;
}

size_t HitCallbacks::after_count() const {
    AcquireSRWLockShared(&lock_);
    const size_t n = n_after_;
    ReleaseSRWLockShared(&lock_);
    return n;
}

void HitCallbacks::fault(const std::string& module, const char* which) {
    if (log_)
        log_(std::string("hits: ") + which + " callback of " + module +
             " faulted and is switched off (the game goes on; fix the mod and restart the game)");
}

void HitCallbacks::call_before(EmfWorld* w, EmfHit& h, HitStats& st) {
    // copy under the lock, call outside it (a callback may register another)
    size_t n = 0;
    AcquireSRWLockShared(&lock_);
    n = n_before_;
    ReleaseSRWLockShared(&lock_);
    for (size_t i = 0; i < n; ++i) {
        auto& e = before_[i];  // entries are only appended: index i is final once counted
        if (!e.on.load(std::memory_order_relaxed)) continue;
        if (!guarded_before(e.cb, w, &h, e.user)) {
            if (e.on.exchange(false)) {
                ++st.faults;
                fault(e.module, "a before-the-hit");
            }
        }
    }
}

void HitCallbacks::call_after(EmfWorld* w, const EmfHit& h) {
    size_t n = 0;
    AcquireSRWLockShared(&lock_);
    n = n_after_;
    ReleaseSRWLockShared(&lock_);
    for (size_t i = 0; i < n; ++i) {
        auto& e = after_[i];
        if (!e.on.load(std::memory_order_relaxed)) continue;
        if (!guarded_after(e.cb, w, &h, e.user) && e.on.exchange(false)) fault(e.module, "an on_hit");
    }
}

void HitCallbacks::set_log(std::function<void(const std::string&)> log) { log_ = std::move(log); }

void HitCallbacks::reset() {
    AcquireSRWLockExclusive(&lock_);
    for (auto& e : before_) {
        e.cb = nullptr;
        e.module.clear();
    }
    for (auto& e : after_) {
        e.cb = nullptr;
        e.module.clear();
    }
    n_before_ = 0;
    n_after_ = 0;
    ReleaseSRWLockExclusive(&lock_);
}

bool pre_hit(uint8_t* hc, const PreHitEnv& env, HitOnce& once, const HitRules& rules, HitCallbacks& cbs, HitStats& st) {
    if (!hc || env.world_kind != EMF_WORLD_SERVER) return false;
    if (rd<uint8_t>(hc, offsetof(HC, candidateType)) != 0) return false;  // terrain and building hits: not passed
    if (!rules.size() && !cbs.before_count()) return false;
    const uint32_t entity = rd<uint32_t>(hc, offsetof(HC, targetId));  // this level's entity (a part or a parent)
    const bool applies = !env.applies || env.applies(entity, env.ctx);
    if (!once.should_fire(hc, rd<uint32_t>(hc, offsetof(HC, impactId)), entity,
                          rd<uint32_t>(hc, offsetof(HC, propagatedFromEntityId)), applies))
        return false;
    ++st.seen;
    EmfHit h{};
    hit_from_candidate(hc, &h);
    if (env.weapon_item) h.weapon_item = env.weapon_item(rd<uint32_t>(hc, offsetof(HC, weaponPideId)), env.ctx);
    if (env.health) {
        h.health_before = env.health(entity, env.ctx);  // the entity whose health this level changes
        if (h.health_before < 0 && h.target != entity) h.health_before = env.health(h.target, env.ctx);  // a part without
    }
    h.health_after = h.health_before < 0 ? -1 : std::max(0, h.health_before - h.amount);
    const EmfHit start = h;
    rules.apply(h, env.classify, env.ctx);
    cbs.call_before(env.world, h, st);
    const bool changed = h.amount != start.amount || h.stamina != start.stamina || h.reaction != start.reaction ||
                         h.cancel != start.cancel;
    if (!changed) return false;
    hit_to_candidate(h, hc);
    ++st.changed;
    if (h.cancel) ++st.cancelled;
    return true;
}

// ---- the detour ----

namespace {

using ApplyHitFn = void (*)(bool* out_propagate, void* entity, void* ctx, uint8_t* hit);
ApplyHitFn g_o_apply = nullptr;
std::atomic<bool> g_on{false};
thread_local HitOnce t_once;

void hook_work(CoreState* st, void* ctx, uint8_t* hit) {
    void* program_context = *static_cast<void**>(ctx);  // HitConclusionCtx +0: the program context
    uint8_t* world = program_context_world(program_context);
    if (!world) return;
    PreHitEnv env;
    fill_pre_hit_env(reinterpret_cast<EmfWorld*>(world), &env);
    env.world_kind = WorldView(world).kind();
    LARGE_INTEGER t0, t1, f;
    QueryPerformanceCounter(&t0);
    pre_hit(hit, env, t_once, st->hit_rules, st->hit_callbacks, st->hit_stats);
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&f);
    if (f.QuadPart > 0)
        st->hit_stats.ns += static_cast<uint64_t>((t1.QuadPart - t0.QuadPart) * 1000000000LL / f.QuadPart);
}

bool guarded_work(CoreState* st, void* ctx, uint8_t* hit) {
    __try {
        hook_work(st, ctx, hit);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void hk_apply_hit(bool* out_propagate, void* entity, void* ctx, uint8_t* hit) {
    CoreState* st = core_state();
    const bool active = st && ctx && hit && (st->hit_rules.size() || st->hit_callbacks.before_count());
    if (active && !guarded_work(st, ctx, hit)) ++st->hit_stats.faults;
    g_o_apply(out_propagate, entity, ctx, hit);
    if (active) t_once.note_propagate(out_propagate && *out_propagate);  // the conclusion walks up only then
}

}  // namespace

bool install_hit_hook(uintptr_t fn) {
    if (!fn || g_on.load()) return g_on.load();
    const bool ok = install_emf_hook("ApplyHitToEntity (hits)", fn, reinterpret_cast<void*>(&hk_apply_hit),
                                     reinterpret_cast<void**>(&g_o_apply));
    g_on.store(ok);
    return ok;
}

bool hit_hook_on() { return g_on.load(); }

void hit_hook_new_step() { t_once.reset(); }

}  // namespace emf::core
