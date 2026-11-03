#include "components_api.h"

#include <cstdio>
#include <cstring>

#include "emf/emf_api.h"
#include "emf/generated/components.hpp"
#include "teleport.h"  // the record layout (kRecHashes, kRecSizes, kRecData)

namespace emf::core {

namespace {

namespace gen = emf::gen::ecs;

template <class T>
constexpr ComponentRule rule(const char* name, bool savable, bool add, bool remove) {
    return ComponentRule{name, T::kHash, static_cast<uint16_t>(T::kSize), savable, add, remove};
}

// The research's curated lists (add_component_api.md 5.3); savable = no do_not_save in docs/components.txt.
constexpr ComponentRule kRules[] = {
    rule<gen::ScaleComponent>("ScaleComponent", false, true, false),  // a creature's size (Beastmaster loyalty)
    rule<gen::IsTame>("IsTame", true, true, true),
    rule<gen::LifeTime>("LifeTime", true, true, true),
    rule<gen::InteractionPause>("InteractionPause", false, true, false),
    rule<gen::DoNotSave>("DoNotSave", true, true, true),  // removing it lets the entity be saved
    rule<gen::ForceSave>("ForceSave", true, true, true),
    rule<gen::ConditionalDespawn>("ConditionalDespawn", true, false, true),
    rule<gen::DespawnLink>("DespawnLink", true, false, true),
};

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

}  // namespace

const ComponentRule* component_rule(uint32_t hash) {
    if (!hash) return nullptr;
    for (const auto& r : kRules)
        if (r.hash == hash) return &r;
    return nullptr;
}

const ComponentRule* component_rule_by_name(const std::string& name) {
    std::string n = lower(name);
    if (const size_t colon = n.rfind(':'); colon != std::string::npos) n = n.substr(colon + 1);
    for (const auto& r : kRules)
        if (lower(r.name) == n) return &r;
    return nullptr;
}

std::string check_change(ComponentOp op, uint32_t hash, uint32_t data_size, bool data_given, uint32_t flags,
                         bool added_by_this_mod) {
    if (flags & ~(EMF_COMPONENT_PERSISTENT | EMF_COMPONENT_EXPERT)) return "an unknown flag";
    if (flags & EMF_COMPONENT_EXPERT)
        return "EMF_COMPONENT_EXPERT is reserved: this EMF changes only the components it lists";
    const ComponentRule* r = component_rule(hash);
    if (!r) {
        char b[16];
        std::snprintf(b, sizeof b, "0x%08x", hash);
        return std::string("component ") + b + " is not one EMF lets mods " + (op == ComponentOp::Add ? "add" : "remove");
    }
    if (op == ComponentOp::Add) {
        if (!r->add) return std::string(r->name) + " is not one EMF lets mods add";
        if (data_given && data_size != r->size)
            return std::string(r->name) + " has the size " + std::to_string(r->size) + " in this game build, not " +
                   std::to_string(data_size);
    } else if (!r->remove && !added_by_this_mod) {
        return std::string(r->name) + ": removing it is not allowed (only when this mod added it in this session)";
    }
    if (r->savable && !(flags & EMF_COMPONENT_PERSISTENT))
        return std::string(r->name) +
               " is saved with the world: the change outlives the mod (pass EMF_COMPONENT_PERSISTENT to accept that)";
    return {};
}

std::vector<uint8_t> add_component_record(uint32_t hash, const void* data, uint16_t size) {
    std::vector<uint8_t> rec(kRecData + size, 0);
    std::memcpy(rec.data() + kRecHashes, &hash, 4);
    std::memcpy(rec.data() + kRecSizes, &size, 2);
    if (data && size) std::memcpy(rec.data() + kRecData, data, size);
    return rec;
}

std::vector<uint8_t> remove_component_record(uint32_t hash) {
    std::vector<uint8_t> rec(kRemoveSize, 0);
    std::memcpy(rec.data() + kRecHashes, &hash, 4);
    return rec;
}

}  // namespace emf::core
