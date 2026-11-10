// The HUD editor's UI scope detours, against fake scopes, nodes and a fake UI layer (no game, no MinHook).
#include <doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "common/fnv.h"
#include "core/hud_editor/bars.h"
#include "core/hud_editor/hook.h"

using namespace emf::core::hud;

namespace {

template <class T>
void put(uint8_t* mem, size_t off, T v) {
    std::memcpy(mem + off, &v, sizeof v);
}
template <class T>
T get(const uint8_t* mem, size_t off) {
    T v;
    std::memcpy(&v, mem + off, sizeof v);
    return v;
}

struct Node {
    alignas(16) uint8_t scope[0x28]{};
    alignas(16) uint8_t node[0x170]{};
    Node() { put<uint8_t*>(scope, kScopeNode, node); }
    void set_pos(float x, float y) {
        put<float>(node, kNodePos, x);
        put<float>(node, kNodePos + 4, y);
    }
    float x() const { return get<float>(node, kNodePos); }
    float y() const { return get<float>(node, kNodePos + 4); }
    void set_rect(float x, float y, float w, float h) {
        put<float>(node, kNodeRect, x);
        put<float>(node, kNodeRect + 4, y);
        put<float>(node, kNodeRect + 8, w);
        put<float>(node, kNodeRect + 12, h);
    }
};

// A fake UI layer with a transform stack, shared by the nodes that point at it.
struct Layer {
    std::vector<uint8_t> window = std::vector<uint8_t>(0x20), layer = std::vector<uint8_t>(0x6ce8),
                         dl = std::vector<uint8_t>(0x352d8);
    float table[16 * 4] = {};
    uint16_t index[16] = {};
    int pushes = 0, pops = 0;
    Layer() {
        put<uint8_t*>(window.data(), kWindowLayer, layer.data());
        put<int>(layer.data(), kLayerType, kLayerTypeUi);
        put<uint8_t*>(layer.data(), kLayerDrawList, dl.data());
        put<uint8_t*>(layer.data(), kLayerXforms, reinterpret_cast<uint8_t*>(table));
        put<uint8_t*>(layer.data(), kLayerXformIndex, reinterpret_cast<uint8_t*>(index));
        put<uint64_t>(layer.data(), kLayerXformCount, 0);
        put<float>(layer.data(), kLayerClip + 8, 2084);
        put<float>(layer.data(), kLayerClip + 12, 1080);
    }
    void attach(Node& n) { put<uint8_t*>(n.node, kNodeWindow, window.data()); }
    uint64_t count() const { return get<uint64_t>(layer.data(), kLayerXformCount); }
};
Layer* g_layer = nullptr;
void fake_push(void*, const float* xf) {
    Layer& l = *g_layer;
    ++l.pushes;
    const uint64_t c = l.count();
    l.table[c * 4] = xf[0];
    l.table[c * 4 + 1] = xf[1];
    l.table[c * 4 + 2] = xf[2];
    l.index[c] = static_cast<uint16_t>(c);
    put<uint64_t>(l.layer.data(), kLayerXformCount, c + 1);
}
void fake_pop(void*) {
    ++g_layer->pops;
    put<uint64_t>(g_layer->layer.data(), kLayerXformCount, g_layer->count() - 1);
}

void* no_open(void*, const NameInfo*) { return nullptr; }
void no_close(void*) {}

NameInfo name_of(const char* s) { return NameInfo{s, std::strlen(s), emf::fnv1a64(s)}; }

void reset() {
    hud_reset_live_for_tests();
    scale_reset_for_tests();
}

// A fake bar function: writes the bar's rect into out (bar 0's for a call it does not know) and records the top of the
// layer's transform stack at the call.
struct BarCallRec {
    int k;
    float scale, tx, ty;  // the stack's top when the original ran (identity when empty)
};
std::vector<BarCallRec> g_bar_calls;
const float kBarRects[3][4] = {{24.4f, 33.4f, 320, 18}, {24.4f, 57.4f, 260, 12}, {24.4f, 75.4f, 200, 12}};
constexpr uintptr_t kWidget = 0x100000;
void* fake_bar(void* out, void* data, void*) {
    const int k = bar_index(reinterpret_cast<uintptr_t>(data), kWidget);
    BarCallRec rec{k, 1, 0, 0};
    if (g_layer && g_layer->count() > 0) {
        const uint64_t c = g_layer->count() - 1;
        rec.scale = g_layer->table[c * 4];
        rec.tx = g_layer->table[c * 4 + 1];
        rec.ty = g_layer->table[c * 4 + 2];
    }
    g_bar_calls.push_back(rec);
    std::memcpy(out, kBarRects[k >= 0 ? k : 0], 16);
    return out;
}
void* bar_data(int k) { return reinterpret_cast<void*>(kWidget + kBarDataFirst + static_cast<uintptr_t>(k) * kBarDataStride); }
void call_bar_data(void* data, const HudSnapshot* snap, float out[4]) {
    bar_call_path(out, data, nullptr, &fake_bar, snap, UiSpace{2084.2f, 1080.0f, true}, 5);
}
void call_bar(int k, const HudSnapshot* snap, float out[4]) { call_bar_data(bar_data(k), snap, out); }

// The test's layer, transform functions and an active snapshot published and switched on.
struct BarRig {
    Layer layer;
    SnapshotBox<HudSnapshot> box;
    Node vitals;
    BarRig(const HudSnapshot& s) {
        reset();
        g_bar_calls.clear();
        g_layer = &layer;
        set_transform_functions(&fake_push, &fake_pop);
        box.publish(s, 0);
        hud_set_for_tests(&box, true);
        layer.attach(vitals);
    }
    ~BarRig() { hud_set_for_tests(nullptr, false); }
    const HudSnapshot* snap() const { return box.current(); }
};

void measure(float w, float h) {
    Node ui;
    ui.set_rect(0, 0, w, h);
    const NameInfo ni = name_of("doPlayStateUi()");
    hud_open_path(ui.scope, &ni, no_open, 1);
}

void open(Node& n, const char* scope) {
    const NameInfo ni = name_of(scope);
    hud_open_path(n.scope, &ni, no_open, 1000);
}
void close(Node& n) { hud_close_path(n.scope, no_close); }

HudSnapshot active() {
    HudSnapshot s;
    s.active = true;
    return s;
}

}  // namespace

