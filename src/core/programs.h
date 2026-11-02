#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace emf::core {

// One entry of a world's ECS program table (world + off::kProgramTable; its stride and the u16 count's offset are
// world_offsets().program_stride / program_count: 0x1F20 / +0xE15350 on 1273043, 0x1F28 / +0xE16F50 on 1283531).
struct ProgramInfo {
    uint16_t index = 0;
    std::string name;
    const uint8_t* desc = nullptr;  // ProgramDesc (0x188 B): +0x10 update fn
    uintptr_t update_fn = 0;
};

// Snapshot of all programs registered in `world` (fault-guarded; empty on a bad world, or when the program table's layout
// is not known on this build).
std::vector<ProgramInfo> list_programs(const uint8_t* world);
// The program called `name`, or an empty ProgramInfo (desc == nullptr).
ProgramInfo find_program(const uint8_t* world, std::string_view name);

// Program update hooks: pre(ctx,user) / original / post(ctx,user). Fixed pool of trampolines.
using ProgramCallback = void (*)(void* program_context, void* user);
constexpr size_t kMaxProgramHooks = 64;

struct ProgramHookSlot {
    uintptr_t target = 0;
    void (*original)(void*) = nullptr;
    ProgramCallback pre = nullptr;
    ProgramCallback post = nullptr;
    void* user = nullptr;
    bool faulted = false;
};

// Claims a free trampoline for `target`. Returns the detour to install and stores the slot index,
// or nullptr when the pool is full or the target is already hooked.
void* claim_program_hook(uintptr_t target, ProgramCallback pre, ProgramCallback post, void* user, size_t* slot_out);
// Where MinHook must write the original-function pointer for a claimed slot.
void** program_hook_original(size_t slot);
const ProgramHookSlot& program_hook_slot(size_t slot);
void reset_program_hooks();  // tests only

}  // namespace emf::core
