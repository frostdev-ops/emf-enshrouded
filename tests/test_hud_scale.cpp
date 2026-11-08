// HUD scale: the anchor and transform maths, and the balanced push and pop on the game's UI layer, against fake memory
// laid out like the layer, draw list and transform stack of build 1287369.
#include <doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "core/hud_editor/scale.h"

using namespace emf::core::hud;

namespace {

template <class T>
void put(std::vector<uint8_t>& mem, size_t off, T v) {
    std::memcpy(mem.data() + off, &v, sizeof v);
}
template <class T>
T get(const std::vector<uint8_t>& mem, size_t off) {
    T v;
    std::memcpy(&v, mem.data() + off, sizeof v);
    return v;
}

struct Fake {
    std::vector<uint8_t> node = std::vector<uint8_t>(0x170), window = std::vector<uint8_t>(0x20),
                         layer = std::vector<uint8_t>(0x6ce8), dl = std::vector<uint8_t>(0x352d8);
    float table[16 * 4] = {};
    uint16_t index[16] = {};

    Fake() {
        put<uint8_t*>(node, kNodeWindow, window.data());
        put<uint8_t*>(window, kWindowLayer, layer.data());
        put<int>(layer, kLayerType, kLayerTypeUi);
        put<uint8_t*>(layer, kLayerDrawList, dl.data());
        put<uint8_t*>(layer, kLayerXforms, reinterpret_cast<uint8_t*>(table));
        put<uint8_t*>(layer, kLayerXformIndex, reinterpret_cast<uint8_t*>(index));
        set_count(0);
        set_clip(0, 0, 2084, 1080);
        set_rect(20, 20, 341.5f, 187.8f);
    }
    uint8_t* n() { return node.data(); }
    uint8_t* l() { return layer.data(); }
    void set_count(uint64_t c) { put<uint64_t>(layer, kLayerXformCount, c); }
    uint64_t count() const { return get<uint64_t>(layer, kLayerXformCount); }
    void set_clip(float x, float y, float w, float h) {
        put<float>(layer, kLayerClip, x);
        put<float>(layer, kLayerClip + 4, y);
        put<float>(layer, kLayerClip + 8, w);
        put<float>(layer, kLayerClip + 12, h);
    }
    void set_rect(float x, float y, float w, float h) {
        put<float>(node, 0x114, x);
        put<float>(node, 0x118, y);
        put<float>(node, 0x11c, w);
        put<float>(node, 0x120, h);
    }
};

// What the fake push and pop saw.
struct Calls {
    Fake* fake = nullptr;
    int pushes = 0, pops = 0;
    float last[3] = {};
    void* last_layer = nullptr;
} g;

void fake_push(void* layer, const float* xf) {
    ++g.pushes;
    g.last_layer = layer;
    std::memcpy(g.last, xf, sizeof g.last);
    Fake& f = *g.fake;
    const uint64_t c = f.count();
    f.table[c * 4] = xf[0];
    f.table[c * 4 + 1] = xf[1];
    f.table[c * 4 + 2] = xf[2];
    f.index[c] = static_cast<uint16_t>(c);
    f.set_count(c + 1);
}
void fake_pop(void*) {
    ++g.pops;
    g.fake->set_count(g.fake->count() - 1);
}

void start(Fake& f) {
    scale_reset_for_tests();
    g = Calls{};
    g.fake = &f;
    set_transform_functions(&fake_push, &fake_pop);
}

}  // namespace

TEST_CASE("auto_anchor: the screen edge a part is nearest, by thirds") {
    const float area[4] = {0, 0, 2084, 1080};
    const float vitals[4] = {20, 20, 341.5f, 187.8f}, compass[4] = {786.1f, 20, 512, 68};  // run 3 rects
    const float hotbar[4] = {717.1f, 813.4f, 650, 266.6f}, quests[4] = {1489.2f, 0, 595, 123.1f};
    CHECK(auto_anchor(vitals, area).ax == 0.0f);
    CHECK(auto_anchor(vitals, area).ay == 0.0f);
    CHECK(auto_anchor(compass, area).ax == 0.5f);
    CHECK(auto_anchor(compass, area).ay == 0.0f);
    CHECK(auto_anchor(hotbar, area).ax == 0.5f);
    CHECK(auto_anchor(hotbar, area).ay == 1.0f);
    CHECK(auto_anchor(quests, area).ax == 1.0f);
    CHECK(auto_anchor(quests, area).ay == 0.0f);
    const float none[4] = {0, 0, 0, 0}, nan[4] = {NAN, 0, 2084, 1080};
    CHECK(auto_anchor(vitals, none).ax == 0.5f);
    CHECK(auto_anchor(vitals, none).ay == 0.5f);
    CHECK(auto_anchor(vitals, nan).ax == 0.5f);
    CHECK(auto_anchor(vitals, nan).ay == 0.5f);
}

