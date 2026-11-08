#include "core/hud_editor/hook.h"

#include <windows.h>

#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>

#include "core/api.h"
#include "core/hud_editor/elements.h"
#include "core/hud_editor/gate.h"
#include "core/hud_editor/layout.h"

namespace emf::core::hud {

namespace {

// ---- state the detours read and write ----
std::atomic<bool> g_on{false};
std::atomic<uint32_t> g_fault{0};
std::atomic<const SnapshotBox<HudSnapshot>*> g_box{nullptr};

struct Live {
    std::atomic<bool> seen{false};
    std::atomic<uint64_t> last_seen_ms{0};
    std::atomic<uint64_t> rect_a{0}, rect_b{0};  // x,y and w,h as float pairs
};
Live g_live[kHudElementCount];
std::atomic<uint64_t> g_ui_space{0};  // w, h as a float pair; 0 = not measured

WroteGuard g_guard[kHudElementCount];  // only the UI thread writes
thread_local ScopeTracker t_tracker;

OpenFn g_o_open = nullptr;
CloseFn g_o_close = nullptr;

uint64_t pack2(float a, float b) { return std::bit_cast<uint64_t>(std::array<float, 2>{a, b}); }
std::array<float, 2> unpack2(uint64_t v) { return std::bit_cast<std::array<float, 2>>(v); }

bool is_on() { return g_on.load(std::memory_order_relaxed) && g_fault.load(std::memory_order_relaxed) == 0; }

const HudSnapshot* current_snapshot() {
    const SnapshotBox<HudSnapshot>* box = g_box.load(std::memory_order_acquire);
    return box ? box->current() : nullptr;
}

// ---- the bodies (no SEH here; the guarded functions below call them) ----
void after_open_body(void* scope, const NameInfo* ni, const HudSnapshot* snap, uint64_t now) {
    if (!ni) return;
    const int e = find_scope(ni->hash);
    if (e == kNoElement) return;
    uint8_t* node = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(scope) + kScopeNode);
    if (!node) return;
    if (e == kUiSpaceIndex) {
        float r[4];
        std::memcpy(r, node + kNodeRect, sizeof r);
        if (std::isfinite(r[2]) && std::isfinite(r[3]) && r[2] > 0 && r[3] > 0)
            g_ui_space.store(pack2(r[2], r[3]), std::memory_order_relaxed);
        return;
    }
    if (!snap || !snap->active || !t_tracker.has_room()) return;
    const ElementEdit& ed = snap->edits[static_cast<size_t>(e)];
    OpenScope os;
    os.scope = scope;
    os.node = node;
    os.element = e;
    if (!ed.hidden && ed.scale != 1.0f) os.push = scale_open(node, ed.scale);  // recorded below: its close pops
    t_tracker.opened(os);
    Live& live = g_live[e];
    live.last_seen_ms.store(now, std::memory_order_relaxed);
    live.seen.store(true, std::memory_order_relaxed);
    if (ed.outline) static_cast<uint8_t*>(scope)[kScopeOutline] = 1;
}

void before_close_body(void* scope, const HudSnapshot* snap, ScopeTracker::Closed* c) {
    const uint8_t* node = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(scope) + kScopeNode);
    *c = t_tracker.closed(scope, node);
    for (int i = 0; i < c->dropped_count; ++i) scale_lost(c->dropped[i].push);
    if (!c->found) return;
    const int e = c->scope.element;
    if (e < 0 || static_cast<size_t>(e) >= kHudElementCount || !node) return;
    float r[4];
    std::memcpy(r, node + kNodeRect, sizeof r);
    g_live[e].rect_a.store(pack2(r[0], r[1]), std::memory_order_relaxed);
    g_live[e].rect_b.store(pack2(r[2], r[3]), std::memory_order_relaxed);
    if (snap && snap->active) {
        const ElementEdit& ed = snap->edits[static_cast<size_t>(e)];
        const UiSpace ui = hud_ui_space();
        apply_position(const_cast<uint8_t*>(node), percent_to_units(ed.x_percent, ui.w), percent_to_units(ed.y_percent, ui.h),
                       ed.hidden, g_guard[e]);
    }
}

void fault(uint32_t code) {
    g_fault.store(code ? code : 0xFFFFFFFFu, std::memory_order_relaxed);
    g_on.store(false, std::memory_order_relaxed);
}

// ---- the guarded functions: plain data only (MSVC C2712) ----
void guarded_after_open(void* scope, const NameInfo* ni, const HudSnapshot* snap, uint64_t now) {
    __try {
        after_open_body(scope, ni, snap, now);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fault(GetExceptionCode());
    }
}

void guarded_before_close(void* scope, const HudSnapshot* snap, ScopeTracker::Closed* c) {
    __try {
        before_close_body(scope, snap, c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fault(GetExceptionCode());
    }
}

void guarded_after_close(const ScalePush* push) {
    __try {
        scale_close(*push);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fault(GetExceptionCode());
    }
}

void* hk_open(void* scope, const NameInfo* ni) { return hud_open_path(scope, ni, g_o_open, GetTickCount64()); }
void hk_close(void* scope) { hud_close_path(scope, g_o_close); }

}  // namespace

// ---- ScopeTracker ----
void ScopeTracker::opened(const OpenScope& s) {
    if (depth_ >= kDepth) return;
    stack_[depth_++] = s;
}

