// API v35 a minimap's parts (api.h): player_view (the local player's position, heading and camera yaw), fog_at, the
// game's map markers, canvases (drawing panels mods own, drawn by EMF's overlay). Private spec
// 2026-11-11-markers-design part 4, plan 2026-11-12-markers-step3.
#include <windows.h>

#include <cstring>

#include <vector>

#include "common/seh.h"
#include "common/world_view.h"
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
    const CoreState* s = core_state();
    const float known = s ? s->minimap.fog.world_size() : 0.0f;
    v.world_size = known > 0 ? known : kDefaultWorldSize;
    const uint32_t n = out->size && out->size < sizeof v ? out->size : static_cast<uint32_t>(sizeof v);
    std::memcpy(out, &v, n);
    out->size = n;
    return true;
}

bool api_fog_at(double x, double z, float* explored) {
    CoreState* s = core_state();
    return s && explored && s->minimap.fog.explored(x, z, explored);
}

void minimap_client_tick(EmfWorld* client, uint64_t now_ms) {
    CoreState* s = core_state();
    if (!s || !client || !s->minimap.fog.due(now_ms)) return;
    auto* grid = static_cast<const uint8_t*>(WorldView(reinterpret_cast<uint8_t*>(client)).singleton("FogOfWar"));
    FogHeader h;
    const uint8_t* data = nullptr;
    if (!grid || !seh_read(grid, &h.w, 4) || !seh_read(grid + 4, &h.h, 4) || !seh_read(grid + 8, &h.cols, 4) ||
        !seh_read(grid + 12, &h.rows, 4) || !seh_read(grid + 0x18, &data, 8) || !seh_read(grid + 0x20, &h.blocks, 8)) {
        s->minimap.fog.clear();
        return;
    }
    h.data = data;  // checked before anything is read through it
    if (!fog_header_ok(h)) {
        s->minimap.fog.clear();
        return;
    }
    std::vector<uint8_t> copy(static_cast<size_t>(h.blocks) * kFogBlockSide * kFogBlockSide);
    if (!seh_read(data, copy.data(), copy.size())) return;  // a world going away: the next tick tries again
    h.data = copy.data();
    s->minimap.fog.update(h, now_ms);
}
uint32_t api_map_markers(EmfGameMarker* out, uint32_t cap) {
    CoreState* s = core_state();
    if (!s) return 0;
    const auto all = s->markers.map.game_markers();
    for (uint32_t i = 0; out && i < cap && i < all.size(); ++i) {
        EmfGameMarker g{};
        g.size = sizeof g;
        g.type = all[i].type;
        g.flags = all[i].shown ? EMF_GAME_MARKER_SHOWN : 0u;
        g.x = all[i].x;
        g.y = all[i].y;
        g.z = all[i].z;
        out[i] = g;
    }
    return static_cast<uint32_t>(all.size());
}
bool api_canvas_set(const char*, const EmfCanvas*) { return false; }
bool api_canvas_draw(const char*, const EmfCanvasCmd*, uint32_t) { return false; }

}  // namespace emf::core
