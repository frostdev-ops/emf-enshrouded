#pragma once
// Mods' HUD lines as HUD parts (API v12 hud_set, Lua emf.hud): EMF's overlay draws them, so it records each place's drawn
// block here, in UI units, for the HUD page's picking and dragging. Relaxed atomics: the render thread writes, the page
// reads.
#include <cstdint>

#include "core/hud_editor/hook.h"

namespace emf::core::hud {

// The block the overlay drew at a place (0 top right, 1 bottom right, 2 bottom left) this frame; another place is ignored.
void set_lines_live(int place, const float rect_units[4], uint64_t now_ms);
// The place drew nothing this frame (no lines, or hidden): it stays seen, with no area left to pick.
void lines_not_drawn(int place);
LiveInfo lines_live(int place);
void lines_reset_for_tests();

}  // namespace emf::core::hud
