// The world's offsets that a game update moves as one block (build 1283531: +0x1C00, every ECS program entry 8 bytes
// longer), derived at start from the entity storage's offset in GetComponent's code.
#include <doctest.h>

#include <string>

#include "common/world_offsets.h"
#include "fake_world.h"

using namespace emf;

namespace {
constexpr size_t kStorage = static_cast<size_t>(CodeOffset::WorldStorage);
constexpr size_t kPeriod = static_cast<size_t>(CodeOffset::StorageTickPeriod);
constexpr size_t kTick = static_cast<size_t>(CodeOffset::StorageTick);
constexpr size_t kCount = static_cast<size_t>(CodeOffset::ProgramCount);
constexpr size_t kStride = static_cast<size_t>(CodeOffset::ProgramStride);

ResolvedOffset found(uint32_t v, bool matches_record = false) {
    ResolvedOffset r;
    r.value = r.scanned = v;
    r.count = 1;
    r.value_matches = matches_record;
    return r;
}
// What resolve_code_offsets gives on an exe whose hash pins build 1283531 and whose code agrees with the records.
std::array<ResolvedOffset, kCodeOffsetCount> build_1283531() {
    std::array<ResolvedOffset, kCodeOffsetCount> r{};
    r[kStorage] = found(0xE179C0, true);
    r[kPeriod] = found(0x4C0, true);
    r[kTick] = found(0x4DC, true);
    r[kCount] = found(0xE16F50, true);
    r[kStride] = found(0x1F28, true);
    return r;
}
// An unknown build whose code shows the storage at `storage` and SimStep's tick fields at period/tick.
std::array<ResolvedOffset, kCodeOffsetCount> unknown(uint32_t storage, uint32_t period = 0x4C0, uint32_t tick = 0x4DC) {
    std::array<ResolvedOffset, kCodeOffsetCount> r{};
    r[kStorage] = found(storage);
    r[kPeriod] = found(period);
    r[kTick] = found(tick);
    r[kCount] = found(storage - 0xA70);
    r[kStride] = found(0x1F30);
    return r;
}
}  // namespace

TEST_CASE("world offsets: the base is build 1273043's layout (offsets.h)") {
    const WorldOffsets& b = kBaseWorldOffsets;
    CHECK(b.storage == 0xE15DC0u);
    CHECK(b.kind == 0xE15DC8u);
    CHECK(b.entity_map == 0xE15F08u);
    CHECK(b.tick_period_ns == 0xE16280u);
    CHECK(b.tick == 0xE1629Cu);
    CHECK(b.event_types == 0xE15AB8u);
    CHECK(b.event_map == 0xE15AE0u);
    CHECK(b.program_count == 0xE15350u);
    CHECK(b.program_stride == 0x1F20u);
}

TEST_CASE("world offsets: the block moves with the entity storage (build 1283531's offsets, counted in its code)") {
    const WorldOffsets o = shifted_world_offsets(0xE179C0);
    CHECK(o.storage == 0xE179C0u);
    CHECK(o.kind == 0xE179C8u);           // 65 times in the new client's .text
    CHECK(o.entity_map == 0xE17B08u);     // 13
    CHECK(o.tick_period_ns == 0xE17E80u); // 6
    CHECK(o.tick == 0xE17E9Cu);           // 31
    CHECK(o.event_types == 0xE176B8u);    // 5
    CHECK(o.event_map == 0xE176E0u);      // 4
    // the program table is read on its own (RegisterProgram's code), not shifted
    CHECK(o.program_count == kBaseWorldOffsets.program_count);
    CHECK(o.program_stride == kBaseWorldOffsets.program_stride);
    const WorldOffsets back = shifted_world_offsets(0xE15DC0);
    CHECK(back.kind == kBaseWorldOffsets.kind);
    CHECK(back.event_types == kBaseWorldOffsets.event_types);
}

TEST_CASE("world offsets plan: a known build (1283531) takes the storage and the program table from its code") {
    const WorldOffsetPlan p = plan_world_offsets(build_1283531(), true);
    CHECK(p.ok);
    CHECK(p.program_table);
    CHECK(p.shift == 0x1C00);
    CHECK(p.offsets.storage == 0xE179C0u);
    CHECK(p.offsets.tick == 0xE17E9Cu);
    CHECK(p.offsets.event_map == 0xE176E0u);
    CHECK(p.offsets.program_count == 0xE16F50u);
    CHECK(p.offsets.program_stride == 0x1F28u);
    CHECK(p.line == "world offsets from code: storage +0xe179c0 (shift +0x1c00), program table: count +0xe16f50, "
                    "stride 0x1f28");
}

TEST_CASE("world offsets plan: build 1273043 keeps the base (shift +0x0)") {
    auto r = build_1283531();
    r[kStorage] = found(0xE15DC0, true);
    r[kCount] = found(0xE15350, true);
    r[kStride] = found(0x1F20, true);
    const WorldOffsetPlan p = plan_world_offsets(r, true);
    CHECK(p.ok);
    CHECK(p.shift == 0);
    CHECK(p.offsets.kind == kBaseWorldOffsets.kind);
    CHECK(p.line == "world offsets from code: storage +0xe15dc0 (shift +0x0), program table: count +0xe15350, "
                    "stride 0x1f20");
}

