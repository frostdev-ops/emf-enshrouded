#pragma once
// Buffs and status effects (API v32, private research buffs_2026-11-11): the game-free part. The registry's buff type
// collection (BuffRegistry+0x20: a blob whose buffs array holds BuffType records of 0xB8 bytes), an entity's
// BuffContainer (472 bytes: food[4], buffs[16], debuffs[16], rested[1], terrain[1] of {buff entity, type, cleansed}),
// the 0xE8-byte ApplyBuffCtx the game's applyBuffCore takes, the check after it (the core says "ok" without applying
// for an excluded faction, a dead target and a resimulation), the cleanse, and the comparison of a player's container
// with the step before (the game's AppliedBuffEvent carries no target).
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

constexpr size_t kBuffTypeSize = 0xB8;
constexpr size_t kBuffContainerSize = 472;
constexpr size_t kApplyBuffCtxSize = 0xE8;
constexpr uint32_t kBuffMaxTypes = 4096;
// A buff whose end moved later by more than this is refreshed (rain re-applies Wet every step: its end creeps on).
constexpr int64_t kBuffRefreshMinNs = 1'000'000'000LL;

// ---- the registry's collection ----
struct BuffCollection {
    const uint8_t* records = nullptr;
    uint32_t count = 0;
};
// *(registry+0x20) = the collection; its buffs BlobArray {s32 rel from the field, u32 count} at +0x18. Every read is
// guarded; false for a null registry, no collection, a count outside 1..4096 or records that cannot be read.
bool buff_collection(const uint8_t* registry, BuffCollection* out);
const uint8_t* buff_record(const BuffCollection& c, uint32_t id);  // null when not there
bool buff_type_of(const uint8_t* record, EmfBuffType* out);          // false when it cannot be read
// FNV-1a of a 16-byte GUID (the game's tag and type ids), 0 for an all-zero GUID.
uint32_t buff_guid_id(const uint8_t* guid16);

// ---- an entity's container (a copy of its 472 bytes) ----
struct BuffNet {  // the buff entity's BuffNetworkData
    int64_t life = 0, end = 0;
    uint32_t type = 0, level = 0;
    bool known = false;  // false: no data (yet)
};
using BuffNetFn = BuffNet (*)(uint32_t buff_entity, void* ctx);
// The active buffs in slot order (food 3 slots, or 4 with extendedFoodBuffSlots; buffs 16; debuffs 16; rested;
// terrain): the number there is; fills `out` up to `cap`.
uint32_t read_buffs(const uint8_t* container, BuffNetFn net, void* ctx, int64_t now_ns, EmfBuff* out, uint32_t cap);
// The container's offset and slot count of a slot group (all 4 food slots: the game's cleanse looks at all four).
bool buff_has(const uint8_t* container, uint32_t type);
// The first instance of `type` gets cleansed = 1 (what the game's cleanseBuffType does; update_buffcontainer ends it
// the next step). False and nothing written when the container has none.
bool buff_cleanse(uint8_t* container, uint32_t type);

// ---- giving: the ApplyBuffCtx ----
struct BuffHandles {  // from emf_buffs' iterator
    void* program_context = nullptr;
    uint64_t container_w = 0, lifetime_w = 0, faction_r = 0;
    void* skill_tree = nullptr;
    void* balancing = nullptr;
    void* item_registry = nullptr;
    // the source's ImpactHierarchy, CritChance, CritDamageMod, DamageMod, DamageModAgainst, ToolDamageMod,
    // ImpactItemConfig, Strength, Intelligence, Dexterity, Level (ctx 0x68..0xB8)
    uint64_t source[11] = {};
    void* events = nullptr;  // the events-out slots hold the job pointer
};
struct BuffApplyArgs {
    int64_t duration_ns = 0;  // 0: the type's default
    uint32_t item = 0;
    bool with_source = false;  // copy the source's stats onto the buff
    void* target_skills_ui = nullptr;
    void* target_wet_reduction = nullptr;
};
void fill_apply_ctx(uint8_t* ctx, const BuffHandles& h, const BuffApplyArgs& a);

// After applyBuffCore: `err` its error byte, `container` the target's container read back.
struct BuffCheck {
    bool applied = false;
    std::string why;
};
BuffCheck check_applied(uint8_t err, const uint8_t* container, uint32_t type, uint8_t slot, uint16_t faction_exclude,
                        int target_faction, bool despawn_on_death);
const char* buff_slot_name(uint8_t slot);  // "food", "buff", "debuff", "rested", "terrain"

// ---- events: a player's container against the step before ----
class BuffDiff {
public:
    // The events of one entity's container this step; the first sight records only.
    std::vector<EmfBuffEvent> step(uint32_t entity, const uint8_t* container, BuffNetFn net, void* ctx, int64_t now_ns);
    void forget(uint32_t entity);
    void retain(const std::vector<uint32_t>& present);  // forgets every entity not in `present`
    void reset();
    size_t size() const { return last_.size(); }

private:
    struct Inst {
        uint32_t buff_entity, type;
        uint8_t slot, cleansed;
        int64_t end;  // -1: not known
    };
    std::unordered_map<uint32_t, std::vector<Inst>> last_;
};

}  // namespace emf::core
