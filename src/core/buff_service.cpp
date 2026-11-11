#include "core/buff_service.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "common/seh.h"

namespace emf::core {

namespace {

void buffs_program(void* ctx, void* user) { static_cast<BuffService*>(user)->run(ctx); }

// Name order (the engine's iterator order for singletons and the order program_singleton_slot assumes agree then).
const char* const kSingletonsRead[] = {"BalancingRegistry", "BuffRegistry", "g38_items::ItemRegistry",
                                       "g38_skilltree::SkillTree"};
// The target's Faction, PlayerSkillsUi (food time perk), WetDurationReduction; the source's stats copied onto the buff.
const char* const kOtherRead[] = {"Faction",       "PlayerSkillsUi",   "WetDurationReduction", "ImpactHierarchy",
                                  "CritChance",    "CritDamageMod",    "DamageMod",            "DamageModAgainst",
                                  "ToolDamageMod", "ImpactItemConfig", "Strength",             "Intelligence",
                                  "Dexterity",     "Level"};
const char* const kOtherWrite[] = {"BuffContainer", "LifeTime"};
// ApplyBuffCtx 0x68..0xB8, in this order
const char* const kSourceStats[] = {"ImpactHierarchy",  "CritChance",    "CritDamageMod",    "DamageMod",
                                    "DamageModAgainst", "ToolDamageMod", "ImpactItemConfig", "Strength",
                                    "Intelligence",     "Dexterity",     "Level"};

struct CoreCall {
    ApplyBuffCoreFn fn;
    uint8_t out[8];
    uint32_t target, source;
    const void* type;
    uint8_t* ctx;
};
bool guarded_apply(CoreCall& c) {
    __try {
        c.fn(c.out, c.target, c.source, c.type, c.ctx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool guarded_cleanse(uint8_t* container, uint32_t type, bool* found) {
    __try {
        *found = buff_cleanse(container, type);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool guarded_done(EmfBuffCallback cb, EmfWorld* w, const EmfBuffEvent* ev, void* user) {
    __try {
        cb(w, ev, user);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

// the instance of `type` in `slot`'s group of a container copy: its buff entity, else 0
uint32_t buff_entity_of(const uint8_t* c, uint32_t type, uint8_t slot) {
    EmfBuff all[40];
    const uint32_t n = (std::min)(read_buffs(c, nullptr, nullptr, 0, all, 40), 40u);
    for (uint32_t i = 0; i < n; ++i)
        if (all[i].type == type && all[i].slot == slot) return all[i].entity;
    return 0;
}

EmfBuffEvent refused_event(const BuffRequest& r) {
    EmfBuffEvent e{};
    e.size = sizeof e;
    e.kind = EMF_BUFF_REFUSED;
    e.target = r.target;
    e.type = r.type;
    e.total_ns = e.remaining_ns = -1;
    return e;
}

}  // namespace

ProgramSpec BuffService::spec() {
    ProgramSpec s;
    s.name = kBuffsProgramName;
    s.world_kind = EMF_WORLD_SERVER;
    s.fn = buffs_program;
    s.user = this;
    s.events_out = {"AppliedBuffEvent", "NotifyEvent"};
    for (const char* n : kSingletonsRead) s.singletons_read.emplace_back(n);
    for (const char* n : kOtherRead) s.other_read.emplace_back(n);
    for (const char* n : kOtherWrite) s.other_write.emplace_back(n);
    return s;
}

IterLayout BuffService::layout() {
    const ProgramSpec s = spec();
    return iter_layout(s.singletons_read.size() + s.singletons_write.size(), s.other_read.size() + s.other_write.size());
}

bool BuffService::request(BuffRequest r, std::string* why) {
    auto refuse = [&](const std::string& text) {
        if (why) *why = text;
        return false;
    };
    if (!r.world) return refuse("no server world");
    if (!r.target) return refuse("entity 0");
    if (!r.type) return refuse("buff type 0");
    if (!r.remove && !deps_.apply)
        return refuse("giving buffs is not available on this game build (the ApplyBuffCore signature is not usable)");
    if (!r.remove && r.duration_ns < 0) return refuse("a negative duration");
    AcquireSRWLockExclusive(&lock_);
    const bool on = giving_on_ || r.remove;
    const bool room = pending_.size() < kMaxPendingBuffs;
    if (on && room) pending_.push_back(std::move(r));
    ReleaseSRWLockExclusive(&lock_);
    if (!on) return refuse("giving buffs is off (an earlier call into the game faulted)");
    if (!room) return refuse("64 buff requests are already waiting");
    return true;
}

size_t BuffService::pending() const {
    AcquireSRWLockShared(&lock_);
    const size_t n = pending_.size();
    ReleaseSRWLockShared(&lock_);
    return n;
}

void BuffService::say(const std::string& line) {
    const uint64_t now = deps_.now_ms ? deps_.now_ms() : 0;
    AcquireSRWLockExclusive(&say_lock_);
    const std::string out = said_.pass(line, now);
    ReleaseSRWLockExclusive(&say_lock_);
    if (!out.empty() && deps_.log) deps_.log(out);
}

// The handles from the program's iterator; the events-out slots must hold the job pointer and the registry slot must
// be the world's BuffRegistry (a shifted iterator layout would move everything).
bool BuffService::gather(const ProgramCall& call, Handles& hs, std::string& why) {
    if (!call.iter) {
        why = "no iterator (IterBegin not usable)";
        return false;
    }
    auto singleton = [&](const char* name) -> void* {
        const auto slot = program_singleton_slot(call, name);
        return slot && *slot < kProgramIterQwords ? reinterpret_cast<void*>(call.iter[*slot]) : nullptr;
    };
    auto accessor = [&](const char* name, bool write, OtherAccess* out) -> uint64_t {
        const auto a = program_other(call, name, write);
        if (!a || a->iter_slot >= kProgramIterQwords) return 0;
        if (out) *out = *a;
        return call.iter[a->iter_slot];
    };
    void* job = nullptr;
    seh_read(call.ctx, &job, 8);
    const IterLayout l = layout();
    const uint64_t ev0 = call.iter[l.events_in_base], ev1 = call.iter[l.events_in_base + 1];
    if (!job || ev0 != reinterpret_cast<uint64_t>(job) || ev1 != reinterpret_cast<uint64_t>(job)) {
        why = "the events-out slots do not hold the job pointer (iterator layout?)";
        return false;
    }
    const uint8_t* reg = static_cast<const uint8_t*>(singleton("BuffRegistry"));
    void* world_reg = call.world ? WorldView(call.world).singleton("BuffRegistry") : nullptr;
    if (!reg || (world_reg && world_reg != reg)) {
        why = "the buff registry slot does not match the world's singleton (iterator layout?)";
        return false;
    }
    if (!buff_collection(reg, &hs.coll)) {
        why = "the world's buff type list cannot be read";
        return false;
    }
    BuffHandles& h = hs.h;
    h.program_context = call.ctx;
    h.balancing = singleton("BalancingRegistry");
    h.item_registry = singleton("g38_items::ItemRegistry");
    h.skill_tree = singleton("g38_skilltree::SkillTree");
    h.container_w = accessor("BuffContainer", true, &hs.container);
    h.lifetime_w = accessor("LifeTime", true, nullptr);
    h.faction_r = accessor("Faction", false, &hs.faction);
    hs.has_faction = h.faction_r != 0;
    hs.has_skills = accessor("PlayerSkillsUi", false, &hs.skills) != 0;
    hs.has_wet = accessor("WetDurationReduction", false, &hs.wet) != 0;
    for (size_t i = 0; i < 11; ++i) h.source[i] = accessor(kSourceStats[i], false, nullptr);
    h.events = job;
    if (!h.container_w || !h.lifetime_w) {
        why = "the BuffContainer or LifeTime accessor slot is empty";
        return false;
    }
    return true;
}

BuffService::Done BuffService::one(const ProgramCall& call, const Handles& hs, const BuffRequest& r) {
    Done d;
    d.req = r;
    d.ev = refused_event(r);
    auto refused = [&](const std::string& why) {
        d.why = why;
        return d;
    };
    if (reinterpret_cast<uint8_t*>(r.world) != call.world) return refused("the world it was asked for has ended");
    const uint8_t* rec = buff_record(hs.coll, r.type);
    EmfBuffType t{};
    if (!rec || !buff_type_of(rec, &t)) return refused("unknown buff type " + hex(r.type));
    d.ev.slot = t.slot;
    std::string problem;
    auto read = [&](const OtherAccess& a, const char* name, uint32_t entity) {
        return read_other(call, a, name, entity, deps_.read_other, deps_.get_component, &problem).ptr;
    };
    alignas(8) uint8_t box[kBuffContainerSize];
    const void* live = read(hs.container, "BuffContainer", r.target);
    if (!live || !seh_read(live, box, sizeof box)) {
        if (!problem.empty()) say("buffs: " + problem);
        return refused("the target has no buff container");
    }
    if (r.remove) {
        if (!buff_has(box, r.type)) return refused("the target does not have it");
        const uint32_t be = buff_entity_of(box, r.type, t.slot);
        void* w = write_other(call, hs.container, "BuffContainer", r.target, deps_.read_other, deps_.write_other,
                              deps_.get_component, &problem)
                      .ptr;
        bool found = false;
        if (!w || !guarded_cleanse(static_cast<uint8_t*>(w), r.type, &found) || !found) {
            if (!problem.empty()) say("buffs: " + problem);
            return refused("the target's buff container could not be written");
        }
        d.ev.kind = EMF_BUFF_REMOVED;
        d.ev.buff_entity = be;
        d.ev.cleansed = 1;
        return d;
    }
    BuffApplyArgs a;
    a.duration_ns = r.duration_ns;
    a.item = r.item;
    a.with_source = r.source && r.source != r.target;
    a.target_skills_ui = hs.has_skills ? read(hs.skills, "PlayerSkillsUi", r.target) : nullptr;
    a.target_wet_reduction = hs.has_wet ? read(hs.wet, "WetDurationReduction", r.target) : nullptr;
    alignas(8) uint8_t ctx[kApplyBuffCtxSize];
    fill_apply_ctx(ctx, hs.h, a);
    CoreCall c{deps_.apply, {}, r.target, r.source ? r.source : r.target, rec, ctx};
    if (!guarded_apply(c)) {
        AcquireSRWLockExclusive(&lock_);
        giving_on_ = false;
        ReleaseSRWLockExclusive(&lock_);
        say("buffs: the game's applyBuffCore faulted: giving buffs is off until the game restarts");
        return refused("the game's buff code faulted (giving buffs is off now)");
    }
    int faction = -1;
    if (hs.has_faction)
        if (const void* f = read(hs.faction, "Faction", r.target)) {
            uint8_t v = 0;
            if (seh_read(f, &v, 1)) faction = v;
        }
    if (!seh_read(live, box, sizeof box)) return refused("the target's buff container cannot be read back");
    const BuffCheck k = check_applied(c.out[0], box, r.type, t.slot, t.faction_exclude, faction,
                                      (t.flags & EMF_BUFF_DESPAWN_ON_DEATH) != 0);
    if (!k.applied) return refused(k.why);
    d.ev.kind = EMF_BUFF_APPLIED;
    d.ev.buff_entity = buff_entity_of(box, r.type, t.slot);
    d.ev.total_ns = d.ev.remaining_ns = r.duration_ns ? r.duration_ns : t.default_ns;
    return d;
}

void BuffService::run(void* program_context) {
    const ProgramCall* call = current_program_call();
    if (!call || call->ctx != program_context) return;
    std::vector<BuffRequest> work;
    const uint64_t now = deps_.now_ms ? deps_.now_ms() : 0;
    AcquireSRWLockExclusive(&lock_);
    last_run_world_ = call->world;
    last_run_ms_ = now;
    const size_t n = (std::min)(pending_.size(), kBuffsPerTick);
    for (size_t i = 0; i < n; ++i) work.push_back(std::move(pending_[i]));
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(n));
    ReleaseSRWLockExclusive(&lock_);
    if (work.empty()) return;
    Handles hs;
    std::string why;
    const bool ready = gather(*call, hs, why);
    if (!ready) say("buffs: " + why);
    std::vector<Done> done;
    for (const auto& r : work) {
        if (ready) {
            done.push_back(one(*call, hs, r));
            continue;
        }
        Done d;
        d.req = r;
        d.ev = refused_event(r);
        d.why = why;
        done.push_back(std::move(d));
    }
    deliver(std::move(done));
}

void BuffService::world_reset() {
    std::vector<Done> done;
    AcquireSRWLockExclusive(&lock_);
    for (auto& r : pending_) {
        Done d;
        d.ev = refused_event(r);
        d.req = std::move(r);
        d.why = "a new world was set up";
        done.push_back(std::move(d));
    }
    pending_.clear();
    ReleaseSRWLockExclusive(&lock_);
    deliver(std::move(done));
}

void BuffService::on_server_tick(EmfWorld* world) {
    if (!world) return;
    const uint64_t now = deps_.now_ms ? deps_.now_ms() : 0;
    std::vector<Done> done;
    AcquireSRWLockExclusive(&lock_);
    if (world != seen_world_) {
        seen_world_ = world;
        seen_since_ms_ = now;
    }
    const bool running = last_run_world_ == static_cast<void*>(world) && now - last_run_ms_ < kBuffsWatchdogMs;
    if (!pending_.empty() && !running && now - seen_since_ms_ >= kBuffsWatchdogMs) {
        for (auto& r : pending_) {
            Done d;
            d.ev = refused_event(r);
            d.req = std::move(r);
            d.why = "the emf_buffs program is not running in this world";
            done.push_back(std::move(d));
        }
        pending_.clear();
    }
    ReleaseSRWLockExclusive(&lock_);
    if (done.empty()) return;
    say("buffs: the emf_buffs program is not running in this world (left out at world setup or faulted, see the log); "
        "waiting requests are refused");
    deliver(std::move(done));
}

void BuffService::deliver(std::vector<Done> done) {
    if (done.empty() || !deps_.after_step) return;
    auto log = deps_.log;
    deps_.after_step([done = std::move(done), log](EmfWorld* w) {
        for (const auto& d : done) {
            EmfBuffEvent ev = d.ev;
            ev.why = d.ev.kind == EMF_BUFF_REFUSED ? d.why.c_str() : nullptr;
            if (d.req.done && !guarded_done(d.req.done, w, &ev, d.req.user) && log) log("buffs: a callback faulted");
            if (d.req.then) d.req.then(w, ev);
        }
    });
}

}  // namespace emf::core
