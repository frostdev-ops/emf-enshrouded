#pragma once
// HUD scale through the game's UI layer transform stack (docs/engine_parts/re_ui_scopes.md): after a part's scope opens EMF
// pushes a transform that scales around an anchor on the part's rectangle; after the scope closes it pops it. Everything
// the scope draws is scaled (text stays sharp). The detours call these inside their guarded functions: no SEH, no
// locks and no allocation here.
#include <cstddef>
#include <cstdint>

namespace emf::core::hud {

// Build 1287369 (docs/engine_parts/re_ui_scopes.md)
constexpr size_t kNodeWindow = 0x50, kWindowLayer = 0x18, kLayerType = 0x08, kLayerDrawList = 0x6ce0,
                 kLayerClip = 0x21d0, kLayerXforms = 0x21e0, kLayerXformIndex = 0x4208, kLayerXformCount = 0x4210,
                 kXformEntrySize = 16, kDlXformIndex = 0x3522c, kDlXformFlag = 0x352d2;
constexpr int kLayerTypeUi = 4;

struct Xform {
    float scale = 1, tx = 0, ty = 0;  // screen = scale * p + t
};
struct Anchor {
    float ax = 0.5f, ay = 0.5f;  // 0 = left/top edge, 0.5 = centre, 1 = right/bottom edge
};

// The screen edge (or centre) the rectangle's centre is nearest, by thirds of the layer's clip area; the centre when the
// area is not usable. rect and area are {x, y, w, h}.
Anchor auto_anchor(const float rect[4], const float area[4]);
// The point on the rectangle: ox = x + w*ax, oy = y + h*ay.
void anchor_point(const float rect[4], Anchor a, float* ox, float* oy);
// {S*s, tx + S*ox*(1-s), ty + S*oy*(1-s)} for the stack's top {S, tx, ty}.
Xform scaled_about(const Xform& top, float s, float ox, float oy);
// The scale is above 0 and every field is finite.
bool usable_top(const Xform& t);
enum class PopCheck { Pop, Mismatch };
// Pop only when the count now is exactly one above the depth at the push.
PopCheck check_pop(uint64_t count_now, uint64_t depth_at_push);

// *(*(node+0x50)+0x18) when non-null, of type 4 and with a draw list; else null.
uint8_t* layer_of(uint8_t* node);
// The stack's top (identity when empty) and its depth; false when the top is not usable.
bool read_top(const uint8_t* layer, Xform* top, uint64_t* depth);
// After a pop that emptied the stack: the draw list's transform index and flag back to 0.
void clear_emptied_index(uint8_t* layer);

using PushXformFn = void (*)(void* layer, const float* xform);  // xform = {scale, tx, ty}
using PopXformFn = void (*)(void* layer);
// Null: scale is not available.
void set_transform_functions(PushXformFn push, PopXformFn pop);

struct ScalePush {
    uint8_t* layer = nullptr;
    uint64_t depth = 0;
    bool pushed = false;
};
// After the original open: pushes the scaled transform, or returns {} (scale 1 or out of range, no functions, scale off,
// no UI layer, a rectangle or top that is not usable).
ScalePush scale_open(uint8_t* node, float s);
// Pushes a translation by (dx, dy) layer units on top of the stack's top ({S, tx + S*dx, ty + S*dy}), or returns {} (no
// functions, scale off, no UI layer, a top that is not usable, an offset that is not finite). Popped with scale_close.
ScalePush shift_open(uint8_t* node, float dx, float dy);
// After the original close: pops what scale_open pushed, only when the stack stands exactly one above where it was.
// Otherwise scale turns off for the session. Pops even when the editor is off.
void scale_close(const ScalePush& p);
// A scope with our push was dropped without its close: scale turns off.
void scale_lost(const ScalePush& p);

struct ScaleState {
    bool available = false;  // both functions are set
    bool off = false;        // turned off for the session after a mismatch
    uint64_t pushes = 0, pops = 0, mismatches = 0;
};
ScaleState hud_scale_state();
void scale_reset_for_tests();  // counters, off, functions

}  // namespace emf::core::hud
