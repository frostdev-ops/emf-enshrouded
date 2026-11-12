// Doors (doors.h).
#include "core/doors.h"

#include <algorithm>
#include <cstring>

#include "common/offsets.h"
#include "common/seh.h"
#include "emf/generated/components.hpp"

namespace emf::core {

namespace {
namespace gen = emf::gen::ecs;

void doors_program(void* ctx, void* user) { static_cast<DoorService*>(user)->run(ctx); }

const void* comp(GetComponentFn fn, uint8_t* world, uint32_t hash, uint32_t entity, uint64_t want) {
    const ComponentRef r = fn ? get_component(fn, world, hash, entity) : ComponentRef{};
    return r.ptr && r.size == want ? r.ptr : nullptr;
}
}  // namespace

DoorService::DoorService(Deps deps) : deps_(std::move(deps)) {}

ProgramSpec DoorService::spec() {
    ProgramSpec s;
    s.name = kDoorsProgramName;
    s.world_kind = EMF_WORLD_SERVER;
    s.fn = doors_program;
    s.user = this;
    s.other_write = {"DirectionalInteraction", "InteractionOfferRuntime", "ServerToggleState"};
    return s;
}

uint32_t DoorService::state(uint8_t* world, uint32_t entity, uint8_t* side) const {
    if (side) *side = 0;
    if (!world || !entity || WorldView(world).kind() != EMF_WORLD_SERVER) return EMF_DOOR_NONE;
    const auto* t = static_cast<const gen::ServerToggleState*>(
        comp(deps_.get_component, world, gen::ServerToggleState::kHash, entity, sizeof(gen::ServerToggleState)));
    const auto* d = static_cast<const gen::DirectionalInteraction*>(comp(
        deps_.get_component, world, gen::DirectionalInteraction::kHash, entity, sizeof(gen::DirectionalInteraction)));
    if (!t || !d) return EMF_DOOR_NONE;
    bool open = false;
    uint8_t s = 0;
    if (!seh_read(&t->state, &open, 1) || !seh_read(&d->state, &s, 1)) return EMF_DOOR_NONE;
    if (side) *side = s;
    uint32_t r = open ? EMF_DOOR_OPEN : EMF_DOOR_CLOSED;
    const auto* l = static_cast<const gen::LogicGraphNodeConnected*>(comp(
        deps_.get_component, world, gen::LogicGraphNodeConnected::kHash, entity, sizeof(gen::LogicGraphNodeConnected)));
    uint32_t writer = 0;
    if (l && seh_read(&l->writerGraphId, &writer, 4) && writer) r |= EMF_DOOR_LOGIC;
    if (is_locked(entity)) r |= EMF_DOOR_LOCKED;
    return r;
}

bool DoorService::request(uint8_t* world, uint32_t entity, DoorOrderKind kind, uint8_t side, std::string* why) {
    auto refuse = [&](const char* text) {
        if (why) *why = text;
        return false;
    };
    if (!world || WorldView(world).kind() != EMF_WORLD_SERVER) return refuse("not a server world (host only)");
    if (side > 2) return refuse("side is 0 (the default: back), 1 (front) or 2 (back)");
    const uint32_t st = state(world, entity);  // (takes the lock itself: before ours)
    const bool unlocking_ours = kind == DoorOrderKind::Unlock && (st & EMF_DOOR_LOCKED);
    if (!unlocking_ours) {  // a door EMF locked can always be unlocked, even if a switch drives it now
        if (!(st & (EMF_DOOR_OPEN | EMF_DOOR_CLOSED)))
            return refuse("not a door (no ServerToggleState and DirectionalInteraction)");
        if (st & EMF_DOOR_LOGIC) return refuse("a switch drives this door (the logic system owns its state)");
    }
    AcquireSRWLockExclusive(&lock_);
    bool open = (st & EMF_DOOR_OPEN) != 0;
    for (const Order& o : orders_)  // an order still waiting decides what "already" means
        if (o.entity == entity && o.world == world && (o.kind == DoorOrderKind::Open || o.kind == DoorOrderKind::Close))
            open = o.kind == DoorOrderKind::Open;
    const char* no = nullptr;
    if (kind == DoorOrderKind::Open && open) no = "it is already open";
    else if (kind == DoorOrderKind::Close && !open) no = "it is already closed";
    else if (orders_.size() >= kMaxDoorOrders) no = "256 door orders are waiting already";
    else if (kind == DoorOrderKind::Lock && locked_.size() >= kMaxLockedDoors && !find_locked(entity))
        no = "1024 doors are locked already";
    if (!no) orders_.push_back({world, entity, kind, side});
    ReleaseSRWLockExclusive(&lock_);
    return no ? refuse(no) : true;
}

void DoorService::run(void* program_context) {
    const ProgramCall* call = current_program_call();
    if (!call || call->ctx != program_context || !call->iter) return;
    std::vector<Order> work;
    std::vector<Locked> locked;
    AcquireSRWLockExclusive(&lock_);
    work.swap(orders_);
    // locks change first, so this step's lock pass sees them
    for (const Order& o : work) {
        if (o.world != call->world) continue;
        Locked* l = find_locked(o.entity);
        if (o.kind == DoorOrderKind::Lock && !l) locked_.push_back({o.entity, -1, 0});
        if (o.kind == DoorOrderKind::Unlock && l)
            locked_.erase(std::remove_if(locked_.begin(), locked_.end(), [&](const Locked& x) { return x.entity == o.entity; }),
                          locked_.end());
    }
    locked = locked_;
    ReleaseSRWLockExclusive(&lock_);
    if (work.empty() && locked.empty()) return;

    const auto toggle_acc = program_other(*call, "ServerToggleState", true);
    const auto side_acc = program_other(*call, "DirectionalInteraction", true);
    const auto offer_acc = program_other(*call, "InteractionOfferRuntime", true);
    if (!toggle_acc || !side_acc || !offer_acc) {
        say("doors: the emf_doors program's accessors are missing in this world");
        return;
    }
    std::string problem;
    auto write = [&](const OtherAccess& a, const char* name, uint32_t entity) {
        return write_other(*call, a, name, entity, deps_.read_other, deps_.write_other, deps_.get_component, &problem)
            .ptr;
    };
    auto read = [&](const OtherAccess& a, const char* name, uint32_t entity) {
        return read_other(*call, a, name, entity, deps_.read_other, deps_.get_component, &problem).ptr;
    };
    // the time of this step (ns), for ServerToggleState.lastChange
    uint8_t* job = nullptr;
    int64_t tick = 0;
    uint32_t tick_len = 0;
    int64_t now_ns = 0;
    if (seh_read(call->ctx, &job, 8) && job && seh_read(job + off::kJobTick, &tick, 8) &&
        seh_read(job + off::kJobTickLength, &tick_len, 4) && tick > 0 && tick_len > 0)
        now_ns = tick * static_cast<int64_t>(tick_len);
    // writes the door's two states (open: swung to `side`, 0 = back)
    auto set_door = [&](uint32_t entity, bool open, uint8_t side) {
        auto* t = static_cast<gen::ServerToggleState*>(write(*toggle_acc, "ServerToggleState", entity));
        if (!t) return false;
        t->state = open;
        if (now_ns) t->lastChange.value = now_ns;
        auto* d = static_cast<gen::DirectionalInteraction*>(write(*side_acc, "DirectionalInteraction", entity));
        if (!d) return false;
        const uint8_t s = open ? (side ? side : 2) : 0;
        std::memcpy(&d->state, &s, 1);
        return true;
    };
    auto held_of = [&](uint32_t entity) -> Locked* {
        for (Locked& l : locked)
            if (l.entity == entity) return &l;
        return nullptr;
    };

    for (const Order& o : work) {
        if (o.world != call->world) continue;
        if (o.kind == DoorOrderKind::Unlock) {
            if (auto* r = static_cast<gen::InteractionOfferRuntime*>(write(*offer_acc, "InteractionOfferRuntime", o.entity)))
                r->isDisabled = false;
            continue;
        }
        if (o.kind != DoorOrderKind::Open && o.kind != DoorOrderKind::Close) continue;
        const bool open = o.kind == DoorOrderKind::Open;
        if (!set_door(o.entity, open, o.side)) continue;  // gone since it was asked
        if (Locked* l = held_of(o.entity)) {  // EMF's own order on a locked door: that is the state it holds now
            l->held = open ? 1 : 0;
            l->side = open ? (o.side ? o.side : 2) : 0;
        }
    }
    // locks: isDisabled kept set (the game clears it for its own reasons) and the held state put back (a toggle that
    // was already under way when the lock came may still flip the door); a door that is gone loses its lock
    std::vector<uint32_t> gone;
    for (Locked& l : locked) {
        if (!comp(deps_.get_component, call->world, gen::ServerToggleState::kHash, l.entity, sizeof(gen::ServerToggleState))) {
            gone.push_back(l.entity);
            continue;
        }
        if (const auto* cur =
                static_cast<const gen::InteractionOfferRuntime*>(read(*offer_acc, "InteractionOfferRuntime", l.entity))) {
            bool disabled = false;
            if (seh_read(&cur->isDisabled, &disabled, 1) && !disabled)
                if (auto* r = static_cast<gen::InteractionOfferRuntime*>(write(*offer_acc, "InteractionOfferRuntime", l.entity)))
                    r->isDisabled = true;
        }
        const auto* t = static_cast<const gen::ServerToggleState*>(read(*toggle_acc, "ServerToggleState", l.entity));
        const auto* d = static_cast<const gen::DirectionalInteraction*>(read(*side_acc, "DirectionalInteraction", l.entity));
        bool open = false;
        uint8_t side = 0;
        if (!t || !d || !seh_read(&t->state, &open, 1) || !seh_read(&d->state, &side, 1)) continue;
        if (l.held < 0) {  // the first step of the lock: what it holds
            l.held = open ? 1 : 0;
            l.side = side;
        } else if (open != (l.held == 1)) {
            set_door(l.entity, l.held == 1, l.side);
        }
    }
    if (!problem.empty()) say("doors: " + problem);
    AcquireSRWLockExclusive(&lock_);
    for (const Locked& l : locked)  // what each lock holds now (a lock removed meanwhile stays removed)
        if (Locked* live = find_locked(l.entity)) {
            live->held = l.held;
            live->side = l.side;
        }
    for (uint32_t e : gone)
        locked_.erase(std::remove_if(locked_.begin(), locked_.end(), [&](const Locked& x) { return x.entity == e; }),
                      locked_.end());
    ReleaseSRWLockExclusive(&lock_);
}

void DoorService::world_reset() {
    AcquireSRWLockExclusive(&lock_);
    orders_.clear();
    locked_.clear();
    said_.clear();
    ReleaseSRWLockExclusive(&lock_);
}

size_t DoorService::locked_count() const {
    AcquireSRWLockShared(&lock_);
    const size_t n = locked_.size();
    ReleaseSRWLockShared(&lock_);
    return n;
}

DoorService::Locked* DoorService::find_locked(uint32_t entity) {
    for (Locked& l : locked_)
        if (l.entity == entity) return &l;
    return nullptr;
}

bool DoorService::is_locked(uint32_t entity) const {
    AcquireSRWLockShared(&lock_);
    bool l = false;
    for (const Locked& x : locked_) l = l || x.entity == entity;
    ReleaseSRWLockShared(&lock_);
    return l;
}

void DoorService::say(const std::string& line) {
    AcquireSRWLockExclusive(&lock_);
    const bool fresh = said_.insert(line).second;  // each problem once per world
    ReleaseSRWLockExclusive(&lock_);
    if (fresh && deps_.log) deps_.log(line);
}

}  // namespace emf::core
