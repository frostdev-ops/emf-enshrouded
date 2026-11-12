// Markers step 3 (canvas.h).
#include "core/canvas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace emf::core {

namespace {

constexpr float kMaxUnits = 4096.0f;
constexpr float kMinMetresPerUnit = 0.01f;  // a closer zoom gives the game's map canvas an endless rect
constexpr size_t kTextMax = 256;

bool id_ok(const char* id) {
    if (!id) return false;
    const size_t n = std::strlen(id);
    if (n == 0 || n > 48) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == '-' || c == ':'))
            return false;
    }
    return true;
}

bool fin(float v) { return std::isfinite(v) && std::fabs(v) <= kMaxUnits; }

bool spec_ok(const EmfCanvas& c) {
    return c.size >= sizeof(EmfCanvas) && c.corner <= EMF_CANVAS_BOTTOM_RIGHT && c.shape <= EMF_CANVAS_CIRCLE &&
           fin(c.x) && fin(c.y) && std::isfinite(c.w) && std::isfinite(c.h) && c.w >= 8 && c.w <= 1080 && c.h >= 8 &&
           c.h <= 1080;
}

// how many of a[] a kind uses
int used(uint8_t kind) {
    switch (kind) {
        case EMF_CANVAS_RECT_CMD: return 5;
        case EMF_CANVAS_CIRCLE_CMD: return 4;
        case EMF_CANVAS_LINE: return 5;
        case EMF_CANVAS_TRI: return 6;
        case EMF_CANVAS_TEXT: return 3;
        case EMF_CANVAS_IMAGE: return 9;
        case EMF_CANVAS_FOG:
        case EMF_CANVAS_MARKERS:
        case EMF_CANVAS_MAP: return 3;
        default: return -1;
    }
}

}  // namespace

bool canvas_cmd_ok(const EmfCanvasCmd& c) {
    const int n = used(c.kind);
    if (n < 0) return false;
    if (c.kind == EMF_CANVAS_FOG || c.kind == EMF_CANVAS_MARKERS || c.kind == EMF_CANVAS_MAP) {
        // a world point and metres per unit
        return std::isfinite(c.a[0]) && std::isfinite(c.a[1]) && std::fabs(c.a[0]) <= 1e6f && std::fabs(c.a[1]) <= 1e6f &&
               std::isfinite(c.a[2]) && c.a[2] >= kMinMetresPerUnit && c.a[2] <= 1000;
    }
    for (int i = 0; i < n; ++i)
        if (!fin(c.a[i])) return false;
    switch (c.kind) {
        case EMF_CANVAS_RECT_CMD: return c.a[2] >= 0 && c.a[3] >= 0 && c.a[4] >= 0;
        case EMF_CANVAS_CIRCLE_CMD: return c.a[2] >= 0 && c.a[3] >= 0;
        case EMF_CANVAS_LINE: return c.a[4] >= 0;
        case EMF_CANVAS_TEXT: return c.s && *c.s && c.a[2] > 0 && c.a[2] <= 200;
        case EMF_CANVAS_IMAGE: return c.image != 0 && c.a[2] >= 0 && c.a[3] >= 0;
        default: return true;
    }
}

CanvasStore::SetResult CanvasStore::set(const std::string& owner, const char* id, const EmfCanvas* c) {
    if (!id_ok(id)) return SetResult::BadId;
    if (c && !spec_ok(*c)) return SetResult::BadNumbers;
    std::lock_guard<std::mutex> lock(m_);
    const auto it = std::find_if(canvases_.begin(), canvases_.end(), [&](const Canvas& x) { return x.id == id; });
    if (it != canvases_.end() && it->owner != owner) return c ? SetResult::Taken : SetResult::NotFound;
    if (!c) {
        if (it == canvases_.end()) return SetResult::NotFound;
        canvases_.erase(it);
        publish();
        return SetResult::Removed;
    }
    EmfCanvas spec = *c;
    spec.size = sizeof(EmfCanvas);
    if (it != canvases_.end()) {
        it->spec = spec;
        publish();
        return SetResult::Replaced;
    }
    if (canvases_.size() >= kCanvasMax) return SetResult::Full;
    Canvas n;
    n.owner = owner;
    n.id = id;
    n.spec = spec;
    canvases_.push_back(std::move(n));
    publish();
    return SetResult::Ok;
}

