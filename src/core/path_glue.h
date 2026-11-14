#pragma once
// path_find (API v37): the game's path query calls behind EMF's path service, run by the internal server program
// "emf_path" (private research path_query_impl_2026-11-12.md section 7).
#include <cstdint>
#include <functional>
#include <string>

#include "core/path_service.h"
#include "core/system_programs.h"
#include "emf/emf_api.h"

namespace emf::core {

constexpr const char* kPathProgramName = "emf_path";
constexpr uint64_t kPathWatchdogMs = 5000;    // open queries, and the program has not run this long ...
constexpr uint64_t kPathWatchdogSteps = 150;  // ... nor in this many server steps (one long step is not a stall)

// The four game functions (Sig::CreatePathQuery ... Sig::PathQueryDefaults).
struct PathFns {
    uint32_t* (*create)(void* cmdbuf, uint32_t* out_id, const void* query) = nullptr;
    bool (*destroy)(void* cmdbuf, uint32_t entity, uint32_t id) = nullptr;
    bool (*get)(void* program_context, void* out, uint32_t id, bool accept_dirty) = nullptr;
    void* (*defaults)(void* query) = nullptr;
};

struct PathGlueDeps {
    PathFns fns;
    // The entity's component (CurrentTransform: whether the owner still exists), or null.
    std::function<const void*(uint8_t* world, uint32_t hash, uint32_t entity)> component;
    // Runs fn after the server world's next step, on its thread, with that world.
    std::function<void(std::function<void(EmfWorld*)>)> after_step;
    std::function<void(const std::string&)> log;
    std::function<uint64_t()> now_ms;
    // path.door: Sig::PathGridRegister, PathGridBoxToCells, PathGridNodePos and PathGridCellList are usable (the grid's layout is the one
    // EMF knows); false: path.door says it is not available on this build
    bool grid_layout = false;
};

// Sets the feature up (once, at start); its program to add to the server world.
ProgramSpec path_setup(PathGlueDeps deps);
// The path service while the feature is on (null when off or after a fault in a game call).
PathService* path_service();
// A new server world: the old world's queries end as cancelled (answered after the new world's first step).
void path_world_reset();
// After every server world step: when queries are open but emf_path has not run for kPathWatchdogMs (left out of the
// world), they end as cancelled (logged once per world).
void path_server_tick(uint64_t now_ms);
// Turns the feature off (its program could not be added).
void path_stop();

constexpr size_t kMaxDoorAsks = 16;  // path.door requests waiting for emf_path's next run
// path.door (0.6.11): emf_path reads the game's path grid for `entity` at its next run (private research
// path_doors_2026-11-14.md) and `reply` gets one or two lines after that step, on the server world's thread. "" when
// asked; else why not (not available on this build, path_find off, too many waiting).
std::string path_door_ask(uint32_t entity, std::function<void(const std::string&)> reply);

}  // namespace emf::core
