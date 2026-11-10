#include "core/hud_editor/scale.h"

#include <atomic>
#include <cmath>
#include <cstring>

#include "core/hud_editor/layout.h"

namespace emf::core::hud {

namespace {

std::atomic<PushXformFn> g_push{nullptr};
std::atomic<PopXformFn> g_pop{nullptr};
std::atomic<bool> g_off{false};
std::atomic<uint64_t> g_pushes{0}, g_pops{0}, g_mismatches{0};

template <class T>
T load(const uint8_t* base, size_t off) {
    T v;
    std::memcpy(&v, base + off, sizeof v);
    return v;
}
template <class T>
void store(uint8_t* base, size_t off, T v) {
    std::memcpy(base + off, &v, sizeof v);
}

bool all_finite(const float* v, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(v[i])) return false;
    return true;
}

}  // namespace

Anchor auto_anchor(const float rect[4], const float area[4]) {
    Anchor a;
    if (!all_finite(rect, 4) || !all_finite(area, 4) || !(area[2] > 1.0f) || !(area[3] > 1.0f)) return a;
    const float fx = (rect[0] + rect[2] * 0.5f - area[0]) / area[2];
    const float fy = (rect[1] + rect[3] * 0.5f - area[1]) / area[3];
    a.ax = fx < 1.0f / 3 ? 0.0f : fx > 2.0f / 3 ? 1.0f : 0.5f;
    a.ay = fy < 1.0f / 3 ? 0.0f : fy > 2.0f / 3 ? 1.0f : 0.5f;
    return a;
}

void anchor_point(const float rect[4], Anchor a, float* ox, float* oy) {
    *ox = rect[0] + rect[2] * a.ax;
    *oy = rect[1] + rect[3] * a.ay;
}

Xform scaled_about(const Xform& top, float s, float ox, float oy) {
    return Xform{top.scale * s, top.tx + top.scale * ox * (1 - s), top.ty + top.scale * oy * (1 - s)};
}

bool usable_top(const Xform& t) { return t.scale > 0 && std::isfinite(t.scale) && std::isfinite(t.tx) && std::isfinite(t.ty); }

PopCheck check_pop(uint64_t count_now, uint64_t depth_at_push) {
    return count_now == depth_at_push + 1 ? PopCheck::Pop : PopCheck::Mismatch;
}

uint8_t* layer_of(uint8_t* node) {
    if (!node) return nullptr;
    const uint8_t* window = load<const uint8_t*>(node, kNodeWindow);
    if (!window) return nullptr;
    uint8_t* layer = load<uint8_t*>(window, kWindowLayer);
    if (!layer || load<int>(layer, kLayerType) != kLayerTypeUi) return nullptr;
    if (!load<const uint8_t*>(layer, kLayerDrawList)) return nullptr;
    return layer;
}

bool read_top(const uint8_t* layer, Xform* top, uint64_t* depth) {
    const uint64_t count = load<uint64_t>(layer, kLayerXformCount);
    *depth = count;
    *top = Xform{};
    if (count == 0) return true;
    if (count > 0xFFFF) return false;  // the index stack holds u16 entries
    const uint8_t* index = load<const uint8_t*>(layer, kLayerXformIndex);
    const uint8_t* entries = load<const uint8_t*>(layer, kLayerXforms);
    if (!index || !entries) return false;
    const uint16_t idx = load<uint16_t>(index, (count - 1) * sizeof(uint16_t));
    const uint8_t* e = entries + static_cast<size_t>(idx) * kXformEntrySize;
    Xform t{load<float>(e, 0), load<float>(e, 4), load<float>(e, 8)};
    if (!usable_top(t)) return false;
    *top = t;
    return true;
}

void clear_emptied_index(uint8_t* layer) {
    uint8_t* dl = load<uint8_t*>(layer, kLayerDrawList);
    if (!dl) return;
    store<uint16_t>(dl, kDlXformIndex, 0);
    store<uint8_t>(dl, kDlXformFlag, 0);
}

void set_transform_functions(PushXformFn push, PopXformFn pop) {
    g_push.store(push, std::memory_order_release);
    g_pop.store(pop, std::memory_order_release);
}

ScalePush scale_open(uint8_t* node, float s) {
    if (s == 1.0f || !(s >= kScaleMin && s <= kScaleMax)) return {};
    const PushXformFn push = g_push.load(std::memory_order_acquire);
    if (!push || !g_pop.load(std::memory_order_acquire) || g_off.load(std::memory_order_relaxed)) return {};
    uint8_t* layer = layer_of(node);
    if (!layer) return {};
    float rect[4], area[4];
    std::memcpy(rect, node + 0x114, sizeof rect);  // last frame's layout: stable from frame to frame
    if (!all_finite(rect, 4)) return {};
    Xform top;
    uint64_t depth = 0;
    if (!read_top(layer, &top, &depth)) return {};
    std::memcpy(area, layer + kLayerClip, sizeof area);
    float ox = 0, oy = 0;
    anchor_point(rect, auto_anchor(rect, area), &ox, &oy);
    const Xform x = scaled_about(top, s, ox, oy);
    const float xf[3] = {x.scale, x.tx, x.ty};
    push(layer, xf);
    g_pushes.fetch_add(1, std::memory_order_relaxed);
    return ScalePush{layer, depth, true};
}

ScalePush shift_open(uint8_t* node, float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return {};
    const PushXformFn push = g_push.load(std::memory_order_acquire);
    if (!push || !g_pop.load(std::memory_order_acquire) || g_off.load(std::memory_order_relaxed)) return {};
    uint8_t* layer = layer_of(node);
    if (!layer) return {};
    Xform top;
    uint64_t depth = 0;
    if (!read_top(layer, &top, &depth)) return {};
    const float xf[3] = {top.scale, top.tx + top.scale * dx, top.ty + top.scale * dy};
    push(layer, xf);
    g_pushes.fetch_add(1, std::memory_order_relaxed);
    return ScalePush{layer, depth, true};
}

void scale_close(const ScalePush& p) {
    if (!p.pushed || !p.layer) return;
    const PopXformFn pop = g_pop.load(std::memory_order_acquire);
    if (!pop || check_pop(load<uint64_t>(p.layer, kLayerXformCount), p.depth) == PopCheck::Mismatch) {
        g_off.store(true, std::memory_order_relaxed);
        g_mismatches.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    pop(p.layer);
    g_pops.fetch_add(1, std::memory_order_relaxed);
    if (p.depth == 0) clear_emptied_index(p.layer);
}

void scale_lost(const ScalePush& p) {
    if (!p.pushed) return;
    g_off.store(true, std::memory_order_relaxed);
    g_mismatches.fetch_add(1, std::memory_order_relaxed);
}

ScaleState hud_scale_state() {
    ScaleState st;
    st.available = g_push.load(std::memory_order_acquire) != nullptr && g_pop.load(std::memory_order_acquire) != nullptr;
    st.off = g_off.load(std::memory_order_relaxed);
    st.pushes = g_pushes.load(std::memory_order_relaxed);
    st.pops = g_pops.load(std::memory_order_relaxed);
    st.mismatches = g_mismatches.load(std::memory_order_relaxed);
    return st;
}

void scale_reset_for_tests() {
    g_push.store(nullptr);
    g_pop.store(nullptr);
    g_off.store(false);
    g_pushes.store(0);
    g_pops.store(0);
    g_mismatches.store(0);
}

}  // namespace emf::core::hud
