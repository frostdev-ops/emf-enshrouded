// Markers step 3 (API v35): the local player's view for a minimap: the heading from the player's rotation, the camera's
// yaw from its view matrix, and player_view through the core (game-free).
#include <windows.h>

#include <doctest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "core/api.h"
#include "core/api_sizes.h"
#include "core/marker_capture.h"
#include "core/marker_map.h"
#include "core/player_view.h"

using namespace emf::core;

TEST_CASE("player view: the heading from a rotation: 0 north (+z), 90 east (+x), 180 south, 270 west") {
    CHECK(heading_from_quat(0, 0, 0, 1) == doctest::Approx(0));
    const float s = std::sqrt(0.5f);
    CHECK(heading_from_quat(0, s, 0, s) == doctest::Approx(90));    // 90 degrees about +y: forward +x
    CHECK(heading_from_quat(0, 1, 0, 0) == doctest::Approx(180));   // 180 about +y: forward -z
    CHECK(heading_from_quat(0, -s, 0, s) == doctest::Approx(270));  // -90 about +y: forward -x (never negative)
    CHECK(heading_from_quat(0, 0, 0, 0) == doctest::Approx(0));     // a zero quaternion: north, no NaN
}

TEST_CASE("player view: the camera's yaw from the view matrix (its forward: x v[2], z v[10])") {
    float v[16] = {};
    v[2] = 0;
    v[10] = 1;
    CHECK(yaw_from_view(v) == doctest::Approx(0));
    v[2] = 1;
    v[10] = 0;
    CHECK(yaw_from_view(v) == doctest::Approx(90));
    v[2] = 0;
    v[10] = -1;
    CHECK(yaw_from_view(v) == doctest::Approx(180));
}

TEST_CASE("api v35: version 35, five more functions") {
    const EmfApi* api = api_table();
    CHECK(api->version == 35u);
    CHECK(api->size == kApiV35Size);
    CHECK(kApiV35Size == 992u);
    CHECK(api->player_view);
    CHECK(api->fog_at);
    CHECK(api->map_markers);
    CHECK(api->canvas_set);
    CHECK(api->canvas_draw);
}

TEST_CASE("player_view: false without a local player; with one: the position, the heading and the world size") {
    CoreState state;
    set_core_state(&state);
    marker_capture_reset_for_tests();
    EmfPlayerView v{};
    v.size = sizeof v;
    CHECK_FALSE(api_table()->player_view(&v));
    marker_capture_set_player(true, 1500, 320, 4740);
    const float s = std::sqrt(0.5f);
    marker_capture_set_player_rotation(0, s, 0, s);
    REQUIRE(api_table()->player_view(&v));
    CHECK(v.x == 1500);
    CHECK(v.z == 4740);
    CHECK(v.heading_deg == doctest::Approx(90));
    CHECK(v.world_size == doctest::Approx(10240));
    CHECK((v.flags & EMF_VIEW_CAMERA) == 0u);  // no camera captured yet
    CHECK_FALSE(api_table()->player_view(nullptr));
    marker_capture_reset_for_tests();
    set_core_state(nullptr);
}

TEST_CASE("api map_markers: the game's own entries from the map list; counts with a null buffer; at most cap filled") {
    CoreState state;
    set_core_state(&state);
    CHECK(api_table()->map_markers(nullptr, 0) == 0u);
    std::vector<uint8_t> entries(8 * kMapEntryBytes, 0);
    MapList l;
    l.data = entries.data();
    l.capacity = 8;
    for (uint32_t i = 0; i < 3; ++i) {
        uint8_t* e = entries.data() + i * kMapEntryBytes;
        const uint32_t entity = 1500 + i, type = 0x10 + i;
        const int64_t pos[3] = {static_cast<int64_t>((100.0 + i) * 4294967296.0), 0, static_cast<int64_t>(200.0 * 4294967296.0)};
        std::memcpy(e, &entity, 4);
        std::memcpy(e + 0x10, pos, sizeof pos);
        std::memcpy(e + 0x30, &type, 4);
        e[0x34] = 2;
        ++l.count;
    }
    state.markers.map.pre(&l, true);
    CHECK(api_table()->map_markers(nullptr, 0) == 3u);
    EmfGameMarker out[2]{};
    CHECK(api_table()->map_markers(out, 2) == 3u);
    CHECK(out[0].size == sizeof(EmfGameMarker));
    CHECK(out[0].type == 0x10u);
    CHECK(out[1].x == doctest::Approx(101));
    CHECK(out[1].flags == EMF_GAME_MARKER_SHOWN);
    set_core_state(nullptr);
}
