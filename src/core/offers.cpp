#include "offers.h"

#include <algorithm>
#include <cstring>

#include "emf/generated/components.hpp"

namespace emf::core {

using Ior = emf::gen::ecs::InteractionOfferRuntime;

uint32_t custom_verb_id(std::string_view text) {
    uint32_t h = 2166136261u;  // FNV-1a-32, as the game hashes its own ids
    for (const char c : std::string_view("emf.verb:")) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    for (const char c : text) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h;
}

namespace {
SRWLOCK g_verb_lock = SRWLOCK_INIT;
std::map<uint32_t, std::string>& verb_texts() {
    static std::map<uint32_t, std::string> m;
    return m;
}
}  // namespace

void register_verb_text(const std::string& text) {
    if (text.empty()) return;
    AcquireSRWLockExclusive(&g_verb_lock);
    if (verb_texts().size() < 4096) verb_texts()[custom_verb_id(text)] = text;
    ReleaseSRWLockExclusive(&g_verb_lock);
}

std::optional<std::string> verb_text(uint32_t id) {
    AcquireSRWLockShared(&g_verb_lock);
    const auto it = verb_texts().find(id);
    std::optional<std::string> out;
    if (it != verb_texts().end()) out = it->second;
    ReleaseSRWLockShared(&g_verb_lock);
    return out;
}

size_t verb_text_count() {
    AcquireSRWLockShared(&g_verb_lock);
    const size_t n = verb_texts().size();
    ReleaseSRWLockShared(&g_verb_lock);
    return n;
}

uint32_t offer_verb(const OfferSpec& s) {
    if (!s.text.empty()) return custom_verb_id(s.text);
    return s.verb ? s.verb : kOfferDefaultVerb;
}

std::vector<uint8_t> offer_runtime_bytes(const OfferSpec& s) {
    std::vector<uint8_t> b(Ior::kSize, 0);
    const uint32_t sequence = s.sequence ? s.sequence : kOfferDefaultSequence;
    const uint32_t verb = offer_verb(s);
    std::memcpy(b.data() + offsetof(Ior, defaultSequence), &sequence, 4);
    std::memcpy(b.data() + offsetof(Ior, defaultVerbId), &verb, 4);
    b[offsetof(Ior, reduceInteractionWeight)] = (s.flags & kOfferLowPriority) ? 1 : 0;
    b[offsetof(Ior, allowedAsBuildingSpark)] = (s.flags & kOfferBuildMode) ? 1 : 0;
    return b;  // isOffered and offerId stay 0: interaction_offer sets them in the next step
}

std::vector<uint8_t> offer_client_bytes() {
    return std::vector<uint8_t>(emf::gen::ecs::ClientInteractionOffer::kSize, 0);
}

std::vector<uint8_t> offer_range_bytes(const OfferSpec& s) {
    return std::vector<uint8_t>{s.range_m ? s.range_m : kOfferDefaultRange};
}

bool OfferRegistry::add(uint32_t session, uint32_t entity, const std::string& owner, OfferSpec spec, OfferCallback cb) {
    for (auto& e : entries_)
        if (e.session == session && e.entity == entity) {
            if (e.owner != owner) return false;
            e.spec = std::move(spec);
            e.cb = std::move(cb);
            return true;
        }
    entries_.push_back(Entry{session, entity, owner, std::move(spec), std::move(cb), 0});
    return true;
}

bool OfferRegistry::remove(uint32_t session, uint32_t entity, const std::string& owner) {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) {
        return e.session == session && e.entity == entity && e.owner == owner;
    });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
}

std::optional<OfferCallback> OfferRegistry::accept(uint32_t session, uint32_t host) {
    for (auto& e : entries_)
        if (e.session == session && e.entity == host) {
            ++e.accepts;
            return e.cb;
        }
    return std::nullopt;
}

const OfferRegistry::Entry* OfferRegistry::find(uint32_t session, uint32_t entity) const {
    for (const auto& e : entries_)
        if (e.session == session && e.entity == entity) return &e;
    return nullptr;
}

std::string OfferRegistry::owner(uint32_t session, uint32_t entity) const {
    const Entry* e = find(session, entity);
    return e ? e->owner : std::string();
}

std::vector<uint32_t> OfferRegistry::entities(uint32_t session) const {
    std::vector<uint32_t> out;
    for (const auto& e : entries_)
        if (e.session == session) out.push_back(e.entity);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<uint32_t> OfferRegistry::remove_owner(const std::string& owner) {
    std::vector<uint32_t> out;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->owner == owner) {
            out.push_back(it->entity);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void OfferRegistry::new_session(uint32_t session) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.session != session; }),
                   entries_.end());
}

// ---- OfferService ----

namespace {
namespace ecs = emf::gen::ecs;
bool is_player_entity(uint32_t entity) { return entity >= 1 && entity <= 1023; }
}  // namespace

OfferService::OfferService(Deps deps) : deps_(std::move(deps)) {}

void OfferService::say(const std::string& line) {
    if (deps_.log) deps_.log(line);
}

