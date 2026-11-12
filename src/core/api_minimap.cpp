// API v35 a minimap's parts (api.h): player_view (the local player's position, heading and camera yaw), fog_at, the
// game's map markers, canvases (drawing panels mods own, drawn by EMF's overlay). Private spec
// 2026-11-11-markers-design part 4, plan 2026-11-12-markers-step3.
#include <windows.h>

#include <intrin.h>

#include <cstring>

#include <vector>

#include "common/seh.h"
#include "common/world_view.h"
#include "core/api.h"
#include "core/fault_report.h"
#include "core/marker_capture.h"
#include "core/player_view.h"

namespace emf::core {

namespace {
constexpr float kDefaultWorldSize = 10240.0f;
uint64_t now_of(const CoreState* s) { return s && s->now_ms ? s->now_ms() : GetTickCount64(); }
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
    if (!s) return false;
    s->minimap.fog_asked_ms.store(now_of(s), std::memory_order_relaxed);  // keeps the fog read (minimap_wanted)
    return explored && s->minimap.fog.explored(x, z, explored);
}

void minimap_client_tick(EmfWorld* client, uint64_t now_ms) {
    CoreState* s = core_state();
    if (!s || !client || !s->minimap.fog.due(now_ms)) return;
    const auto canvases = s->minimap.canvases.view();
    if (!minimap_wanted(canvases && !canvases->empty(), s->minimap.fog_asked_ms.load(std::memory_order_relaxed), now_ms))
        return;  // no mod uses the fog of war: nothing is copied
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
    s->minimap.markers_asked_ms.store(now_of(s), std::memory_order_relaxed);  // keeps the map list copied
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
bool api_canvas_set(const char* id, const EmfCanvas* c) {
    CoreState* s = core_state();
    if (!s) return false;
    const std::string owner = module_name_of(_ReturnAddress());
    const auto r = s->minimap.canvases.set(owner, id, c);
    switch (r) {
        case CanvasStore::SetResult::Ok:
        case CanvasStore::SetResult::Replaced:
        case CanvasStore::SetResult::Removed: return true;
        case CanvasStore::SetResult::NotFound: return false;
        default: break;
    }
    if (s->log_line) {
        const char* why = r == CanvasStore::SetResult::BadId    ? "a bad id (1-48 of A-Z a-z 0-9 _ . - :)"
                          : r == CanvasStore::SetResult::Full    ? "32 canvases already"
                          : r == CanvasStore::SetResult::Taken   ? "the id is another mod's"
                                                                 : "bad numbers (corner 0-3, shape 0-1, w and h 8-1080)";
        std::string out;
        {
            std::lock_guard<std::mutex> l(s->markers.log_lock);  // a mod retrying every frame: once per 10 s
            out = s->markers.log.pass("canvas_set refused (" + owner + "): " + why, now_of(s));
        }
        if (!out.empty()) s->log_line(out);
    }
    return false;
}

bool api_canvas_draw(const char* id, const EmfCanvasCmd* cmds, uint32_t count) {
    CoreState* s = core_state();
    if (!s) return false;
    uint32_t kept = 0;
    return s->minimap.canvases.draw(module_name_of(_ReturnAddress()), id, cmds, count, &kept);
}

}  // namespace emf::core
