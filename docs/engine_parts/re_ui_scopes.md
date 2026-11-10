# The game's UI scopes and the UI layer's transform stack (HUD editor)

Build 1287369 (the game exe). Confirmed by static analysis of that build and by three spike runs with a native test mod in
a local test world on 2026-11-08. The dedicated server has no UI. Addresses are RVAs from the image base.

## UI scopes

- `UiScopeOpen(scope, nameInfo)` is at RVA 0xa3cb10 and `UiScopeClose(scope)` at 0xa40080. `nameInfo` is
  `{const char* name; size_t len; uint64_t hash}`, where the hash is FNV-1a 64 of the name.
- The scope: `scope[0]` is the parent node and `scope[1]` (+0x08) is this scope's node, which lives from frame to frame.
  The byte at +0x14 means "attach at close". The byte at +0x20 is 1 when the game draws its own magenta debug outline for
  the scope; the open sets it to 0.
- The node: +0x70 is the position (x, y) the drawing code set, a layout input. The inputs from +0x70 to +0xc8 are reset on
  the first touch in a frame. +0x78, +0x80 and +0x88 are sizes. +0x90 is not a scale. +0x114 is the rectangle as laid
  out, with a copy at +0x104.
- An edit sticks when it is added to +0x70 just before the original close. Writing after the open is overwritten, and
  writing the rectangle does nothing.
- Hide works by subtracting 20000 from the position. A size of 0.001 does not hide.
- About 98 named scopes are opened in a world, each once a frame.
- The UI space is the rectangle of `doPlayStateUi()`: 0,0 2084.2x1080 on the test PC (height 1080).

## Scale: the layer's transform stack

Confirmed live with 1.5 and 0.75 on `playerStatusParent`, `compassSurvivalMeterParent` and `barHudStackParent`: 4408
pushes and 4408 pops, and the text stayed sharp.

- A node draws into the layer `L = *(*(node+0x50)+0x18)`. A UI layer has the int 4 at L+8 and its draw list
  `DL = *(L+0x6ce0)`.
- The layer keeps a transform stack of a uniform scale plus a translation: on screen a point is `scale*p + t`. Its count is
  the u64 at L+0x4210, its index stack the u16 array at `*(L+0x4208)`, and its entries `{scale, tx, ty, idx}` are 16 bytes
  each at `*(L+0x21e0) + idx*16`.
- `PushTransform(L, const float xform[3])` is at RVA 0x11c4aa0 and `PopTransform(L)` at 0x11c2850. Each draw command takes
  the draw list's transform index (the u16 at DL+0x3522c), so pushing after the open and popping after the close scales
  everything the scope draws (children, text, later clip rectangles).
- After a pop that empties the stack the game leaves the index in place: EMF sets the u16 at DL+0x3522c and the byte at
  DL+0x352d2 to 0.
- The layer's clip area in its own units is at L+0x21d0 (x, y, w, h). Scaling the vertices instead garbles text.
- Move and scale take only what a scope and its children draw. Moving or scaling `barHudStackParent` (or
  `barGroupParent`) left the hotbar's glass panel and its ALT page marker (`pageDotsParent`) behind; `barPagesContainer`
  and `doActionbar` took them along. So a HUD part must be the outermost scope that holds all its visuals.
- The UI-scale getter at 0x140d46090 is a lerp by resolution, not a scale.

## The vitals' bars and the status list (2026-11-10)

Build 1287369, static and live (private research `hud_parts_2026-11-10`).

- `doPlayerHealthWidget()` (function 0x140c68c60) opens `vitalBars` and calls one bar function (RVA 0xc7fc40,
  `out = bar(out, data, style)`) three times: health (data at widget+0x18, style +0xdbf8) always, then mana (+0xf0,
  +0xdc58) unless a flag hides it, then stamina (+0x1c8, +0xdc28) only while stamina is not full. Each bar opens
  `resourceBarRange` and `resourceBarFill` and draws its end mark itself. The returned `out` is the bar's rect
  `{x, y, w, h}` (health 320x18 at 24.4,33.4, mana 260x12 at 24.4,57.4, a 6-unit gap); the widget keeps the rects in a
  list of at most 4 and, after `vitalBars` closes, draws the dark backing panel from them, one piece per bar, inside its
  own area. The style offsets are colour blocks in `*(*(0x14283f408)+200)` (mana is +0xdc58, stamina +0xdc28).
- Moving `resourceBarRange`/`resourceBarFill` by their position does not work: the game clips them to their old slot and
  the panel stays. What works (the HUD editor's bars): a translation pushed on the layer's transform stack around the
  whole bar call (bar, fill and end mark move together) and the same offset added to the returned rect (that bar's panel
  piece follows while it stays inside the widget's area; far away the bar is drawn without it). EMF finds the function
  through `Sig::PlayerVitalBar`, the widget's three calls (the pattern holds the data and style offsets).
- The status effects are `buffDebuffParent` (the Rested box and `buffDebuffListing`, one row per effect: its icon is an
  `iconFrame` scope, its text is drawn by the list) beside the food column `foodIconParent` (3 icons, 120 units high),
  both in `buffRow`. The list grows downward; a translation on `buffDebuffParent` by `food height - list height` puts it on
  the column's bottom edge so it grows upward. Night away from a fire gives "Exposed", one row 21.6 units high.

## How EMF finds them

Four signatures, optional and client only (`Sig::UiScopeOpen`, `UiScopeClose`, `UiPushTransform`, `UiPopTransform`). Their
patterns hold the offsets the editor uses (the outline flag, the layer type and draw list, the stack count, index stack,
entries and the draw list's index and flag), so a game update that moves one breaks the signature and the editor, or only
its scale, turns off.
