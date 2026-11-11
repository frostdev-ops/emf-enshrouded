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
        const int32_t h = health ? health(e, hctx) : -1;
        if (h >= 0) before_.emplace_back(e, h);  // an entity without known health cannot be followed
    }
}

std::vector<EnvDrop> EnvSnapshot::post(const void* job_ctx, const EnvSlice&, HealthFn health, void* hctx) {
    std::vector<EnvDrop> drops;
    if (job_ctx != ctx_ || !health) return drops;
    for (const auto& [e, before] : before_) {
        const int32_t after = health(e, hctx);
        if (after != -1 && after < before) drops.push_back(EnvDrop{e, before, after});
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
    if ((kind == EMF_HIT_KIND_SHROUD || kind == EMF_HIT_KIND_DROWNING) && d.after <= 0 && env.max_health) {
        const int32_t max = env.max_health(d.entity, env.ctx);
        if (max > 0 && static_cast<int64_t>(h.amount) * 4 >= max) h.flags |= EMF_HIT_INSTANT;  // not a per-step tick
    }
    ++st.seen;
    const int32_t dropped = h.amount;
    rules.apply(h, env.classify, env.ctx, &carry);
    cbs.call_before(env.world, h, st);
    const int32_t final_amount = h.cancel ? 0 : h.amount;
    const int32_t restore = dropped - final_amount;
    if (restore != 0 && env.add_health) {
        const int32_t now = env.add_health(d.entity, restore, env.ctx);
        h.health_after = now >= 0 ? now : std::max(0, d.before - final_amount);
        ++st.changed;
    } else if (restore != 0) {
        h.health_after = std::max(0, d.before - final_amount);
    }
    h.amount = final_amount;
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