ComponentChangeRequest OfferService::change_of(EmfWorld* w, uint32_t session, uint32_t entity, uint32_t hash,
                                               ComponentOp op, std::vector<uint8_t> bytes, bool first) {
    ComponentChangeRequest r;
    r.world = w;
    r.entity = entity;
    r.hash = hash;
    r.op = op;
    r.data = std::move(bytes);
    r.mod = "emf.offers";
    r.internal = true;
    r.done = [this, session, entity, hash, op, first](EmfWorld*, uint32_t status, const std::string& why) {
        AcquireSRWLockExclusive(&lock_);
        auto st = state_.find({session, entity});
        if (st != state_.end() && st->second.pending > 0) --st->second.pending;
        // the offer itself did not land: the entity keeps no offer of EMF's (a game offer that appeared meanwhile, a
        // gone entity); a re-add that finds one there is fine
        const bool lost = op == ComponentOp::Add && hash == ecs::InteractionOfferRuntime::kHash &&
                          (status == EMF_COMPONENT_FAILED || (first && status == EMF_COMPONENT_ALREADY));
        std::string owner;
        if (lost) {
            owner = registry_.owner(session, entity);
            if (!owner.empty()) registry_.remove(session, entity, owner);
            state_.erase({session, entity});
        }
        ReleaseSRWLockExclusive(&lock_);
        if (lost && !owner.empty())
            say("offer on entity " + std::to_string(entity) + " (" + owner + ") did not land: " +
                (status == EMF_COMPONENT_ALREADY ? std::string("it has an offer of the game's") : why));
    };
    return r;
}

std::vector<ComponentChangeRequest> OfferService::adds_for(EmfWorld* w, uint32_t session, uint32_t entity,
                                                           const OfferSpec& spec, bool first, bool* range_added) {
    std::vector<ComponentChangeRequest> out;
    const auto has = [&](uint32_t hash) { return deps_.has && deps_.has(w, hash, entity); };
    if (!has(ecs::InteractionOfferRuntime::kHash))
        out.push_back(change_of(w, session, entity, ecs::InteractionOfferRuntime::kHash, ComponentOp::Add,
                                offer_runtime_bytes(spec), first));
    if (!has(ecs::ClientInteractionOffer::kHash))
        out.push_back(change_of(w, session, entity, ecs::ClientInteractionOffer::kHash, ComponentOp::Add,
                                offer_client_bytes(), first));
    if (!has(ecs::CheckPlayerInRange::kHash)) {
        out.push_back(change_of(w, session, entity, ecs::CheckPlayerInRange::kHash, ComponentOp::Add,
                                offer_range_bytes(spec), first));
        if (range_added) *range_added = true;
    }
    return out;
}

void OfferService::submit(std::vector<ComponentChangeRequest>& reqs) {
    for (auto& r : reqs) {
        std::string why;
        if (!deps_.change || !deps_.change(r, &why)) {
            if (r.done) r.done(r.world, EMF_COMPONENT_FAILED, why.empty() ? "changing components is off" : why);
        }
    }
    reqs.clear();
}

uint32_t OfferService::spawn_offer(EmfWorld* w, const uint8_t guid[16], double x, double y, double z,
                                   const OfferSpec& spec, const std::string& owner, OfferCallback cb, std::string* why) {
    if (!deps_.spawn) {
        if (why) *why = "spawning is off on this game build";
        return 0;
    }
    std::vector<OfferComponent> comps = {{ecs::InteractionOfferRuntime::kHash, offer_runtime_bytes(spec)},
                                         {ecs::ClientInteractionOffer::kHash, offer_client_bytes()},
                                         {ecs::CheckPlayerInRange::kHash, offer_range_bytes(spec)}};
    const uint32_t id = deps_.spawn(w, guid, x, y, z, comps, why);
    if (!id) return 0;
    const uint32_t cur = session();
    AcquireSRWLockExclusive(&lock_);
    registry_.add(cur, id, owner, spec, std::move(cb));
    State st;
    st.range_added = true;
    state_[{cur, id}] = st;
    ReleaseSRWLockExclusive(&lock_);
    return id;
}

bool OfferService::offer(EmfWorld* w, uint32_t entity, const OfferSpec& spec, const std::string& owner,
                         OfferCallback cb, std::string* why) {
    const auto refuse = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!entity) return refuse("no entity");
    if (is_player_entity(entity)) return refuse("never a player");
    const auto has = [&](uint32_t hash) { return deps_.has && deps_.has(w, hash, entity); };
    if (!has(ecs::CurrentTransform::kHash)) return refuse("no such entity (or it has no position)");
    const uint32_t cur = session();
    std::vector<ComponentChangeRequest> reqs;
    AcquireSRWLockExclusive(&lock_);
    if (const OfferRegistry::Entry* e = registry_.find(cur, entity)) {
        if (e->owner != owner) {
            ReleaseSRWLockExclusive(&lock_);
            return refuse("another mod's offer is on it (" + e->owner + ")");
        }
        if (offer_runtime_bytes(e->spec) != offer_runtime_bytes(spec)) {
            ReleaseSRWLockExclusive(&lock_);
            return refuse("its offer is there already: remove it first, then offer again");
        }
        registry_.add(cur, entity, owner, spec, std::move(cb));  // the same offer: a new callback
        ReleaseSRWLockExclusive(&lock_);
        return true;
    }
    if (has(ecs::InteractionOfferRuntime::kHash)) {
        ReleaseSRWLockExclusive(&lock_);
        return refuse("it has its own offer from the game (a chest, a bench, a bed...)");
    }
    bool range_added = false;
    reqs = adds_for(w, cur, entity, spec, true, &range_added);
    registry_.add(cur, entity, owner, spec, std::move(cb));
    State& st = state_[{cur, entity}];
    st.pending = static_cast<int>(reqs.size());
    st.range_added = range_added;
    ReleaseSRWLockExclusive(&lock_);
    submit(reqs);
    return true;
}