TEST_CASE("world offsets plan: a known build whose code disagrees uses its record and says so") {
    // the exe's hash says 1273043 (record 0xE15DC0) but GetComponent shows 0xE179C0: the hash pins the code, so the
    // record is used (resolve_code_offsets sets from_record and the value), and the line names both
    auto r = build_1283531();
    r[kStorage].value = 0xE15DC0;
    r[kStorage].value_matches = false;
    r[kStorage].from_record = true;
    const WorldOffsetPlan p = plan_world_offsets(r, true);
    CHECK(p.ok);
    CHECK(p.shift == 0);
    CHECK(p.offsets.storage == 0xE15DC0u);
    CHECK(p.line.rfind("world offsets from the build's record (its code shows +0xe179c0): storage +0xe15dc0 (shift +0x0)",
                       0) == 0);
}

TEST_CASE("world offsets plan: an unknown build follows GetComponent's storage when SimStep confirms the tick fields") {
    const WorldOffsetPlan p = plan_world_offsets(unknown(0xE1A9C0), false);
    CHECK(p.ok);
    CHECK(p.shift == 0x4C00);
    CHECK(p.offsets.storage == 0xE1A9C0u);
    CHECK(p.offsets.kind == 0xE1A9C8u);
    CHECK(p.offsets.tick == 0xE1AE9Cu);
    CHECK(p.program_table);
    CHECK(p.offsets.program_count == 0xE19F50u);
    CHECK(p.offsets.program_stride == 0x1F30u);
    CHECK(p.line == "world offsets from code: storage +0xe1a9c0 (shift +0x4c00), program table: count +0xe19f50, "
                    "stride 0x1f30");
    // a block that moved down
    const WorldOffsetPlan down = plan_world_offsets(unknown(0xE15CC0), false);
    CHECK(down.ok);
    CHECK(down.shift == -0x100);
    CHECK(down.line.rfind("world offsets from code: storage +0xe15cc0 (shift -0x100)", 0) == 0);
}

TEST_CASE("world offsets plan: an unknown build whose SimStep code moved the tick fields stays off") {
    const WorldOffsetPlan p = plan_world_offsets(unknown(0xE179C0, 0x4C8), false);
    CHECK_FALSE(p.ok);
    CHECK(p.line == "world offsets: SimStep's code does not confirm the entity storage's tick fields (period +0x4c8, "
                    "tick +0x4dc; EMF expects +0x4c0, +0x4dc)");
    auto r = unknown(0xE179C0);
    r[kTick].count = 0;  // the tick instruction not found
    CHECK_FALSE(plan_world_offsets(r, false).ok);
}

TEST_CASE("world offsets plan: an unknown build without one clear storage offset in GetComponent stays off") {
    auto r = unknown(0xE179C0);
    r[kStorage].count = 0;
    WorldOffsetPlan p = plan_world_offsets(r, false);
    CHECK_FALSE(p.ok);
    CHECK(p.line == "world offsets: GetComponent's code does not show the entity storage's offset (0 match(es))");
    r[kStorage].count = 2;
    CHECK_FALSE(plan_world_offsets(r, false).ok);
    // a storage so low that the event registry before it would wrap below the world's start
    p = plan_world_offsets(unknown(0x200), false);
    CHECK_FALSE(p.ok);
    CHECK(p.line.find("+0x200") != std::string::npos);
}

TEST_CASE("world offsets plan: an unknown build whose program table is not confirmed runs without it") {
    auto r = unknown(0xE179C0);
    r[kStride].count = 2;  // ambiguous
    const WorldOffsetPlan p = plan_world_offsets(r, false);
    CHECK(p.ok);
    CHECK_FALSE(p.program_table);
    CHECK(p.offsets.program_count == 0u);
    CHECK(p.offsets.program_stride == 0u);
    CHECK(p.line == "world offsets from code: storage +0xe179c0 (shift +0x1c00), program table: not confirmed in "
                    "RegisterProgram's code (hook_program and register_program are off)");
}

TEST_CASE("world offsets plan: an unknown build's program table must fit: entries that hold a name, the count after them") {
    auto r = unknown(0xE179C0);  // count +0xE16F50
    r[kStride] = found(0x10);    // too short for an entry (EMF reads its name at +0x10 / +0x18): a stray match
    CHECK_FALSE(plan_world_offsets(r, false).program_table);
    r[kStride] = found(0x1F40);  // 0x380 entries of 0x1F40 end at +0xE19350, past the count
    CHECK_FALSE(plan_world_offsets(r, false).program_table);
    r[kStride] = found(0x1F28);  // build 1283531's: the table ends at +0xE13F50, before the count
    CHECK(plan_world_offsets(r, false).program_table);
    // a known build's records are taken as they are
    CHECK(plan_world_offsets(build_1283531(), true).program_table);
}

TEST_CASE("world offsets: the base until set, then what the start-up set") {
    CHECK(world_offsets().storage == kBaseWorldOffsets.storage);
    {
        test::ScopedWorldOffsets moved(shifted_world_offsets(0xE179C0));
        CHECK(world_offsets().tick == 0xE17E9Cu);
    }
    CHECK(world_offsets().tick == kBaseWorldOffsets.tick);
}
