#include "component_service.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "common/seh.h"
#include "emf/generated/components.hpp"

namespace emf::core {

namespace {

// ---- guarded engine calls (no C++ objects with destructors in these frames), as teleport.cpp's ----
enum class Push { Ok, Full, Fault };
Push push_guarded(QueuePushFn fn, uint8_t* cb, uint8_t kind, uint32_t size, uint32_t entity, const uint8_t* image) {
    __try {
        uint8_t* rec = fn(cb, cb + kCmdBufQueue, kind, size, entity);
        if (!rec) return Push::Full;
        std::memcpy(rec + kRecHashes, image + kRecHashes, size - kRecHashes);
        return Push::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Push::Fault;
    }
}

// The running job's command buffer: cmdbuf = *(*ctx + 8), with a capacity and a fill that make sense.
uint8_t* command_buffer(void* ctx) {
    uint8_t* job = nullptr;
    uint8_t* cb = nullptr;
    uint64_t cap = 0;
    uint32_t used = 0;
    if (!seh_read(ctx, &job, 8) || !job || !seh_read(job + kJobCmdBuf, &cb, 8) || !cb ||
        !seh_read(cb + kCmdBufCapacity, &cap, 8) || !seh_read(cb + kCmdBufUsed, &used, 4))
        return nullptr;
    if (cap == 0 || cap > (uint64_t(1) << 30) || used > cap) return nullptr;
    return cb;
}

void components_program(void* ctx, void* user) { static_cast<ComponentService*>(user)->run(ctx); }

std::string name_of(uint32_t hash) {
    if (const ComponentRule* r = component_rule(hash)) return r->name;
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", hash);
    return b;
}

}  // namespace

ComponentService::ComponentService(Deps deps) : deps_(std::move(deps)) {}

ProgramSpec ComponentService::spec() {
    ProgramSpec s;
    s.name = kComponentsProgramName;
    s.world_kind = EMF_WORLD_SERVER;
    s.fn = components_program;
    s.user = this;
    return s;
}

bool ComponentService::enabled() const {
    AcquireSRWLockShared(&lock_);
    const bool on = enabled_;
    ReleaseSRWLockShared(&lock_);
    return on;
}

size_t ComponentService::pending() const {
    AcquireSRWLockShared(&lock_);
    const size_t n = jobs_.size();
    ReleaseSRWLockShared(&lock_);
    return n;
}

bool ComponentService::is_player_entity(uint32_t entity) { return entity >= 1 && entity <= 1023; }

bool ComponentService::present(uint8_t* world, uint32_t entity, uint32_t hash) const {
    return get_component(deps_.get_component, world, hash, entity).ptr != nullptr;
}

bool ComponentService::exists(uint8_t* world, uint32_t entity) const {
    return present(world, entity, emf::gen::ecs::CurrentTransform::kHash);
}

void ComponentService::say(const std::string& line) {
    if (!deps_.log) return;
    AcquireSRWLockExclusive(&say_lock_);
    const std::string out = said_.pass(line, deps_.now_ms ? deps_.now_ms() : 0);
    ReleaseSRWLockExclusive(&say_lock_);
    if (!out.empty()) deps_.log(out);
}

bool ComponentService::request(const ComponentChangeRequest& r, std::string* why) {
    const auto refuse = [&](const std::string& reason) {
        if (why) *why = reason;
        say("change_component(" + name_of(r.hash) + " on " + std::to_string(r.entity) + ") refused: " + reason);
        return false;
    };
    if (!r.world || WorldView(reinterpret_cast<uint8_t*>(r.world)).kind() != EMF_WORLD_SERVER)
        return refuse("the server world only (a world this game hosts)");
    if (!r.entity) return refuse("no entity");
    if (is_player_entity(r.entity)) return refuse("never a player");
    const uint32_t cur = session();
    AcquireSRWLockExclusive(&lock_);
    if (ledger_session_ != cur) {  // a new session: what mods added before is gone with that world
        added_.clear();
        ledger_session_ = cur;
    }
    const bool added_before = added_.count({r.mod, r.entity, r.hash}) != 0;
    const std::string policy = check_change(r.op, r.hash, static_cast<uint32_t>(r.data.size()), !r.data.empty(),
                                            r.flags, added_before);
    const std::string value = r.op == ComponentOp::Add
                                  ? check_data(r.hash, r.data.empty() ? nullptr : r.data.data(),
                                               static_cast<uint32_t>(r.data.size()))
                                  : std::string();
    std::string reason;
    if (!enabled_) reason = "changing components is off (see the log)";
    else if (!policy.empty()) reason = policy;
    else if (!value.empty()) reason = value;
    else if (jobs_.size() >= kMaxComponentChanges) reason = "too many changes under way";
    else
        for (const auto& j : jobs_)
            if (j.req.entity == r.entity && j.req.hash == r.hash && j.req.world == r.world) {
                reason = "a change of that component on that entity is under way";
                break;
            }
    if (!reason.empty()) {
        ReleaseSRWLockExclusive(&lock_);
        return refuse(reason);
    }
    Job j;
    j.id = next_id_++;
    j.req = r;
    if (j.req.op == ComponentOp::Add && j.req.data.empty())
        j.req.data.assign(component_rule(r.hash)->size, 0);  // NULL data: zeros of the component's size
    j.session = cur;
    jobs_.push_back(std::move(j));
    ReleaseSRWLockExclusive(&lock_);
    return true;
}

ComponentService::Result ComponentService::answer(const Job& j, uint32_t status, const std::string& why,
                                                   bool world_gone) {
    if (status == EMF_COMPONENT_FAILED)
        say("change_component(" + name_of(j.req.hash) + " on " + std::to_string(j.req.entity) + ") failed: " + why);
    return Result{j.req.done, world_gone ? nullptr : j.req.world, status, why};  // a world that ended may be freed
}

void ComponentService::deliver(std::vector<Result>& results) {
    for (auto& r : results)
        if (r.done) r.done(r.world, r.status, r.why);
    results.clear();
}

void ComponentService::run(void* program_context) {
    const ProgramCall* call = current_program_call();
    if (!call || call->ctx != program_context) return;
    auto* world = reinterpret_cast<EmfWorld*>(call->world);
    const uint32_t cur = session();
    std::vector<Job> work;
    AcquireSRWLockExclusive(&lock_);
    if (enabled_)
        for (const auto& j : jobs_)
            if (j.phase == Phase::Push && j.req.world == world && j.session == cur) work.push_back(j);
    ReleaseSRWLockExclusive(&lock_);
    if (work.empty()) return;

    uint8_t* cb = command_buffer(program_context);
    std::vector<Result> failed;
    bool off = false;
    std::string off_why;
    if (!cb || !deps_.queue_push) {
        off = true;
        off_why = "the game's command buffer is not where EMF expects it (a game update?): changing components is off";
    } else {
        for (auto& j : work) {
            if (!exists(call->world, j.req.entity)) {  // gone since the check: never a push into its slot
                failed.push_back(answer(j, EMF_COMPONENT_FAILED, "the entity is gone"));
                j.finished = true;
                continue;
            }
            const std::vector<uint8_t> rec = j.req.op == ComponentOp::Add
                                                 ? add_component_record(j.req.hash, j.req.data.data(),
                                                                        static_cast<uint16_t>(j.req.data.size()))
                                                 : remove_component_record(j.req.hash);
            const uint8_t kind = j.req.op == ComponentOp::Add ? kRecAddComponents : kRecRemoveComponents;
            const Push p = push_guarded(deps_.queue_push, cb, kind, static_cast<uint32_t>(rec.size()), j.req.entity,
                                        rec.data());
            if (p == Push::Fault) {
                off = true;
                off_why = "QueuePush faulted: changing components is off";
                break;
            }
            if (p == Push::Full) {
                if (++j.push_waits > kComponentPushRetries) {
                    failed.push_back(answer(j, EMF_COMPONENT_FAILED, "the game's command buffer stayed full for 2 s"));
                    j.finished = true;
                }
                continue;
            }
            j.phase = Phase::Verify;
        }
    }
    AcquireSRWLockExclusive(&lock_);
    for (const auto& j : work)
        for (auto it = jobs_.begin(); it != jobs_.end(); ++it)
            if (it->id == j.id) {
                if (j.finished) jobs_.erase(it);
                else *it = j;
                break;
            }
    if (off) {
        enabled_ = false;
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (it->phase != Phase::Verify) {  // not pushed: answered now; pushed ones are still verified
                failed.push_back(answer(*it, EMF_COMPONENT_FAILED, off_why));
                it = jobs_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& f : failed) early_.push_back(std::move(f));
    ReleaseSRWLockExclusive(&lock_);
    if (off) say(off_why);
}

void ComponentService::on_server_tick(EmfWorld* world) {
    auto* raw = reinterpret_cast<uint8_t*>(world);
    const uint32_t cur = session();
    std::vector<Result> results;
    AcquireSRWLockExclusive(&lock_);
    results.swap(early_);
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        Job& j = *it;
        bool done = false;
        if (j.session != cur) {
            results.push_back(answer(j, EMF_COMPONENT_FAILED, "the world it was meant for ended", true));
            done = true;
        } else if (j.req.world == world) {
            const bool add = j.req.op == ComponentOp::Add;
            if (j.phase == Phase::Check) {
                if (!exists(raw, j.req.entity)) {
                    results.push_back(answer(j, EMF_COMPONENT_FAILED, "no such entity (or it has no position)"));
                    done = true;
                } else if (present(raw, j.req.entity, emf::gen::ecs::PlayerState::kHash)) {
                    results.push_back(answer(j, EMF_COMPONENT_FAILED, "never a player"));
                    done = true;
                } else if (present(raw, j.req.entity, j.req.hash) == add) {
                    results.push_back(answer(j, EMF_COMPONENT_ALREADY, add ? "already there" : "not there"));
                    done = true;
                } else {
                    j.phase = Phase::Push;
                }
            } else if (j.phase == Phase::Verify) {
                if (present(raw, j.req.entity, j.req.hash) == add) {
                    const auto key = std::make_tuple(j.req.mod, j.req.entity, j.req.hash);
                    if (add) added_.insert(key);
                    else added_.erase(key);
                    results.push_back(answer(j, EMF_COMPONENT_DONE, ""));
                    done = true;
                } else if (++j.verify_steps >= kComponentVerifySteps) {
                    results.push_back(answer(j, EMF_COMPONENT_FAILED, "the game did not apply it"));
                    done = true;
                }
            }
        }
        it = done ? jobs_.erase(it) : std::next(it);
    }
    ReleaseSRWLockExclusive(&lock_);
    deliver(results);
}

void ComponentService::answer_all_locked(const std::string& why, std::vector<Result>& out) {
    for (const auto& j : jobs_) out.push_back(answer(j, EMF_COMPONENT_FAILED, why, true));
    jobs_.clear();
    for (auto& r : early_) {
        r.world = nullptr;
        out.push_back(std::move(r));
    }
    early_.clear();
    added_.clear();
}

void ComponentService::world_reset() {
    std::vector<Result> results;
    AcquireSRWLockExclusive(&lock_);
    answer_all_locked("a new world was set up", results);
    ReleaseSRWLockExclusive(&lock_);
    deliver(results);
}

size_t ComponentService::end_session(const std::string& why) {
    std::vector<Result> results;
    AcquireSRWLockExclusive(&lock_);
    answer_all_locked(why, results);
    ReleaseSRWLockExclusive(&lock_);
    const size_t n = results.size();
    deliver(results);
    return n;
}

}  // namespace emf::core