bool CanvasStore::draw(const std::string& owner, const char* id, const EmfCanvasCmd* cmds, uint32_t count,
                       uint32_t* kept) {
    if (kept) *kept = 0;
    if (!id) return false;
    std::vector<CanvasCmd> list;
    const uint32_t n = cmds ? std::min<uint32_t>(count, static_cast<uint32_t>(kCanvasCmdsMax)) : 0;
    list.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (!canvas_cmd_ok(cmds[i])) continue;
        CanvasCmd c;
        c.kind = cmds[i].kind;
        c.rgba = cmds[i].rgba;
        c.image = c.kind == EMF_CANVAS_IMAGE ? cmds[i].image : 0;
        std::memcpy(c.a, cmds[i].a, sizeof c.a);
        if (cmds[i].s && c.kind == EMF_CANVAS_TEXT) c.s.assign(cmds[i].s, strnlen(cmds[i].s, kTextMax));
        list.push_back(std::move(c));
    }
    // one MAP, one FOG and one MARKERS per list (the last of each): each is a full drawing of the map or the fog
    bool seen_map = false, seen_fog = false, seen_markers = false;
    for (size_t i = list.size(); i-- > 0;) {
        bool* seen = list[i].kind == EMF_CANVAS_MAP       ? &seen_map
                     : list[i].kind == EMF_CANVAS_FOG     ? &seen_fog
                     : list[i].kind == EMF_CANVAS_MARKERS ? &seen_markers
                                                          : nullptr;
        if (!seen) continue;
        if (*seen) list[i].kind = 0;  // left out below
        *seen = true;
    }
    std::erase_if(list, [](const CanvasCmd& c) { return c.kind == 0; });
    std::lock_guard<std::mutex> lock(m_);
    const auto it =
        std::find_if(canvases_.begin(), canvases_.end(), [&](const Canvas& x) { return x.id == id && x.owner == owner; });
    if (it == canvases_.end()) return false;
    if (kept) *kept = static_cast<uint32_t>(list.size());
    it->cmds = std::move(list);
    publish();
    return true;
}

size_t CanvasStore::remove_owner(const std::string& owner) {
    std::lock_guard<std::mutex> lock(m_);
    const size_t n = std::erase_if(canvases_, [&](const Canvas& c) { return c.owner == owner; });
    if (n) publish();
    return n;
}

std::shared_ptr<const std::vector<Canvas>> CanvasStore::view() const {
    std::lock_guard<std::mutex> lock(m_);
    return view_;
}

void CanvasStore::publish() { view_ = std::make_shared<const std::vector<Canvas>>(canvases_); }

size_t canvas_first_drawn(const std::vector<CanvasCmd>& cmds, bool map_ready) {
    if (!map_ready) return 0;
    size_t first = 0;
    for (size_t i = 0; i < cmds.size(); ++i)
        if (cmds[i].kind == EMF_CANVAS_MAP) first = i + 1;
    return first;
}

void canvas_layout(const EmfCanvas& c, float display_w, float display_h, float out[4]) {
    const float k = display_h > 0 ? display_h / 1080.0f : 1.0f;
    const float w = c.w * k, h = c.h * k;
    const bool left = c.corner == EMF_CANVAS_TOP_LEFT || c.corner == EMF_CANVAS_BOTTOM_LEFT;
    const bool top = c.corner == EMF_CANVAS_TOP_LEFT || c.corner == EMF_CANVAS_TOP_RIGHT;
    out[0] = left ? c.x * k : display_w - c.x * k - w;
    out[1] = top ? c.y * k : display_h - c.y * k - h;
    out[2] = w;
    out[3] = h;
}

