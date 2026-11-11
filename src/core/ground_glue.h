#pragma once
// Markers step 2 (API v34): the game end of GroundService (private research map_click_2026-11-11 3.2-3.5): the voxel
// grid (the server world's "ecs::VoxelGrid" singleton: the realm table at +0x1bf8, count +0x1c00, stride 0x3870; a
// realm's grid: cell shift +0x10, nx +0x14, ny +0x18, origin +0x70/+0x74/+0x78), the realm the server's programs run in
// (from EMF's own teleport program's context: *(*(ctx)+0xbb)), the game's voxel ray (Sig::VoxelRay) and the scene's
// reset box (a post-hook on Sig::ResetBoundsSet: box 0 at manager+0xb0, its top = worldMax.y). Every read is guarded.
#include <cstdint>
#include <string>

#include "core/ground_search.h"
#include "emf/emf_api.h"

namespace emf::core {

using VoxelRayFn = void* (*)(void* hit, void* terrain, const float* from, const float* to, const uint8_t* options);

void ground_set_ray(VoxelRayFn fn);
bool ground_ray_ready();
// Hooks the reset-bounds setter (once); false (why) when it cannot be hooked.
bool ground_hook_reset_bounds(uintptr_t setter, std::string* why);
// EMF's teleport program, every server step: the realm the server world's programs run in.
void ground_note_realm(void* program_context);
// GroundService's Deps.
bool ground_limits(EmfWorld* world, GroundLimits* out, std::string* why);
bool ground_ray(EmfWorld* world, double x, double z, double from_y, double to_y, RayResult* out);
// For the log and the console: what the last limits read found ("" before any).
std::string ground_describe();

}  // namespace emf::core