TEST_CASE("apply_position: adds the offset to the position the game set, once per reset") {
    Node n;
    WroteGuard g;
    n.set_pos(20, 20);
    CHECK(apply_position(n.node, 10, -5, false, g));
    CHECK(n.x() == 30.0f);
    CHECK(n.y() == 15.0f);
    CHECK_FALSE(apply_position(n.node, 10, -5, false, g));
    CHECK(n.x() == 30.0f);
    CHECK(n.y() == 15.0f);
    n.set_pos(20, 20);  // the game resets the inputs
    CHECK(apply_position(n.node, 10, -5, false, g));
    CHECK(n.x() == 30.0f);
    CHECK(n.y() == 15.0f);
}

TEST_CASE("apply_position: hidden moves the part 20000 up and left") {
    Node n;
    WroteGuard g;
    n.set_pos(20, 20);
    CHECK(apply_position(n.node, 0, 0, true, g));
    CHECK(n.x() == -19980.0f);
    CHECK(n.y() == -19980.0f);
}

TEST_CASE("apply_position: a default edit writes nothing") {
    Node n;
    WroteGuard g;
    n.set_pos(20, 20);
    CHECK(apply_position(n.node, 5, 5, false, g));
    CHECK(g.valid);
    n.set_pos(20, 20);
    CHECK_FALSE(apply_position(n.node, 0, 0, false, g));
    CHECK_FALSE(g.valid);
    CHECK(n.x() == 20.0f);
    CHECK(n.y() == 20.0f);
}

TEST_CASE("apply_position: another node is never skipped") {
    Node a, b;
    WroteGuard g;
    a.set_pos(20, 20);
    b.set_pos(20, 20);
    CHECK(apply_position(a.node, 10, 10, false, g));
    CHECK(apply_position(b.node, 10, 10, false, g));
    CHECK(b.x() == 30.0f);
}

