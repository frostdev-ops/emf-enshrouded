#include "world_offsets.h"

#include <cstdio>

namespace emf {
namespace {
WorldOffsets g_offsets = kBaseWorldOffsets;

uint32_t moved(uint32_t base_offset, uint32_t storage) {
    return storage + (base_offset - kBaseWorldOffsets.storage);  // unsigned: base_offset below the storage wraps back
}

std::string hex_shift(int64_t shift) {
    char s[32];
    std::snprintf(s, sizeof s, "%s0x%llx", shift < 0 ? "-" : "+",
                  static_cast<unsigned long long>(shift < 0 ? -shift : shift));
    return s;
}
}  // namespace

WorldOffsets shifted_world_offsets(uint32_t storage) {
    const WorldOffsets& b = kBaseWorldOffsets;
    WorldOffsets o = b;
    o.storage = storage;
    o.kind = moved(b.kind, storage);
    o.entity_map = moved(b.entity_map, storage);
    o.tick_period_ns = moved(b.tick_period_ns, storage);
    o.tick = moved(b.tick, storage);
    o.event_types = moved(b.event_types, storage);
    o.event_map = moved(b.event_map, storage);
    return o;
}

const WorldOffsets& world_offsets() { return g_offsets; }
void set_world_offsets(const WorldOffsets& offsets) { g_offsets = offsets; }

WorldOffsetPlan plan_world_offsets(const std::array<ResolvedOffset, kCodeOffsetCount>& code, bool known_build) {
    WorldOffsetPlan p;
    const ResolvedOffset& storage = code[static_cast<size_t>(CodeOffset::WorldStorage)];
    char line[256];
    if (!is_usable(storage, known_build)) {
        std::snprintf(line, sizeof line,
                      "world offsets: GetComponent's code does not show the entity storage's offset (%zu match(es))",
                      storage.count);
        p.line = line;
        return p;
    }
    if (!world_tick_offsets_ok(code, known_build)) {
        const ResolvedOffset& period = code[static_cast<size_t>(CodeOffset::StorageTickPeriod)];
        const ResolvedOffset& tick = code[static_cast<size_t>(CodeOffset::StorageTick)];
        std::snprintf(line, sizeof line,
                      "world offsets: SimStep's code does not confirm the entity storage's tick fields (period +0x%x, "
                      "tick +0x%x; EMF expects +0x%x, +0x%x)",
                      period.count == 1 ? period.value : 0u, tick.count == 1 ? tick.value : 0u,
                      kBaseWorldOffsets.tick_period_ns - kBaseWorldOffsets.storage,
                      kBaseWorldOffsets.tick - kBaseWorldOffsets.storage);
        p.line = line;
        return p;
    }
    // the event registry lies before the storage: a storage closer to the world's start than that is not this layout
    if (storage.value < kBaseWorldOffsets.storage - kBaseWorldOffsets.event_types) {
        std::snprintf(line, sizeof line,
                      "world offsets: GetComponent's code puts the entity storage at +0x%x, too close to the world's "
                      "start for its layout",
                      storage.value);
        p.line = line;
        return p;
    }
    p.ok = true;
    p.offsets = shifted_world_offsets(storage.value);
    p.shift = static_cast<int64_t>(storage.value) - static_cast<int64_t>(kBaseWorldOffsets.storage);
    const ResolvedOffset& count = code[static_cast<size_t>(CodeOffset::ProgramCount)];
    const ResolvedOffset& stride = code[static_cast<size_t>(CodeOffset::ProgramStride)];
    p.program_table = is_usable(count, known_build) && is_usable(stride, known_build);
    p.offsets.program_count = p.program_table ? count.value : 0;
    p.offsets.program_stride = p.program_table ? stride.value : 0;

    if (storage.from_record && storage.count == 1)
        std::snprintf(line, sizeof line, "world offsets from the build's record (its code shows +0x%x): storage +0x%x (shift %s)",
                      storage.scanned, storage.value, hex_shift(p.shift).c_str());
    else if (storage.from_record)
        std::snprintf(line, sizeof line, "world offsets from the build's record (not found in its code): storage +0x%x (shift %s)",
                      storage.value, hex_shift(p.shift).c_str());
    else
        std::snprintf(line, sizeof line, "world offsets from code: storage +0x%x (shift %s)", storage.value,
                      hex_shift(p.shift).c_str());
    p.line = line;
    if (p.program_table) {
        std::snprintf(line, sizeof line, ", program table: count +0x%x, stride 0x%x", count.value, stride.value);
        p.line += line;
    } else {
        p.line += ", program table: not confirmed in RegisterProgram's code (hook_program and register_program are off)";
    }
    return p;
}

}  // namespace emf
