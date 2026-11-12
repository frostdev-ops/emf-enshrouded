// Markers step 2 (map_input.h).
#include "core/map_input.h"

#include <windowsx.h>

#include <atomic>

namespace emf::core {

namespace {
std::atomic<bool> g_announced{false};
}

MapInput& map_input() {
    static MapInput in;
    return in;
}

void map_input_set_announced(bool on) { g_announced.store(on, std::memory_order_relaxed); }
bool map_input_announced() { return g_announced.load(std::memory_order_relaxed); }

MapInputEnv map_input_env(float client_w, float client_h, uint64_t now_ms) {
    MapInputEnv e;
    e.map_open = marker_sight(now_ms).map_open && marker_map_view(&e.view) && map_view_fresh(e.view, now_ms);
    e.announced = map_input_announced();
    e.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    e.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    e.client_w = client_w;
    e.client_h = client_h;
    e.now_ms = now_ms;
    return e;
}

void MapInput::set_done(std::function<void(double, double)> f) {
    std::lock_guard<std::mutex> lock(m_);
    done_ = std::move(f);
}

void MapInput::cancel() {
    std::lock_guard<std::mutex> lock(m_);
    hold_.closed();  // its release stays EMF's: the game never saw the press
}

bool map_view_fresh(const MapView& v, uint64_t now_ms) {
    return v.ms != 0 && (now_ms <= v.ms || now_ms - v.ms <= kMapGestureFreshMs);
}

void MapInput::set_click_listener(std::function<void(const EmfMapClick&)> f) {
    std::lock_guard<std::mutex> lock(m_);
    click_ = std::move(f);
}

void MapInput::set_hold_ms(uint64_t ms) {
    std::lock_guard<std::mutex> lock(m_);
    if (!hold_.holding()) hold_.set_hold_ms(ms);
    else pending_hold_ms_ = ms ? ms : kMapHoldMs;
}

void MapInput::take_pending_hold() {
    if (pending_hold_ms_ && !hold_.holding()) {
        hold_.set_hold_ms(pending_hold_ms_);
        pending_hold_ms_ = 0;
    }
}

MapInputResult MapInput::message(UINT msg, WPARAM wp, LPARAM lp, const MapInputEnv& env) {
    MapInputResult r;
    const float px = static_cast<float>(GET_X_LPARAM(lp)), py = static_cast<float>(GET_Y_LPARAM(lp));
    const float ui_x = env.client_w > 0 ? px * env.view.ui_w / env.client_w : px;
    const float ui_y = env.client_h > 0 ? py * env.view.ui_h / env.client_h : py;
    double x = 0, z = 0;
    const bool on_map = env.map_open && map_world_at(env.view, ui_x, ui_y, &x, &z);
    const bool shift = env.shift || (wp & MK_SHIFT) != 0;
    MapHoldOut o;
    int click_button = -1;
    std::function<void(const EmfMapClick&)> click;
    {
        std::lock_guard<std::mutex> lock(m_);
        hold_.set_enabled(env.announced && env.map_open);
        switch (msg) {
            case WM_LBUTTONDOWN:
                take_pending_hold();
                o = hold_.down(shift, on_map, x, z, env.now_ms);
                click_button = 0;
                break;
            case WM_LBUTTONDBLCLK:
                take_pending_hold();
                o = hold_.dblclk(shift, on_map, x, z, env.now_ms);
                click_button = 0;
                break;
            case WM_LBUTTONUP: o = hold_.up(env.now_ms); break;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
                o = hold_.right();
                right_ours_ = o.swallow;
                click_button = 1;
                if (msg == WM_RBUTTONDBLCLK && !o.swallow && right_rewrite_) r.rewrite_right_down = true;
                right_rewrite_ = o.swallow;
                break;
            case WM_RBUTTONUP:
                o.swallow = right_ours_;
                right_ours_ = false;
                break;
            case WM_MOUSEWHEEL: o = hold_.wheel(); break;
            default: break;
        }
        click = click_;
    }
    r.swallow = o.swallow;
    r.rewrite_down = o.rewrite_down;
    if (o.done) finish(o);
    if (!o.swallow && click_button >= 0 && on_map && click) {
        EmfMapClick c{};
        c.size = sizeof c;
        c.button = static_cast<uint32_t>(click_button);
        c.mods = ((wp & MK_SHIFT) ? EMF_MAP_SHIFT : 0u) | ((wp & MK_CONTROL) ? EMF_MAP_CTRL : 0u) |
                 (env.alt ? EMF_MAP_ALT : 0u);
        c.x = x;
        c.z = z;
        click(c);
    }
    return r;
}

void MapInput::frame(const MapInputEnv& env) {
    MapHoldOut o;
    {
        std::lock_guard<std::mutex> lock(m_);
        if (!hold_.holding()) return;
        o = env.map_open && !env.captured ? hold_.tick(env.now_ms) : hold_.closed();
    }
    if (o.done) finish(o);
}

MapRing MapInput::ring(uint64_t now_ms) const {
    std::lock_guard<std::mutex> lock(m_);
    MapRing r;
    r.holding = hold_.holding();
    r.progress = hold_.progress(now_ms);
    r.x = hold_.x();
    r.z = hold_.z();
    return r;
}

void MapInput::finish(const MapHoldOut& o) {
    std::function<void(double, double)> f;
    {
        std::lock_guard<std::mutex> lock(m_);
        f = done_;
    }
    if (f) f(o.x, o.z);
}

}  // namespace emf::core
