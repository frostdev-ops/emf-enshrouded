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

}  // namespace emf::core