MapDrawPlan plan_map_draw(const EmfCanvas& c, float ui_w, float ui_h, double cx, double cz, double mpu, float world_w,
                          float world_h) {
    MapDrawPlan p;
    if (!(ui_w > 0) || !(ui_h > 0) || !(world_w > 100) || !(world_h > 100) || !(world_w < 1e6f) || !(world_h < 1e6f) ||
        !(mpu >= kMinMetresPerUnit) || !std::isfinite(cx) || !std::isfinite(cz))
        return p;
    canvas_layout(c, ui_w, ui_h, p.vp);
    const double k = p.vp[2] / c.w;  // UI units per canvas unit
    p.s = static_cast<float>(k / mpu);
    const double mx = p.vp[0] + p.vp[2] * 0.5, my = p.vp[1] + p.vp[3] * 0.5;
    p.cv[0] = static_cast<float>(mx - p.s * cx);
    p.cv[1] = static_cast<float>(my - p.s * (world_h - cz));
    p.cv[2] = p.s * world_w;
    p.cv[3] = p.s * world_h;
    p.base = 2048.0f / (world_w < world_h ? world_w : world_h);
    p.ok = std::isfinite(p.s) && p.s > 0 && std::isfinite(p.cv[0]) && std::isfinite(p.cv[1]) && std::isfinite(p.cv[2]) &&
           std::isfinite(p.cv[3]);
    return p;
}

void canvas_world_to(const float rect[4], float units_w, double cx, double cz, double mpu, double x, double z, float* px,
                     float* py) {
    const double k = units_w > 0 ? rect[2] / units_w : 1.0;  // pixels per canvas unit
    const double m = mpu > 0 ? mpu : 1.0;
    *px = static_cast<float>(rect[0] + rect[2] * 0.5 + (x - cx) / m * k);
    *py = static_cast<float>(rect[1] + rect[3] * 0.5 - (z - cz) / m * k);  // north up
}

std::vector<FogCell> fog_cells(const float rect[4], float units_w, double cx, double cz, double mpu,
                               const std::function<float(double, double)>& explored) {
    std::vector<FogCell> out;
    if (!explored || !(rect[2] > 0) || !(rect[3] > 0) || !(units_w > 0) || !(mpu > 0)) return out;
    const double k = rect[2] / units_w;          // pixels per unit
    const double ppm = k / mpu;                   // pixels per metre
    const double span = std::max(rect[2], rect[3]) / ppm;  // metres across
    constexpr int kMaxCells = 72;
    double cell = 8.0;                            // whole fog pixels
    while (span / cell > kMaxCells) cell += 8.0;
    const double left = cx - rect[2] * 0.5 / ppm, top = cz + rect[3] * 0.5 / ppm;  // the canvas's north-west corner
    const double x0 = std::floor(left / cell) * cell, z0 = std::ceil(top / cell) * cell;
    for (double z = z0; z > top - rect[3] / ppm - cell; z -= cell)
        for (double x = x0; x < left + rect[2] / ppm + cell; x += cell) {
            const float a = 1.0f - std::clamp(explored(x + cell * 0.5, z - cell * 0.5), 0.0f, 1.0f);
            if (a <= 0.01f) continue;
            float sx, sy;
            canvas_world_to(rect, units_w, cx, cz, mpu, x, z, &sx, &sy);
            const float w = static_cast<float>(cell * ppm);
            // clipped to the rect (the overlay clips too; this keeps cells off the frame)
            const float x1 = std::max(sx, rect[0]), y1 = std::max(sy, rect[1]);
            const float x2 = std::min(sx + w, rect[0] + rect[2]), y2 = std::min(sy + w, rect[1] + rect[3]);
            if (x2 <= x1 || y2 <= y1) continue;
            out.push_back({x1, y1, x2 - x1, y2 - y1, a});
        }
    return out;
}

}  // namespace emf::core