TEST_CASE("ScopeTracker: a close matches its open, nested") {
    ScopeTracker t;
    int a, b, na, nb;
    t.opened({&a, &na, 0, {}});
    t.opened({&b, &nb, 5, {}});
    CHECK(t.depth() == 2);
    const auto cb = t.closed(&b, &nb);
    CHECK(cb.found);
    CHECK(cb.scope.element == 5);
    const auto ca = t.closed(&a, &na);
    CHECK(ca.found);
    CHECK(ca.scope.element == 0);
    CHECK(t.depth() == 0);
}

TEST_CASE("ScopeTracker: an open that never closed is dropped, and reported, when an outer scope closes") {
    ScopeTracker t;
    int a, b, na, nb;
    uint8_t layer[1];
    t.opened({&a, &na, 0, {}});
    t.opened({&b, &nb, 1, {layer, 3, true}});
    const auto c = t.closed(&a, &na);
    CHECK(c.found);
    CHECK(c.dropped_count == 1);
    CHECK(c.dropped[0].push.pushed);
    CHECK(c.dropped[0].element == 1);
    CHECK(t.depth() == 0);
}

TEST_CASE("ScopeTracker: the same scope address with another node is not ours; unmanaged closes do nothing") {
    ScopeTracker t;
    int a, n1, n2, other;
    t.opened({&a, &n1, 0, {}});
    CHECK_FALSE(t.closed(&a, &n2).found);
    CHECK_FALSE(t.closed(&other, &n1).found);
    CHECK(t.depth() == 1);
    CHECK(t.closed(&a, &n1).found);
}

TEST_CASE("ScopeTracker: full at 32; has_room says so") {
    ScopeTracker t;
    int s[ScopeTracker::kDepth + 1];
    for (int i = 0; i < ScopeTracker::kDepth; ++i) {
        CHECK(t.has_room());
        t.opened({&s[i], &s[i], i, {}});
    }
    CHECK_FALSE(t.has_room());
    t.opened({&s[ScopeTracker::kDepth], &s[ScopeTracker::kDepth], 99, {}});  // ignored
    CHECK(t.depth() == ScopeTracker::kDepth);
}

TEST_CASE("UI scopes: a managed part is moved at close, outlined at open; inactive or unmanaged scopes are untouched") {
    reset();
    SnapshotBox<HudSnapshot> box;
    hud_set_for_tests(&box, true);
    measure(2084.2f, 1080);
    CHECK(hud_ui_space().measured);
    CHECK(hud_ui_space().w == doctest::Approx(2084.2f));
    CHECK(hud_ui_space().h == doctest::Approx(1080.0f));

    HudSnapshot s = active();
    s.edits[0].x_percent = 10;
    s.edits[0].y_percent = -5;
    s.edits[0].outline = true;
    box.publish(s, 0);
    hud_set_for_tests(&box, true);

    Node n;
    n.set_pos(20, 20);
    n.set_rect(20, 20, 341, 188);
    open(n, "playerStatusParent");
    CHECK(get<uint8_t>(n.scope, kScopeOutline) == 1);
    CHECK(hud_live(0).seen);
    CHECK(hud_live(0).last_seen_ms == 1000);
    close(n);
    CHECK(n.x() == doctest::Approx(228.42f));
    CHECK(n.y() == doctest::Approx(-34.0f));
    CHECK(hud_live(0).rect[2] == doctest::Approx(341.0f));

    // not active: untouched
    HudSnapshot off;
    box.publish(off, 5000);
    Node m;
    m.set_pos(20, 20);
    open(m, "playerStatusParent");
    CHECK(get<uint8_t>(m.scope, kScopeOutline) == 0);
    close(m);
    CHECK(m.x() == 20.0f);

    // active but not a part
    box.publish(s, 9000);
    Node u;
    u.set_pos(20, 20);
    open(u, "vitalBars");
    close(u);
    CHECK(u.x() == 20.0f);
    CHECK(get<uint8_t>(u.scope, kScopeOutline) == 0);
    hud_set_for_tests(nullptr, false);
}