ScopeTracker::Closed ScopeTracker::closed(const void* scope, const void* node) {
    Closed c;
    for (int i = depth_ - 1; i >= 0 && i >= depth_ - 16; --i) {
        if (stack_[i].scope == scope && stack_[i].node == node) {
            c.found = true;
            c.scope = stack_[i];
            for (int j = i + 1; j < depth_ && c.dropped_count < 16; ++j) c.dropped[c.dropped_count++] = stack_[j];
            depth_ = i;
            return c;
        }
    }
    return c;
}

// ---- positions ----
bool apply_position(uint8_t* node, float dx_units, float dy_units, bool hidden, WroteGuard& guard) {
    if (dx_units == 0 && dy_units == 0 && !hidden) {
        guard.valid = false;
        return false;
    }
    float pos[2];
    std::memcpy(pos, node + kNodePos, sizeof pos);
    if (guard.valid && guard.node == node && std::memcmp(&pos[0], &guard.x, sizeof(float)) == 0 &&
        std::memcmp(&pos[1], &guard.y, sizeof(float)) == 0)
        return false;  // the game has not reset the inputs since our write
    pos[0] += dx_units;
    pos[1] += dy_units;
    if (hidden) {
        pos[0] -= kHideShift;
        pos[1] -= kHideShift;
    }
    std::memcpy(node + kNodePos, pos, sizeof pos);
    guard.node = node;
    guard.x = pos[0];
    guard.y = pos[1];
    guard.valid = true;
    return true;
}

LiveInfo hud_live(size_t element) {
    LiveInfo li;
    if (element >= kHudElementCount) return li;
    const Live& l = g_live[element];
    li.seen = l.seen.load(std::memory_order_relaxed);
    li.last_seen_ms = l.last_seen_ms.load(std::memory_order_relaxed);
    const auto a = unpack2(l.rect_a.load(std::memory_order_relaxed)), b = unpack2(l.rect_b.load(std::memory_order_relaxed));
    li.rect[0] = a[0];
    li.rect[1] = a[1];
    li.rect[2] = b[0];
    li.rect[3] = b[1];
    return li;
}

UiSpace hud_ui_space() {
    UiSpace s;
    const uint64_t v = g_ui_space.load(std::memory_order_relaxed);
    if (v == 0) return s;
    const auto wh = unpack2(v);
    s.w = wh[0];
    s.h = wh[1];
    s.measured = true;
    return s;
}

uint32_t hud_fault_code() { return g_fault.load(std::memory_order_relaxed); }

// ---- the detours' bodies ----
void* hud_open_path(void* scope, const NameInfo* ni, OpenFn original, uint64_t now_ms) {
    void* r = original(scope, ni);
    if (is_on()) guarded_after_open(scope, ni, current_snapshot(), now_ms);
    return r;
}

void hud_close_path(void* scope, CloseFn original) {
    ScopeTracker::Closed c;
    if (t_tracker.depth() > 0)  // also while off: a pushed transform is still popped
        guarded_before_close(scope, is_on() ? current_snapshot() : nullptr, &c);
    original(scope);
    if (c.found && c.scope.push.pushed) guarded_after_close(&c.scope.push);
}

void hud_set_for_tests(const SnapshotBox<HudSnapshot>* box, bool on) {
    g_box.store(box, std::memory_order_release);
    g_on.store(on, std::memory_order_relaxed);
}

void hud_reset_live_for_tests() {
    for (auto& l : g_live) {
        l.seen = false;
        l.last_seen_ms = 0;
        l.rect_a = 0;
        l.rect_b = 0;
    }
    g_ui_space = 0;
    for (auto& g : g_guard) g = WroteGuard{};
    t_tracker.clear();
    g_fault = 0;
    g_on = false;
    g_box = nullptr;
}

bool install_hud_hooks(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index,
                       const SnapshotBox<HudSnapshot>* box, std::string* why) {
    auto fail = [&](const char* m) {
        if (why) *why = m;
        return false;
    };
    if (!hud_signatures_ready(sigs, known_build, build_index)) return fail("the UI scope signatures are not usable on this game build");
    if (hud_scale_ready(sigs, known_build, build_index)) {
        set_transform_functions(reinterpret_cast<PushXformFn>(sigs[static_cast<size_t>(Sig::UiPushTransform)].addr),
                                reinterpret_cast<PopXformFn>(sigs[static_cast<size_t>(Sig::UiPopTransform)].addr));
    } else {
        set_transform_functions(nullptr, nullptr);
    }
    // The close first, the open second; hooks are never removed, and the close alone only passes through.
    if (!install_emf_hook("UiScopeClose (HUD editor)", sigs[static_cast<size_t>(Sig::UiScopeClose)].addr,
                          reinterpret_cast<void*>(&hk_close), reinterpret_cast<void**>(&g_o_close)))
        return fail("the UI scope close could not be hooked");
    if (!install_emf_hook("UiScopeOpen (HUD editor)", sigs[static_cast<size_t>(Sig::UiScopeOpen)].addr,
                          reinterpret_cast<void*>(&hk_open), reinterpret_cast<void**>(&g_o_open)))
        return fail("the UI scope open could not be hooked");
    g_box.store(box, std::memory_order_release);
    g_on.store(true, std::memory_order_relaxed);
    return true;
}

}  // namespace emf::core::hud
