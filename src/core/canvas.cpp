// Markers step 3 (canvas.h).
#include "core/canvas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace emf::core {

namespace {

constexpr float kMaxUnits = 4096.0f;
constexpr size_t kTextMax = 256, kPictureIdMax = 64;

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
               std::isfinite(c.a[2]) && c.a[2] > 0 && c.a[2] <= 1000;
    }
    for (int i = 0; i < n; ++i)
        if (!fin(c.a[i])) return false;
    switch (c.kind) {
        case EMF_CANVAS_RECT_CMD: return c.a[2] >= 0 && c.a[3] >= 0 && c.a[4] >= 0;
        case EMF_CANVAS_CIRCLE_CMD: return c.a[2] >= 0 && c.a[3] >= 0;
        case EMF_CANVAS_LINE: return c.a[4] >= 0;
        case EMF_CANVAS_TEXT: return c.s && *c.s && c.a[2] > 0 && c.a[2] <= 200;
        case EMF_CANVAS_IMAGE: return c.s && *c.s && c.a[2] >= 0 && c.a[3] >= 0;
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
        std::memcpy(c.a, cmds[i].a, sizeof c.a);
        if (cmds[i].s) {
            const size_t cap = c.kind == EMF_CANVAS_IMAGE ? kPictureIdMax : kTextMax;
            c.s.assign(cmds[i].s, strnlen(cmds[i].s, cap));
        }
        list.push_back(std::move(c));
    }
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

}  // namespace emf::core
