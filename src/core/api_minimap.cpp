// API v35 a minimap's parts (api.h): player_view (the local player's position, heading and camera yaw), fog_at, the
// game's map markers, canvases (drawing panels mods own, drawn by EMF's overlay). Private spec
// 2026-11-11-markers-design part 4, plan 2026-11-12-markers-step3.
#include <windows.h>

#include <cstring>

#include "core/api.h"
#include "core/marker_capture.h"
#include "core/player_view.h"

namespace emf::core {

namespace {
constexpr float kDefaultWorldSize = 10240.0f;
}  // namespace

bool api_player_view(EmfPlayerView* out) {
    if (!out) return false;
    double p[3];
    if (!marker_player_pos(p)) return false;
    EmfPlayerView v{};
    v.size = sizeof v;
    v.x = p[0];
    v.y = p[1];
    v.z = p[2];
    float q[4];
    if (marker_player_rotation(q)) v.heading_deg = heading_from_quat(q[0], q[1], q[2], q[3]);
    CameraSnap cam;
    if (marker_camera(&cam)) {
        v.camera_yaw_deg = yaw_from_view(cam.v);
        v.flags |= EMF_VIEW_CAMERA;
    }
    v.world_size = kDefaultWorldSize;
    const uint32_t n = out->size && out->size < sizeof v ? out->size : static_cast<uint32_t>(sizeof v);
    std::memcpy(out, &v, n);
    out->size = n;
    return true;
}

bool api_fog_at(double, double, float*) { return false; }
uint32_t api_map_markers(EmfGameMarker*, uint32_t) { return 0; }
bool api_canvas_set(const char*, const EmfCanvas*) { return false; }
bool api_canvas_draw(const char*, const EmfCanvasCmd*, uint32_t) { return false; }

}  // namespace emf::core
