#pragma once
// The vitals' bars and the status list (docs/engine_parts/re_ui_scopes.md): which bar a call of the player widget's bar
// function is, the offsets that stack the bars in the layout's order, and the offset that makes the status effects list
// grow upward from the food icons' bottom edge while it stays on screen. Pure functions; no Windows headers.
#include <array>
#include <cstdint>
#include <optional>

#include "core/hud_editor/colours.h"
#include "core/hud_editor/elements.h"
#include "core/hud_editor/hook.h"
#include "core/hud_editor/scale.h"
#include "core/hud_editor/snapshot.h"

namespace emf::core::hud {

// Build 1287369: the bars' data in the widget (health, mana, stamina), and the game's gap between two bars (units).
constexpr uintptr_t kBarDataFirst = 0x18, kBarDataStride = 0xd8;
constexpr float kBarGap = 6.0f;

// base = the first call's data - kBarDataFirst; 0..2 when data is that bar's, else -1.
int bar_index(uintptr_t data, uintptr_t base);

struct BarRect {
    float x = 0, y = 0, w = 0, h = 0;  // UI units, as the game laid the bar out (before EMF's offsets)
    bool seen = false;
};

// The y offsets that stack the seen bars that are not hidden in `order`, from the top of the seen bars, each next one
// its height plus the gap below (the gap measured between the first two seen bars in the game's order, kBarGap with
// fewer); 0 for a bar not seen or hidden. A hidden or absent bar leaves no hole.
std::array<float, kBarCount> order_dy(const std::array<BarRect, kBarCount>& game, const BarOrder& order,
                                      const std::array<bool, kBarCount>& hidden);

// The y offset (layer units) for the status list at list_y with height list_h beside a food column column_h high:
// column_h - list_h (its bottom on the column's bottom), then at most what keeps its bottom on screen and at least what
// keeps its top on screen (the top wins); top is the transform stack's top (screen = scale * p + t), ui_h the screen's
// height in UI units. 0 when an input is not usable.
float grow_up_dy(float list_y, float list_h, float column_h, const Xform& top, float ui_h);

// ---- the bar detour (the player widget's bar function: out, data, style -> out, a {x, y, w, h} rect) ----
using BarFn = void* (*)(void* out, void* data, void* style);

// The detour's work around one call, split so hook.cpp guards each half and calls the original between them.
struct BarCall {
    int bar = -1;          // the bar this call draws (inside vitalBars only), -1: the original runs untouched
    float dx = 0, dy = 0;  // the offset applied (layer units): only when the translation was pushed
    ScalePush push;
    bool recolour = false;  // the original gets `style` (EMF's copy with the player's colours), not the game's block
    StyleBlock style{};
};
// Before the original: which bar (the first call in vitalBars sets the base), the game's colour block recorded (read from
// `style`, the game's) and EMF's copy when the bar has colours, its offset (the layout's, plus the order's from last
// frame's rects, plus kHideShift when hidden) and the pushed translation.
BarCall bar_before(void* data, const void* style, const HudSnapshot* snap, UiSpace ui);
// The style the original gets: EMF's copy in `c` when it recolours, else the game's own.
void* bar_style(BarCall& c, void* style);
// After the original: pops, records the game's rect for the next frame's order, shifts the returned rect by the offset
// (the bar's panel piece follows) and records it as the bar's live rect.
void bar_after(const BarCall& c, void* result, uint64_t now_ms);
// bar_before, the original, bar_after: the tests' entry (no SEH).
void* bar_call_path(void* out, void* data, void* style, BarFn original, const HudSnapshot* snap, UiSpace ui,
                    uint64_t now_ms);
// vitalBars opened / closed on this thread (hook.cpp): at open, this frame's game rects become last frame's (and a panel
// colour still written is put back); at close, the panel colour is written when the snapshot has one (null: not).
void bars_vitals_open(uint8_t* node);
void bars_vitals_close(const HudSnapshot* snap);

// ---- the panel colour: a scoped write into health's frame colour (the widget tints the panel behind the bars with it,
// after vitalBars closed and before its inner "doPlayerHealthWidget()" scope closes) ----
constexpr uint64_t kPlayerWidgetHash = 0x9442c26a6a17b7a0ull;  // fnv1a64("doPlayerHealthWidget()")
// Whether n bytes at p may be written as they are (hook.cpp: VirtualQuery; EMF never changes page protection). Null:
// never written.
using WritableFn = bool (*)(const void* p, size_t n);
void set_panel_writable_check(WritableFn f);
// Puts the saved frame colour back when it was written (the widget closed, the next vitalBars opened, the editor is off).
void panel_restore();
PanelStatus panel_status();
LiveInfo bar_live(int bar);
// The game's own colour block of a bar, as last drawn inside vitalBars; empty until then (stamina while full).
std::optional<StyleBlock> bar_game_colours(int bar);
ColourStats colour_stats();
void bars_reset_for_tests();  // this thread's state, the live info, the game's colours and the counters

}  // namespace emf::core::hud
