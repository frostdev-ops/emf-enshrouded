#pragma once
// Markers step 3b (API v35): the game's own world map drawn into the canvases' MAP commands (a minimap), by the game's
// map canvas MapCanvasDraw (private docs/research/minimap_renderer_2026-11-12.md). Called on the UI thread just before
// the game closes doPlayStateUi() (on every UI pass, as the game's own map is): EMF opens a scope of its own (through the
// scope open's trampoline), sets that node's rect to the whole world's canvas rect, pushes a clip to the minimap's
// rect, draws, pops, restores the rect and closes the scope. Every read is guarded before the call; a fault inside it
// turns the map drawing off for the session. Client only.
#include <cstdint>
#include <string>

#include "core/hud_editor/hook.h"

namespace emf::core {

using MapCanvasFn = void (*)(void* scope, const void* view);
using UiPushClipFn = void (*)(void* layer, const float* rect, char intersect);
using UiPopClipFn = void (*)(void* layer);

// Start-up: the functions (null: no map drawing on this build) and the UI frame context's global (read from
// MapCanvasDraw's code at +0x1e: mov r8, [rip + disp32]).
void minimap_draw_setup(MapCanvasFn canvas, UiPushClipFn push, UiPopClipFn pop, hud::OpenFn open, hud::CloseFn close);
bool minimap_draw_ready();
// The UI thread, inside doPlayStateUi() just before its close.
void minimap_draw_in_play_ui();
std::string minimap_draw_status();  // for the log: on, off and why, or turned off after a fault

}  // namespace emf::core