TEST_CASE("UI space follows the measured rect") {  // Review Focus 4
    reset();
    SnapshotBox<HudSnapshot> box;
    HudSnapshot s = active();
    s.edits[0].x_percent = 10;
    s.edits[0].y_percent = 10;
    box.publish(s, 0);
    hud_set_for_tests(&box, true);
    measure(2084.2f, 1080);
    Node n;
    n.set_pos(20, 20);
    open(n, "playerStatusParent");
    close(n);
    CHECK(n.x() == doctest::Approx(20 + 208.42f));
    CHECK(n.y() == doctest::Approx(20 + 108.0f));
    measure(2560, 1440);
    n.set_pos(20, 20);
    open(n, "playerStatusParent");
    close(n);
    CHECK(n.x() == doctest::Approx(20 + 256.0f));
    CHECK(n.y() == doctest::Approx(20 + 144.0f));
    hud_set_for_tests(nullptr, false);
}

TEST_CASE("UI scopes: a scaled part pushes at open and pops at close, once each") {
    reset();
    Layer layer;
    g_layer = &layer;
    set_transform_functions(&fake_push, &fake_pop);
    SnapshotBox<HudSnapshot> box;
    HudSnapshot s = active();
    s.edits[0].scale = 1.5f;
    box.publish(s, 0);
    hud_set_for_tests(&box, true);
    Node n;
    layer.attach(n);
    n.set_rect(20, 20, 341.5f, 187.8f);
    open(n, "playerStatusParent");
    CHECK(layer.pushes == 1);
    CHECK(layer.count() == 1);
    close(n);
    CHECK(layer.pops == 1);
    CHECK(layer.count() == 0);
    CHECK(hud_scale_state().mismatches == 0);
    hud_set_for_tests(nullptr, false);
}

TEST_CASE("UI scopes: a hidden part, or scale 1, pushes nothing") {
    reset();
    Layer layer;
    g_layer = &layer;
    set_transform_functions(&fake_push, &fake_pop);
    SnapshotBox<HudSnapshot> box;
    HudSnapshot s = active();
    s.edits[0].scale = 1.5f;
    s.edits[0].hidden = true;
    s.edits[1].scale = 1.0f;
    s.edits[1].x_percent = 3;
    box.publish(s, 0);
    hud_set_for_tests(&box, true);
    Node n, b;
    layer.attach(n);
    layer.attach(b);
    n.set_rect(20, 20, 341.5f, 187.8f);
    b.set_rect(20, 20, 100, 50);
    open(n, "playerStatusParent");
    close(n);
    open(b, "buffRow");
    close(b);
    CHECK(layer.pushes == 0);
    CHECK(layer.pops == 0);
    hud_set_for_tests(nullptr, false);
}

TEST_CASE("UI scopes: a pushed transform is popped even after the editor turned off") {
    reset();
    Layer layer;
    g_layer = &layer;
    set_transform_functions(&fake_push, &fake_pop);
    SnapshotBox<HudSnapshot> box;
    HudSnapshot s = active();
    s.edits[0].scale = 1.5f;
    box.publish(s, 0);
    hud_set_for_tests(&box, true);
    Node n;
    layer.attach(n);
    n.set_rect(20, 20, 341.5f, 187.8f);
    open(n, "playerStatusParent");
    REQUIRE(layer.pushes == 1);
    hud_set_for_tests(&box, false);
    close(n);
    CHECK(layer.pops == 1);
    CHECK(layer.count() == 0);
    hud_set_for_tests(nullptr, false);
}

TEST_CASE("UI scopes: an unmanaged close pops nothing") {
    reset();
    Layer layer;
    g_layer = &layer;
    set_transform_functions(&fake_push, &fake_pop);
    SnapshotBox<HudSnapshot> box;
    HudSnapshot s = active();
    s.edits[0].scale = 1.5f;
    box.publish(s, 0);
    hud_set_for_tests(&box, true);
    Node n, other;
    layer.attach(n);
    layer.attach(other);
    n.set_rect(20, 20, 341.5f, 187.8f);
    open(n, "playerStatusParent");
    open(other, "somethingElse");
    close(other);  // not ours: nothing popped
    CHECK(layer.pops == 0);
    close(n);
    CHECK(layer.pops == 1);
    hud_set_for_tests(nullptr, false);
}

