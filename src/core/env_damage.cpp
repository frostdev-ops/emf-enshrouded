#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "core/env_damage.h"

#include <algorithm>
#include <cstring>

namespace emf::core {

uint32_t env_slice_entity(const EnvSlice& s, uint64_t i) {
    if (!s.list || i >= s.count) return 0;
    uint32_t id = 0;
    std::memcpy(&id, s.list + i * 16, sizeof id);
    return id;
}

void EnvSnapshot::pre(const void* job_ctx, const EnvSlice& s, HealthFn health, void* hctx) {
    ctx_ = job_ctx;
    before_.clear();
    for (uint64_t i = 0; i < s.count; ++i) {
        const uint32_t e = env_slice_entity(s, i);
        if (!e) continue;
        const int32_t h = health ? health(e, hctx) : kEnvNoHealth;
        if (h != kEnvNoHealth) before_.emplace_back(e, h);  // an entity without known health cannot be followed
    }
}

std::vector<EnvDrop> EnvSnapshot::post(const void* job_ctx, const EnvSlice&, HealthFn health, void* hctx) {
    std::vector<EnvDrop> drops;
    if (job_ctx != ctx_ || !health) return drops;
    for (const auto& [e, before] : before_) {
        const int32_t after = health(e, hctx);
        if (after != kEnvNoHealth && after < before) drops.push_back(EnvDrop{e, before, after});
    }
    ctx_ = nullptr;
    before_.clear();
    return drops;
}

bool env_apply(uint8_t kind, const EnvDrop& d, const EnvEnv& env, const HitRules& rules, HitCallbacks& cbs,
               HitStats& st, HitCarry& carry, EmfHit* out) {
    EmfHit h{};
    h.size = sizeof h;
    h.kind = kind;
    h.target = d.entity;
    h.part = d.entity;
    h.amount = d.before - d.after;
    h.health_before = d.before;
    h.health_after = std::max(0, d.after);
    // instant: the game's own signals (deadly shroud is fog type 3; only players drown tick by tick, a creature out of
    // air gets Health 0 at once)
    if (d.after <= 0 && kind == EMF_HIT_KIND_SHROUD && env.deadly_fog && env.deadly_fog(d.entity, env.ctx))
        h.flags |= EMF_HIT_INSTANT;
    if (d.after <= 0 && kind == EMF_HIT_KIND_DROWNING && env.classify &&
        env.classify(d.entity, env.ctx) != HitClass::Player)
        h.flags |= EMF_HIT_INSTANT;
    ++st.seen;
    const int32_t dropped = h.amount;
    rules.apply(h, env.classify, env.ctx, &carry);
    cbs.call_before(env.world, h, st);
    const int32_t final_amount = h.cancel ? 0 : h.amount;
    const int32_t restore = dropped - final_amount;
    if (restore != 0) {
        int32_t now = 0;
        if (!env.add_health || !env.add_health(d.entity, restore, env.ctx, &now)) {
            ++st.faults;  // Health could not be put right: the drop stands, delivered as it happened
            h.cancel = 0;
            h.amount = dropped;
            h.health_after = std::max(0, d.after);
            if (h.amount > 0 && d.after <= 0) h.flags |= EMF_HIT_KILLED;
            if (out) *out = h;
            return true;
        }
        h.health_after = std::max(0, now);
        ++st.changed;
    }
    h.amount = final_amount;
    if (!h.cancel && h.amount > 0 && h.health_after <= 0) h.flags |= EMF_HIT_KILLED;
    if (h.cancel) ++st.cancelled;
    if (out) *out = h;
    return !h.cancel;
}

void EnvQueue::push(const EmfHit& h) {
    std::lock_guard<std::mutex> l(m_);
    if (q_.size() >= kMax) {
        ++dropped_;
        return;
    }
    q_.push_back(h);
}

std::vector<EmfHit> EnvQueue::take() {
    std::lock_guard<std::mutex> l(m_);
    std::vector<EmfHit> out;
    out.swap(q_);
    return out;
}

size_t EnvQueue::size() const {
    std::lock_guard<std::mutex> l(m_);
    return q_.size();
}

uint64_t EnvQueue::dropped() const {
    std::lock_guard<std::mutex> l(m_);
    return dropped_;
}

}  // namespace emf::core
