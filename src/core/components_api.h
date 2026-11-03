#pragma once
// Adding and removing components (EMF API v23): which components a mod may add or remove, the checks a request
// passes before it is queued, and the engine's own "add components" (kind 3) and "remove components" (kind 4) records,
// the ones the shipped teleport pushes (docs/engine_parts/re_teleport.md 1.2; research add_component_api.md).
// v1 lets mods change only a curated list; anything else is refused. Pure (unit-tested).
#include <cstdint>
#include <string>
#include <vector>

namespace emf::core {

enum class ComponentOp { Add, Remove };

struct ComponentRule {
    const char* name;  // short name ("ScaleComponent")
    uint32_t hash;     // qualifiedHash (generated kHash)
    uint16_t size;     // its size in this game build (generated kSize)
    bool savable;      // no do_not_save: a change goes into the world save and outlives the mod
    bool add;          // mods may add it
    bool remove;       // mods may remove it (and any component they added this session)
};

// The curated rule for a component, or nullptr (not one mods may change).
const ComponentRule* component_rule(uint32_t hash);
// By short or full name ("ScaleComponent", "keen::ecs::ScaleComponent"), any case; nullptr when not curated.
const ComponentRule* component_rule_by_name(const std::string& name);

// "" when the change may be queued, else why not. `data_given`: the mod passed data (else zeros); `added_by_this_mod`:
// this mod added that component in this session (it may remove it again).
std::string check_change(ComponentOp op, uint32_t hash, uint32_t data_size, bool data_given, uint32_t flags,
                         bool added_by_this_mod);

// Whole records as QueuePush hands them out (the first 0xC header bytes stay QueuePush's): kind 3 adding one
// component (hash at +0xC, the other 15 hash slots 0, size at +0x4C, the data at +0x6C; null data = zeros) and kind 4
// removing one (hash at +0xC).
std::vector<uint8_t> add_component_record(uint32_t hash, const void* data, uint16_t size);
std::vector<uint8_t> remove_component_record(uint32_t hash);

}  // namespace emf::core