TEST_CASE("UI scopes: an inner scaled scope that never closed turns scale off, and nothing extra is popped") {
    reset();
    Layer layer;
    g_layer = &layer;
    set_transform_functions(&fake_push, &fake_pop);
    SnapshotBox<HudSnapshot> box;
    HudSnapshot s = active();
    s.edits[0].scale = 1.5f;
    s.edits[1].scale = 1.5f;
    box.publish(s, 0);
    hud_set_for_tests(&box, true);
    Node vitals, buffs;
    layer.attach(vitals);
    layer.attach(buffs);
    vitals.set_rect(20, 20, 341.5f, 187.8f);
    buffs.set_rect(30, 150, 200, 40);
    open(vitals, "playerStatusParent");
    open(buffs, "buffRow");
    REQUIRE(layer.pushes == 2);
    close(vitals);  // buffs never closes
    CHECK(hud_scale_state().off);
    CHECK(layer.pops == 0);
    hud_set_for_tests(nullptr, false);
}

TEST_CASE("UI scopes: a fault in a detour is caught, recorded and switches the hooks' work off") {
    reset();
    SnapshotBox<HudSnapshot> box;
    box.publish(active(), 0);
    hud_set_for_tests(&box, true);
    Node bad;
    put<uint8_t*>(bad.scope, kScopeNode, reinterpret_cast<uint8_t*>(0x10));
    const NameInfo ni = name_of("doPlayStateUi()");
    hud_open_path(bad.scope, &ni, no_open, 1);
    CHECK(hud_fault_code() == 0xC0000005u);
    // off now: a second open does nothing
    Node good;
    good.set_rect(0, 0, 2084.2f, 1080);
    hud_open_path(good.scope, &ni, no_open, 2);
    CHECK_FALSE(hud_ui_space().measured);
    hud_set_for_tests(nullptr, false);
}

TEST_CASE("install_hud_hooks: refuses where the signatures are not ready, and hooks nothing") {
    std::array<emf::Resolved, emf::kSigCount> sigs{};
    std::string why;
    SnapshotBox<HudSnapshot> box;
    CHECK_FALSE(install_hud_hooks(sigs, true, 7, &box, &why));
    CHECK(why == "the UI scope signatures are not usable on this game build");
}

TEST_CASE("bar_call_path: outside vitalBars nothing changes") {  // Review Focus 2
    HudSnapshot s = active();
    s.edits[static_cast<size_t>(bar_element(kBarMana))].y_percent = 10;
    BarRig r(s);
    float o[4];
    call_bar(1, r.snap(), o);
    CHECK(r.layer.pushes == 0);
    CHECK(o[1] == doctest::Approx(57.4f));
    CHECK_FALSE(bar_live(kBarMana).seen);
}

TEST_CASE("bar_call_path: a moved bar is drawn shifted and its rect follows") {
    HudSnapshot s = active();
    s.edits[static_cast<size_t>(bar_element(kBarMana))].y_percent = 10;  // 10 % of 1080 = 108 units
    BarRig r(s);
    open(r.vitals, "vitalBars");
    float o[4];
    call_bar(0, r.snap(), o);
    CHECK(r.layer.pushes == 0);  // health is not moved
    CHECK(o[1] == doctest::Approx(33.4f));
    call_bar(1, r.snap(), o);
    CHECK(r.layer.pushes == 1);
    CHECK(r.layer.pops == 1);
    REQUIRE(g_bar_calls.size() == 2);
    CHECK(g_bar_calls[1].ty == doctest::Approx(108));
    CHECK(g_bar_calls[1].tx == doctest::Approx(0));
    CHECK(o[1] == doctest::Approx(57.4f + 108));  // the panel piece follows
    CHECK(o[0] == doctest::Approx(24.4f));
    close(r.vitals);
    const LiveInfo li = bar_live(kBarMana);
    CHECK(li.seen);
    CHECK(li.rect[1] == doctest::Approx(57.4f + 108));
    CHECK(li.rect[2] == doctest::Approx(260));
    CHECK(hud_live(static_cast<size_t>(bar_element(kBarMana))).rect[1] == doctest::Approx(57.4f + 108));
}