bool OfferService::remove(EmfWorld* w, uint32_t entity, const std::string& owner, std::string* why) {
    const uint32_t cur = session();
    std::vector<ComponentChangeRequest> reqs;
    AcquireSRWLockExclusive(&lock_);
    if (!registry_.remove(cur, entity, owner)) {
        ReleaseSRWLockExclusive(&lock_);
        if (why) *why = "this mod has no offer on it";
        return false;
    }
    const auto st = state_.find({cur, entity});
    const bool range = st == state_.end() || st->second.range_added;
    state_.erase({cur, entity});
    reqs.push_back(change_of(w, cur, entity, ecs::InteractionOfferRuntime::kHash, ComponentOp::Remove, {}, false));
    reqs.push_back(change_of(w, cur, entity, ecs::ClientInteractionOffer::kHash, ComponentOp::Remove, {}, false));
    if (range)
        reqs.push_back(change_of(w, cur, entity, ecs::CheckPlayerInRange::kHash, ComponentOp::Remove, {}, false));
    ReleaseSRWLockExclusive(&lock_);
    submit(reqs);
    return true;
}

std::vector<uint32_t> OfferService::remove_owner(EmfWorld* w, const std::string& owner) {
    const uint32_t cur = session();
    std::vector<uint32_t> mine;
    AcquireSRWLockExclusive(&lock_);
    for (const auto& e : registry_.all())
        if (e.owner == owner && e.session == cur) mine.push_back(e.entity);
    ReleaseSRWLockExclusive(&lock_);
    std::sort(mine.begin(), mine.end());
    for (const uint32_t id : mine) remove(w, id, owner, nullptr);
    AcquireSRWLockExclusive(&lock_);
    registry_.remove_owner(owner);  // and any of an ended session
    ReleaseSRWLockExclusive(&lock_);
    return mine;
}

void OfferService::on_accept(EmfWorld* w, const void* event, uint32_t size) {
    if (!event || size < ecs::InteractionAcceptedEvent::kSize) return;
    uint32_t guest = 0, host = 0;
    std::memcpy(&guest, static_cast<const uint8_t*>(event) + offsetof(ecs::InteractionAcceptedEvent, guestEntityId), 4);
    std::memcpy(&host, static_cast<const uint8_t*>(event) + offsetof(ecs::InteractionAcceptedEvent, hostEntityId), 4);
    AcquireSRWLockExclusive(&lock_);
    std::optional<OfferCallback> cb = registry_.accept(session(), host);
    ReleaseSRWLockExclusive(&lock_);
    if (cb && *cb) (*cb)(w, guest, host);
}

void OfferService::on_server_tick(EmfWorld* w) {
    const uint32_t cur = session();
    std::vector<ComponentChangeRequest> reqs;
    std::vector<std::string> gone;
    AcquireSRWLockExclusive(&lock_);
    if (cur != session_seen_) {  // a new world: the old one's offers went with it, and its ids mean other entities now
        registry_.new_session(cur);
        for (auto it = state_.begin(); it != state_.end();) it = it->first.first == cur ? std::next(it) : state_.erase(it);
        session_seen_ = cur;
    }
    if (++ticks_ >= kOfferUpkeepTicks) {
        ticks_ = 0;
        for (const auto& e : registry_.all()) {
            if (e.session != cur) continue;
            const auto has = [&](uint32_t hash) { return deps_.has && deps_.has(w, hash, e.entity); };
            State& st = state_[{cur, e.entity}];
            if (!has(ecs::CurrentTransform::kHash)) {
                gone.push_back(std::to_string(e.entity) + " (" + e.owner + ")");
                registry_.remove(cur, e.entity, e.owner);
                state_.erase({cur, e.entity});
            } else if (st.pending == 0 && !has(ecs::InteractionOfferRuntime::kHash)) {
                bool range_added = false;
                auto again = adds_for(w, cur, e.entity, e.spec, false, &range_added);
                st.pending = static_cast<int>(again.size());
                if (range_added) st.range_added = true;
                for (auto& r : again) reqs.push_back(std::move(r));
            }
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    for (const auto& g : gone) say("offer on entity " + g + " dropped: the entity is gone");
    submit(reqs);
}

std::vector<OfferRegistry::Entry> OfferService::list() const {
    AcquireSRWLockShared(&lock_);
    auto out = registry_.all();
    ReleaseSRWLockShared(&lock_);
    return out;
}

}  // namespace emf::core