TEST_CASE("anchor_point: the anchor on the part's rect") {
    float ox = 0, oy = 0;
    const float vitals[4] = {20, 20, 341.5f, 187.8f}, hotbar[4] = {717.1f, 813.4f, 650, 266.6f};
    anchor_point(vitals, {0, 0}, &ox, &oy);
    CHECK(ox == doctest::Approx(20.0f));
    CHECK(oy == doctest::Approx(20.0f));
    anchor_point(hotbar, {0.5f, 1.0f}, &ox, &oy);
    CHECK(ox == doctest::Approx(1042.1f));
    CHECK(oy == doctest::Approx(1080.0f));
}

TEST_CASE("scaled_about: around a point, from the identity") {
    const Xform a = scaled_about({1, 0, 0}, 1.5f, 20, 20);
    CHECK(a.scale == doctest::Approx(1.5f));
    CHECK(a.tx == doctest::Approx(-10.0f));
    CHECK(a.ty == doctest::Approx(-10.0f));
    const Xform b = scaled_about({1, 0, 0}, 0.75f, 1042.1f, 1080);
    CHECK(b.scale == doctest::Approx(0.75f));
    CHECK(b.tx == doctest::Approx(260.525f));
    CHECK(b.ty == doctest::Approx(270.0f));
}

TEST_CASE("scaled_about: composes with a parent's transform") {  // Review Focus 4
    const Xform t = scaled_about({2, 5, 7}, 0.75f, 100, 40);
    CHECK(t.scale == doctest::Approx(1.5f));
    CHECK(t.tx == doctest::Approx(55.0f));
    CHECK(t.ty == doctest::Approx(27.0f));
    // the point (120,60): parent then child = 2*(100 + 0.75*20) + 5 = 235, 2*(40 + 0.75*20) + 7 = 117
    CHECK(t.scale * 120 + t.tx == doctest::Approx(235.0f));
    CHECK(t.scale * 60 + t.ty == doctest::Approx(117.0f));
}

TEST_CASE("usable_top and check_pop") {
    CHECK_FALSE(usable_top({0, 0, 0}));
    CHECK_FALSE(usable_top({1, NAN, 0}));
    CHECK_FALSE(usable_top({1, 0, INFINITY}));
    CHECK(usable_top({1.2f, 3, 4}));
    CHECK(check_pop(5, 4) == PopCheck::Pop);
    CHECK(check_pop(4, 4) == PopCheck::Mismatch);
    CHECK(check_pop(6, 4) == PopCheck::Mismatch);
}

TEST_CASE("layer_of: only a UI layer with a draw list") {
    Fake f;
    CHECK(layer_of(f.n()) == f.l());
    put<int>(f.layer, kLayerType, 3);
    CHECK(layer_of(f.n()) == nullptr);
    put<int>(f.layer, kLayerType, kLayerTypeUi);
    put<uint8_t*>(f.layer, kLayerDrawList, nullptr);
    CHECK(layer_of(f.n()) == nullptr);
    put<uint8_t*>(f.layer, kLayerDrawList, f.dl.data());
    put<uint8_t*>(f.window, kWindowLayer, nullptr);
    CHECK(layer_of(f.n()) == nullptr);
    put<uint8_t*>(f.node, kNodeWindow, nullptr);
    CHECK(layer_of(f.n()) == nullptr);
    CHECK(layer_of(nullptr) == nullptr);
}

TEST_CASE("read_top: identity on an empty stack, else the entry the index stack names") {
    Fake f;
    Xform top;
    uint64_t depth = 99;
    CHECK(read_top(f.l(), &top, &depth));
    CHECK(top.scale == 1.0f);
    CHECK(top.tx == 0.0f);
    CHECK(top.ty == 0.0f);
    CHECK(depth == 0);
    f.set_count(2);
    f.index[0] = 0;
    f.index[1] = 1;
    f.table[4] = 1.25f;
    f.table[5] = 3;
    f.table[6] = 4;
    CHECK(read_top(f.l(), &top, &depth));
    CHECK(top.scale == 1.25f);
    CHECK(top.tx == 3.0f);
    CHECK(top.ty == 4.0f);
    CHECK(depth == 2);
    f.table[4] = 0;
    CHECK_FALSE(read_top(f.l(), &top, &depth));
}