TEST_CASE("bar_call_path: the order swaps health and mana from last frame's rects") {
    BarRig r(active());
    float o[4];
    open(r.vitals, "vitalBars");  // frame 1: the default order
    call_bar(0, r.snap(), o);
    call_bar(1, r.snap(), o);
    close(r.vitals);
    CHECK(r.layer.pushes == 0);
    HudSnapshot s = active();
    s.order = {1, 0, 2};
    r.box.publish(s, 1);
    g_bar_calls.clear();
    open(r.vitals, "vitalBars");  // frame 2: mana on top
    call_bar(0, r.snap(), o);
    CHECK(o[1] == doctest::Approx(33.4f + 18));  // health below mana: 33.4 + 12 + 6
    call_bar(1, r.snap(), o);
    CHECK(o[1] == doctest::Approx(33.4f));
    close(r.vitals);
    REQUIRE(g_bar_calls.size() == 2);
    CHECK(g_bar_calls[0].ty == doctest::Approx(18));
    CHECK(g_bar_calls[1].ty == doctest::Approx(-24));
    CHECK(r.layer.pushes == 2);
    CHECK(r.layer.pops == 2);
}

TEST_CASE("bar_call_path: a bar first seen this frame gets no order offset that frame") {  // Review Focus 1
    BarRig r(active());
    float o[4];
    open(r.vitals, "vitalBars");
    call_bar(0, r.snap(), o);
    call_bar(1, r.snap(), o);
    close(r.vitals);
    HudSnapshot s = active();
    s.order = {2, 0, 1};  // stamina first
    r.box.publish(s, 1);
    open(r.vitals, "vitalBars");  // stamina appears now: it was not seen last frame
    call_bar(0, r.snap(), o);
    call_bar(1, r.snap(), o);
    call_bar(2, r.snap(), o);
    CHECK(o[1] == doctest::Approx(75.4f));  // where the game put it
    close(r.vitals);
}

TEST_CASE("bar_call_path: a hidden bar is pushed off by kHideShift") {
    HudSnapshot s = active();
    s.edits[static_cast<size_t>(bar_element(kBarHealth))].hidden = true;
    BarRig r(s);
    open(r.vitals, "vitalBars");
    float o[4];
    call_bar(0, r.snap(), o);
    REQUIRE(g_bar_calls.size() == 1);
    CHECK(g_bar_calls[0].tx == doctest::Approx(-kHideShift));
    CHECK(g_bar_calls[0].ty == doctest::Approx(-kHideShift));
    CHECK(o[1] == doctest::Approx(33.4f - kHideShift));
    close(r.vitals);
}

TEST_CASE("bar_call_path: a data pointer off the stride is left alone") {  // Review Focus 2
    HudSnapshot s = active();
    for (int b = 0; b < 3; ++b) s.edits[static_cast<size_t>(bar_element(b))].x_percent = 5;
    BarRig r(s);
    open(r.vitals, "vitalBars");
    float o[4];
    call_bar(0, r.snap(), o);
    const int before = r.layer.pushes;
    call_bar_data(reinterpret_cast<void*>(kWidget + 0x20), r.snap(), o);
    CHECK(r.layer.pushes == before);
    CHECK(o[0] == doctest::Approx(24.4f));
    close(r.vitals);
}

TEST_CASE("bar_call_path: inside a scaled group the shift is multiplied by the stack's top scale") {  // Review Focus 4
    HudSnapshot s = active();
    s.edits[static_cast<size_t>(bar_element(kBarMana))].y_percent = 10;
    BarRig r(s);
    const float group[3] = {2, 10, 0};
    fake_push(nullptr, group);  // the vitals group's scale, pushed by its own scope
    open(r.vitals, "vitalBars");
    float o[4];
    call_bar(0, r.snap(), o);
    call_bar(1, r.snap(), o);
    REQUIRE(g_bar_calls.size() == 2);
    CHECK(g_bar_calls[1].scale == doctest::Approx(2));
    CHECK(g_bar_calls[1].tx == doctest::Approx(10));
    CHECK(g_bar_calls[1].ty == doctest::Approx(216));
    CHECK(o[1] == doctest::Approx(57.4f + 108));  // the rect stays in the bar's own units
    close(r.vitals);
}

