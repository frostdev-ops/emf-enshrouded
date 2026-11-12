// Markers step 3b (minimap_draw.h).
#include "core/minimap_draw.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <mutex>

#include "common/fnv.h"
#include "common/seh.h"
#include "core/api.h"
#include "core/canvas.h"
#include "core/hud_editor/scale.h"
#include "core/marker_capture.h"

namespace emf::core {

namespace {

constexpr size_t kViewBytes = 0x270;
constexpr uint32_t kWorldW = 0x36e9d0, kWorldH = 0x36e9d4, kShaderBlock = 0x36e9f0;

MapCanvasFn g_canvas = nullptr;
UiPushClipFn g_push = nullptr;
UiPopClipFn g_pop = nullptr;
hud::OpenFn g_open = nullptr;
hud::CloseFn g_close = nullptr;
uint8_t* const* g_frame_global = nullptr;  // the UI frame context's global
std::atomic<int> g_state{0};               // 0 not set up, 1 on, -1 off after a fault
std::mutex g_status_m;
std::string g_status = "not set up";
std::atomic<uint64_t> g_shown_ms{0};

void set_status(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_status_m);
    g_status = s;
}

// Every read the canvas makes without its own check, checked here (research 7).
bool ui_ready(float* world_w, float* world_h) {
    uint8_t* frame = nullptr;
    uint8_t* const* root = nullptr;
    uint8_t* root0 = nullptr;
    uint8_t* upd = nullptr;
    uint8_t* sh = nullptr;
    void* sh8 = nullptr;
    if (!g_frame_global || !seh_read(g_frame_global, &frame, 8) || !frame || !seh_read(frame + 0xd0, &root, 8) || !root ||
        !seh_read(root, &root0, 8) || !root0 || !seh_read(root0 + 0x38, &upd, 8) || !upd ||
        !seh_read(upd + kWorldW, world_w, 4) || !seh_read(upd + kWorldH, world_h, 4) ||
        !seh_read(upd + kShaderBlock, &sh, 8) || !sh || !seh_read(sh + 8, &sh8, 8) || !sh8)
        return false;
    return *world_w > 100 && *world_h > 100 && *world_w < 1e6f && *world_h < 1e6f;
}

// The canvas call itself under SEH (a fault turns the map drawing off for the session).
bool call_canvas(void* scope, const void* view) {
    __try {
        g_canvas(scope, view);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool call_push(void* layer, const float* rect) {
    __try {
        g_push(layer, rect, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool call_pop(void* layer) {
    __try {
        g_pop(layer);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// One UI node name per canvas (opening the same name twice under one parent in a pass was never checked).
const hud::NameInfo& node_name(size_t i) {
    static const auto names = [] {
        struct Names {
            char text[kCanvasMax][16];
            hud::NameInfo ni[kCanvasMax];
        } n{};
        for (size_t k = 0; k < kCanvasMax; ++k) {
            std::snprintf(n.text[k], sizeof n.text[k], "emfMinimap%zu", k);
            const size_t len = std::strlen(n.text[k]);
            n.ni[k] = hud::NameInfo{n.text[k], len, fnv1a64(std::string_view(n.text[k], len))};
        }
        return n;
    }();
    return names.ni[i < kCanvasMax ? i : 0];
}

void draw_one(size_t index, const EmfCanvas& spec, const CanvasCmd& m, float ui_w, float ui_h, float world_w,
              float world_h) {
    const MapDrawPlan p = plan_map_draw(spec, ui_w, ui_h, m.a[0], m.a[1], m.a[2], world_w, world_h);
    if (!p.ok) return;
    const hud::NameInfo& ni = node_name(index);
    alignas(16) uint8_t scope[0x40] = {};
    g_open(scope, &ni);  // the trampoline: EMF's own detour does not see it
    uint8_t* node = nullptr;
    std::memcpy(&node, scope + hud::kScopeNode, sizeof node);
    uint8_t* layer = node ? hud::layer_of(node) : nullptr;
    if (node && layer) {
        alignas(16) uint8_t view[kViewBytes] = {};
        std::memcpy(view + 0x10, &p.s, 4);
        std::memcpy(view + 0x14, &p.s, 4);
        std::memcpy(view + 0x38, &world_w, 4);
        std::memcpy(view + 0x3c, &world_h, 4);
        std::memcpy(view + 0x58, &p.base, 4);
        float saved[4];
        std::memcpy(saved, node + hud::kNodeRect, sizeof saved);
        std::memcpy(node + hud::kNodeRect, p.cv, sizeof p.cv);
        bool ok = call_push(layer, p.vp);
        if (ok) {
            ok = call_canvas(scope, view);
            if (!call_pop(layer)) ok = false;
        }
        std::memcpy(node + hud::kNodeRect, saved, sizeof saved);
        if (!ok) {
            g_state.store(-1);
            set_status("off: the game's map canvas faulted (turned off until the game restarts)");
        }
    }
    g_close(scope);
}

}  // namespace

void minimap_draw_setup(MapCanvasFn canvas, UiPushClipFn push, UiPopClipFn pop, hud::OpenFn open, hud::CloseFn close) {
    g_canvas = canvas;
    g_push = push;
    g_pop = pop;
    g_open = open;
    g_close = close;
    g_frame_global = nullptr;
    if (canvas) {  // mov r8, [rip + disp32] at +0x1e (4C 8B 05 disp32; the next instruction at +0x25)
        const auto* code = reinterpret_cast<const uint8_t*>(canvas);
        uint8_t op[3];
        int32_t disp = 0;
        if (seh_read(code + 0x1e, op, 3) && op[0] == 0x4C && op[1] == 0x8B && op[2] == 0x05 &&
            seh_read(code + 0x21, &disp, 4))
            g_frame_global = reinterpret_cast<uint8_t* const*>(code + 0x25 + disp);
    }
    const bool ok = g_canvas && g_push && g_pop && g_open && g_close && g_frame_global;
    g_state.store(ok ? 1 : 0);
    set_status(ok ? "on" : "off: the map canvas, the UI clip or the scope functions are not usable on this game build");
}

bool minimap_draw_ready() { return g_state.load() == 1; }

void minimap_draw_in_play_ui() {
    if (g_state.load(std::memory_order_relaxed) != 1) return;
    CoreState* s = core_state();
    if (!s) return;
    const auto view = s->minimap.canvases.view();
    if (!view || view->empty()) return;
    const uint64_t now = GetTickCount64();
    const MarkerSight sight = marker_sight(now);
    if (sight.map_open || !sight.world_ui) return;  // the game's own map is open, or a menu: not now
    if (!minimap_shown_recently(now)) return;       // the overlay does not show the panels (the console, no world)
    float ui_w = 0, ui_h = 0;  // the UI as laid out (its width follows the screen's shape), as the overlay sees it
    if (!marker_ui_size(&ui_w, &ui_h)) return;
    float world_w = 0, world_h = 0;
    if (!ui_ready(&world_w, &world_h)) return;
    size_t index = 0;
    for (const Canvas& c : *view) {
        const size_t i = index++;
        if (c.spec.hidden) continue;
        for (const CanvasCmd& m : c.cmds)
            if (m.kind == EMF_CANVAS_MAP) {
                draw_one(i, c.spec, m, ui_w, ui_h, world_w, world_h);
                if (g_state.load(std::memory_order_relaxed) != 1) return;
            }
    }
}

void minimap_note_shown(uint64_t now_ms) { g_shown_ms.store(now_ms, std::memory_order_relaxed); }

bool minimap_shown_recently(uint64_t now_ms) {
    const uint64_t t = g_shown_ms.load(std::memory_order_relaxed);
    return t != 0 && (now_ms <= t || now_ms - t <= kMinimapShownMs);
}

void minimap_shown_reset_for_tests() { g_shown_ms.store(0); }

std::string minimap_draw_status() {
    std::lock_guard<std::mutex> lock(g_status_m);
    return g_status;
}

}  // namespace emf::core