TEST_CASE("clear_emptied_index: the draw list's index and flag back to 0") {
    Fake f;
    put<uint16_t>(f.dl, kDlXformIndex, 5);
    put<uint8_t>(f.dl, kDlXformFlag, 1);
    clear_emptied_index(f.l());
    CHECK(get<uint16_t>(f.dl, kDlXformIndex) == 0);
    CHECK(get<uint8_t>(f.dl, kDlXformFlag) == 0);
}

TEST_CASE("scale_open and scale_close: one push, one pop, balanced") {
    Fake f;
    start(f);
    put<uint16_t>(f.dl, kDlXformIndex, 3);
    put<uint8_t>(f.dl, kDlXformFlag, 1);
    const ScalePush p = scale_open(f.n(), 1.5f);
    CHECK(p.pushed);
    CHECK(p.depth == 0);
    CHECK(p.layer == f.l());
    CHECK(g.pushes == 1);
    CHECK(g.last[0] == doctest::Approx(1.5f));
    CHECK(g.last[1] == doctest::Approx(-10.0f));
    CHECK(g.last[2] == doctest::Approx(-10.0f));
    CHECK(f.count() == 1);
    scale_close(p);
    CHECK(g.pops == 1);
    CHECK(f.count() == 0);
    CHECK(get<uint16_t>(f.dl, kDlXformIndex) == 0);
    CHECK(get<uint8_t>(f.dl, kDlXformFlag) == 0);
    const ScaleState st = hud_scale_state();
    CHECK(st.available);
    CHECK_FALSE(st.off);
    CHECK(st.pushes == 1);
    CHECK(st.pops == 1);
    CHECK(st.mismatches == 0);
}

TEST_CASE("scale_open: a child inside a scaled parent pushes on top of the parent's transform") {
    Fake f;
    start(f);
    const ScalePush parent = scale_open(f.n(), 2.0f);
    REQUIRE(parent.pushed);
    f.set_rect(100, 40, 40, 40);  // the child's rect, last frame
    const ScalePush child = scale_open(f.n(), 0.5f);
    REQUIRE(child.pushed);
    CHECK(child.depth == 1);
    CHECK(g.last[0] == doctest::Approx(1.0f));  // 2 * 0.5
    scale_close(child);
    scale_close(parent);
    CHECK(f.count() == 0);
    CHECK(hud_scale_state().pops == 2);
}

TEST_CASE("scale_close: a stack not exactly one above is never popped, and scale turns off for the session") {
    Fake f;
    start(f);
    const ScalePush p = scale_open(f.n(), 1.5f);
    REQUIRE(p.pushed);
    f.set_count(f.count() + 1);  // someone else pushed
    scale_close(p);
    CHECK(g.pops == 0);
    const ScaleState st = hud_scale_state();
    CHECK(st.off);
    CHECK(st.mismatches == 1);
    f.set_count(0);
    CHECK_FALSE(scale_open(f.n(), 1.5f).pushed);
}

TEST_CASE("scale_open: no push for scale 1, out of range, without functions, without a UI layer, or with an unusable top") {
    Fake f;
    start(f);
    CHECK_FALSE(scale_open(f.n(), 1.0f).pushed);
    CHECK_FALSE(scale_open(f.n(), 0.49f).pushed);
    CHECK_FALSE(scale_open(f.n(), 2.01f).pushed);
    CHECK_FALSE(scale_open(f.n(), NAN).pushed);
    CHECK(g.pushes == 0);
    put<int>(f.layer, kLayerType, 3);
    CHECK_FALSE(scale_open(f.n(), 1.5f).pushed);
    put<int>(f.layer, kLayerType, kLayerTypeUi);
    f.set_rect(NAN, 0, 10, 10);
    CHECK_FALSE(scale_open(f.n(), 1.5f).pushed);
    f.set_rect(20, 20, 341.5f, 187.8f);
    f.set_count(1);
    f.table[0] = 0;  // the top has scale 0
    CHECK_FALSE(scale_open(f.n(), 1.5f).pushed);
    f.table[0] = 1;
    CHECK(g.pushes == 0);
    CHECK(scale_open(f.n(), 1.5f).pushed);
    scale_reset_for_tests();  // no functions
    CHECK_FALSE(hud_scale_state().available);
    f.set_count(0);
    CHECK_FALSE(scale_open(f.n(), 1.5f).pushed);
}

TEST_CASE("scale_close and scale_lost: nothing pushed, nothing done; a lost push turns scale off") {
    Fake f;
    start(f);
    scale_close(ScalePush{});
    CHECK(g.pops == 0);
    CHECK_FALSE(hud_scale_state().off);
    scale_lost(ScalePush{});
    CHECK_FALSE(hud_scale_state().off);
    scale_lost(ScalePush{f.l(), 0, true});
    CHECK(hud_scale_state().off);
    CHECK(hud_scale_state().mismatches == 1);
}