TEST_CASE("bar_call_path: the editor switched off while vitalBars was open still closes it") {
    HudSnapshot s = active();
    s.edits[static_cast<size_t>(bar_element(kBarMana))].y_percent = 10;
    BarRig r(s);
    open(r.vitals, "vitalBars");
    hud_set_for_tests(&r.box, false);
    close(r.vitals);  // seen while off
    hud_set_for_tests(&r.box, true);
    float o[4];
    call_bar(1, r.snap(), o);  // an enemy's bar later in the frame, say
    CHECK(r.layer.pushes == 0);
    CHECK(o[1] == doctest::Approx(57.4f));
}

TEST_CASE("bar_call_path: an inactive snapshot moves nothing") {
    HudSnapshot s = active();
    s.edits[static_cast<size_t>(bar_element(kBarMana))].y_percent = 10;
    s.active = false;
    BarRig r(s);
    open(r.vitals, "vitalBars");
    float o[4];
    call_bar(1, r.snap(), o);
    CHECK(r.layer.pushes == 0);
    close(r.vitals);
}

TEST_CASE("status list: buffs_up pushes the upward offset at buffDebuffParent's open and pops after its close") {
    HudSnapshot s = active();
    s.buffs_up = true;
    BarRig r(s);
    measure(2084.2f, 1080);
    Node food, list;
    r.layer.attach(food);
    r.layer.attach(list);
    food.set_rect(20, 87.8f, 40, 120);
    list.set_rect(72, 87.8f, 91.6f, 21.6f);
    open(food, "foodIconParent");
    close(food);
    open(list, "buffDebuffParent");
    CHECK(r.layer.pushes == 1);
    CHECK(r.layer.table[2] == doctest::Approx(98.4f));
    close(list);
    CHECK(r.layer.pops == 1);
    CHECK(r.layer.count() == 0);
}

TEST_CASE("status list: off by default, and nothing without the food column this frame") {
    BarRig off(active());
    measure(2084.2f, 1080);
    Node food, list;
    off.layer.attach(food);
    off.layer.attach(list);
    food.set_rect(20, 87.8f, 40, 120);
    list.set_rect(72, 87.8f, 91.6f, 21.6f);
    open(food, "foodIconParent");
    close(food);
    open(list, "buffDebuffParent");
    close(list);
    CHECK(off.layer.pushes == 0);
    HudSnapshot s = active();
    s.buffs_up = true;
    off.box.publish(s, 1);
    measure(2084.2f, 1080);  // a new frame: the food column not seen yet
    open(list, "buffDebuffParent");
    close(list);
    CHECK(off.layer.pushes == 0);
}

TEST_CASE("bar_live: a bar not drawn in the last frame stays seen but has no area left to pick") {  // review: Important 2
    BarRig r(active());
    float o[4];
    open(r.vitals, "vitalBars");  // stamina drawn
    call_bar(0, r.snap(), o);
    call_bar(1, r.snap(), o);
    call_bar(2, r.snap(), o);
    close(r.vitals);
    CHECK(bar_live(kBarStamina).rect[2] == doctest::Approx(200));
    open(r.vitals, "vitalBars");  // stamina full again: not drawn
    call_bar(0, r.snap(), o);
    call_bar(1, r.snap(), o);
    close(r.vitals);
    open(r.vitals, "vitalBars");  // the next frame starts
    const LiveInfo s = bar_live(kBarStamina);
    CHECK(s.seen);
    CHECK(s.rect[2] == 0);
    CHECK(s.rect[3] == 0);
    CHECK(bar_live(kBarMana).rect[2] == doctest::Approx(260));
    close(r.vitals);
}
