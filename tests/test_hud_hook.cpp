// The HUD editor's UI scope detours, against fake scopes, nodes and a fake UI layer (no game, no MinHook).
#include <doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "common/fnv.h"
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
