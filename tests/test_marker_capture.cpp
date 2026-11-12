// Markers (API v33): what EMF copies for drawing: the camera block of UiPlayData in the UI frame, when the game last
// showed its world-space UI and its map, the map's rectangle, the local player's position (fake memory, no game).
#include <doctest.h>

#include <cstring>
#include <vector>

#include "common/fnv.h"
#include "core/marker_capture.h"

using namespace emf::core;

namespace {

struct FakeNode {
    alignas(16) uint8_t node[0x170]{};
    void set_rect(float x, float y, float w, float h) {
        const float r[4] = {x, y, w, h};
        std::memcpy(node + 0x114, r, sizeof r);
    }
};

struct FakeUiPlayData {
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x2000, 0);
    FakeUiPlayData() {
        float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -10, -20, -30, 0};
        float p[16] = {};
        p[0] = 0.5f;
        p[5] = 1.0f;
        std::memcpy(mem.data() + 0x1d20, v, sizeof v);
        std::memcpy(mem.data() + 0x1d60, p, sizeof p);
    }
};

}  // namespace

TEST_CASE("seq box: what was stored is loaded; a write in progress is refused") {
    SeqBox<CameraSnap> box;
    CameraSnap c{};
    CHECK_FALSE(box.load(&c));  // nothing stored yet
    c.v[12] = 7;
    c.valid = true;
    box.store(c);
    CameraSnap out{};
    REQUIRE(box.load(&out));
    CHECK(out.v[12] == 7);
    box.begin_write_for_tests();  // a writer stopped half way
    CHECK_FALSE(box.load(&out));
}

TEST_CASE("capture: the camera block is copied when doPlayStateUi() opens, not on other scopes") {
    marker_capture_reset_for_tests();
    FakeUiPlayData u;
    FakeNode ui;
    ui.set_rect(0, 0, 2084, 1080);
    marker_capture_set_layout(0x1d20);
    marker_capture_set_uipd(u.mem.data(), 1000);
    CameraSnap c{};
    markers_ui_frame(emf::fnv1a64("vitalBars"), ui.node, 1000);
    CHECK_FALSE(marker_camera(&c));
    markers_ui_frame(kMarkerUiSpaceHash, ui.node, 1000);
    REQUIRE(marker_camera(&c));
    CHECK(c.valid);
    CHECK(c.frame_ms == 1000);
    CHECK(c.v[12] == -10);
    CHECK(c.v[14] == -30);
    CHECK(c.p[0] == 0.5f);
    CHECK(c.p[5] == 1.0f);
}

TEST_CASE("capture: no layout from the signature, a stale world or a bad pointer copies nothing") {
    marker_capture_reset_for_tests();
    FakeUiPlayData u;
    FakeNode ui;
    CameraSnap c{};
    marker_capture_set_uipd(u.mem.data(), 1000);
    markers_ui_frame(kMarkerUiSpaceHash, ui.node, 1000);  // no layout set: off
    CHECK_FALSE(marker_camera(&c));
    marker_capture_set_layout(0x1d20);
    markers_ui_frame(kMarkerUiSpaceHash, ui.node, 4000);  // the client world's last tick was 3 s ago
    CHECK_FALSE(marker_camera(&c));
    marker_capture_set_uipd(reinterpret_cast<uint8_t*>(0x10), 5000);  // unreadable
    markers_ui_frame(kMarkerUiSpaceHash, ui.node, 5000);
    CHECK_FALSE(marker_camera(&c));
    CHECK(marker_capture_faults() == 1);
    marker_capture_set_uipd(nullptr, 6000);  // the world ended
    markers_ui_frame(kMarkerUiSpaceHash, ui.node, 6000);
    CHECK_FALSE(marker_camera(&c));
}

TEST_CASE("capture: the world UI and the map are seen per frame; the map's rectangle and frame are kept") {
    marker_capture_reset_for_tests();
    FakeNode n;
    CHECK_FALSE(marker_sight(1000).world_ui);
    markers_ui_frame(kMarkerWorldUiHash, n.node, 1000);
    CHECK(marker_sight(1100).world_ui);
    CHECK_FALSE(marker_sight(1300).world_ui);  // not seen for more than 250 ms
    CHECK_FALSE(marker_sight(1100).map_open);
    FakeNode ui, scaled, frame;
    ui.set_rect(0, 0, 2084, 1080);
    scaled.set_rect(-500, -300, 3000, 3000);
    frame.set_rect(100, 80, 1800, 900);
    marker_capture_set_layout(0x1d20);
    markers_ui_frame(kMarkerUiSpaceHash, ui.node, 2000);
    markers_ui_frame(kMarkerMapScaledHash, scaled.node, 2000);
    markers_ui_frame(kMarkerMapFrameHash, frame.node, 2000);
    CHECK(marker_sight(2100).map_open);
    MapView m{};
    REQUIRE(marker_map_view(&m));
    CHECK(m.rect[0] == -500);
    CHECK(m.rect[2] == 3000);
    CHECK(m.clip[0] == 100);
    CHECK(m.clip[3] == 900);
    CHECK(m.ui_w == 2084);
    CHECK(m.ui_h == 1080);
    // the scope hashes are the game's own names
    CHECK(kMarkerMapScaledHash == 0x6211bce57d3fd2f3ull);
    CHECK(kMarkerWorldUiHash == emf::fnv1a64("doCachedEntityHealthWidgets"));
}

TEST_CASE("capture: the local player's position") {
    marker_capture_reset_for_tests();
    double p[3];
    CHECK_FALSE(marker_player_pos(p));
    marker_capture_set_player(true, 1.5, 2.5, 3.5);
    REQUIRE(marker_player_pos(p));
    CHECK(p[0] == 1.5);
    CHECK(p[2] == 3.5);
    marker_capture_set_player(false, 0, 0, 0);  // no local player any more
    CHECK_FALSE(marker_player_pos(p));
}

#include "core/hud_editor/hook.h"

TEST_CASE("capture: the HUD editor's scope detour passes every scope on, also with the editor off") {
    marker_capture_reset_for_tests();
    emf::core::hud::hud_reset_live_for_tests();  // the editor is off
    alignas(16) uint8_t scope[0x28]{};
    FakeNode n;
    uint8_t* node = n.node;
    std::memcpy(scope + emf::core::hud::kScopeNode, &node, sizeof node);
    const char* name = "doCachedEntityHealthWidgets";
    const emf::core::hud::NameInfo ni{name, std::strlen(name), emf::fnv1a64(name)};
    auto original = [](void*, const emf::core::hud::NameInfo*) -> void* { return nullptr; };
    emf::core::hud::hud_open_path(scope, &ni, original, 5000);
    CHECK(marker_sight(5100).world_ui);
    emf::core::hud::hud_open_path(scope, nullptr, original, 6000);  // no name: nothing
    CHECK_FALSE(marker_sight(6100).world_ui);
}


TEST_CASE("capture (review I1): the UI space is doPlayStateUi()'s rect, kept every frame; none before it was seen") {
    marker_capture_reset_for_tests();
    float w = 0, h = 0;
    CHECK_FALSE(marker_ui_size(&w, &h));
    FakeNode n;
    n.set_rect(0, 0, 1728, 1080);  // a 16:10 screen (a Steam Deck): not 1920 wide
    markers_ui_frame(kMarkerUiSpaceHash, n.node, 1000);
    REQUIRE(marker_ui_size(&w, &h));
    CHECK(w == doctest::Approx(1728));
    CHECK(h == doctest::Approx(1080));
    marker_capture_reset_for_tests();
}
