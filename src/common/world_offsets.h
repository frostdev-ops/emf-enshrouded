#pragma once
// The game's World object: the offsets a game update can move as one block. Build 1283531 made each of the 0x380
// entries of the ECS program table (world + off::kProgramTable) 8 bytes longer, which moved everything after the table
// by +0x1C00: the program count, the event registry and the entity storage with its kind, entity map and tick fields.
// offsets.h keeps the values of builds 1269579 and 1273043 as the base. At start the core reads the storage's offset
// from GetComponent's code and shifts the block by its distance from the base, and reads the program table's count and
// stride from RegisterProgram's code (code_offsets.h); world_offsets() then gives what every world reader uses.
#include <array>
#include <cstdint>
#include <string>

#include "code_offsets.h"
#include "offsets.h"

namespace emf {

struct WorldOffsets {
    // the block that moves as one (from the world's base); the storage-relative distances are the same on every build
    uint32_t storage;         // entity storage / sim (argument of the sim step)
    uint32_t kind;            // u8: 0 server, 1 prediction, 2 client (storage + 0x8)
    uint32_t entity_map;      // Keen map: EntityId (mixed) -> entity record* (storage + 0x148)
    uint32_t tick_period_ns;  // i64 ns per tick (storage + 0x4C0); ecsNow = period * tick
    uint32_t tick;            // u32 current tick (storage + 0x4DC)
    uint32_t event_types;     // ptr to the event type entries (storage - 0x308)
    uint32_t event_map;       // Keen map: fnv1a32("keen::ecs::" + name) -> u16 event type index (storage - 0x2E0)
    // the ECS program table, read from RegisterProgram's code (not shifted); 0 = not known on this build
    uint32_t program_count;   // u16 number of programs
    uint32_t program_stride;  // bytes per entry
};

// Builds 1269579 and 1273043.
inline constexpr WorldOffsets kBaseWorldOffsets{off::kStorage,     off::kKind,       off::kEntityMap,
                                                off::kTickPeriodNs, off::kTick,       off::kEventTypes,
                                                off::kEventMap,    off::kProgramCount, off::kProgramStride};

// The block moved so that the entity storage sits at `storage`: every offset of the block is the base's plus
// (storage - the base's storage). The program table keeps the base's values.
WorldOffsets shifted_world_offsets(uint32_t storage);

// The offsets in use: the base until set_world_offsets. The core and the probe set them once at start, before any hook
// can run; tests set them and put the base back (tests/fake_world.h ScopedWorldOffsets).
const WorldOffsets& world_offsets();
void set_world_offsets(const WorldOffsets& offsets);

// The start-up decision. ok: the world's offsets are known (a known build: its records, normally confirmed by the scan;
// an unknown build: one match of GetComponent's storage offset, SimStep's tick fields where EMF expects them relative to
// the storage, and a storage high enough for the block before it); without it EMF stays idle. program_table: the
// program count and stride are known (an unknown build: one match each, entries of at least 0x20 bytes and the count
// after the table's end); else both are 0 in `offsets` and the program features stay off.
struct WorldOffsetPlan {
    bool ok = false;
    bool program_table = false;
    WorldOffsets offsets = kBaseWorldOffsets;
    int64_t shift = 0;  // offsets.storage - the base's storage
    std::string line;   // the one log line
};
WorldOffsetPlan plan_world_offsets(const std::array<ResolvedOffset, kCodeOffsetCount>& code, bool known_build);

}  // namespace emf
