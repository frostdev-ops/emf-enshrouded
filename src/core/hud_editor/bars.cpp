#include "core/hud_editor/bars.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>

#include "core/hud_editor/layout.h"

namespace emf::core::hud {

namespace {

struct BarLive {
    std::atomic<bool> seen{false};
    std::atomic<uint64_t> last_seen_ms{0};
    std::atomic<uint64_t> rect_a{0}, rect_b{0};  // x,y and w,h as float pairs
};
BarLive g_bar_live[kBarCount];

// The game's colour block of each bar (one UiColour per atomic: the page and the log read them on another thread; a torn
// block between two fields is harmless there).
struct BarGame {
    std::atomic<bool> seen{false};
    std::atomic<uint64_t> field[6]{};
};
BarGame g_bar_game[kBarCount];
std::atomic<uint64_t> g_recoloured{0}, g_panel_swaps{0}, g_panel_misses{0};

uint64_t pack2(float a, float b) { return std::bit_cast<uint64_t>(std::array<float, 2>{a, b}); }
std::array<float, 2> unpack2(uint64_t v) { return std::bit_cast<std::array<float, 2>>(v); }

// the UI thread's state between vitalBars' open and close
thread_local uint8_t* t_vitals = nullptr;
thread_local int t_calls = 0;
thread_local uintptr_t t_base = 0;
thread_local std::array<BarRect, kBarCount> t_cur{}, t_last{};

// the panel colour: health's style and its frame colour as the bar read it this frame, and the armed swap: the panel's
// uiLayerOpen call gets `emf` instead of `game` (the same 8 bytes the widget loads from the bundle)
thread_local const void* t_health_style = nullptr;
thread_local UiColour t_health_frame{};
struct PanelArm {
    bool armed = false;
    uint64_t game = 0, emf = 0;
};
thread_local PanelArm t_arm;
std::atomic<uint8_t> g_panel_status{static_cast<uint8_t>(PanelStatus::Unknown)};

uint64_t pack_colour(UiColour c) {
    uint64_t v = 0;
    std::memcpy(&v, &c, sizeof v);
    return v;
}


}  // namespace

int bar_index(uintptr_t data, uintptr_t base) {
    if (data < base + kBarDataFirst) return -1;
    const uintptr_t rel = data - base - kBarDataFirst;
    if (rel % kBarDataStride != 0) return -1;
    const uintptr_t k = rel / kBarDataStride;
    return k < kBarCount ? static_cast<int>(k) : -1;
}

std::array<float, kBarCount> order_dy(const std::array<BarRect, kBarCount>& game, const BarOrder& order,
                                      const std::array<bool, kBarCount>& hidden) {
    std::array<float, kBarCount> dy{};
    if (!valid_order(order)) return dy;
    int first = -1, second = -1;
    float top = 0;
    for (size_t b = 0; b < kBarCount; ++b) {
        if (!game[b].seen) continue;
        if (first < 0) {
            first = static_cast<int>(b);
            top = game[b].y;
        } else if (second < 0) {
            second = static_cast<int>(b);
        }
        top = (std::min)(top, game[b].y);
    }
    if (first < 0) return dy;
    float gap = kBarGap;
    if (second >= 0) {
        const BarRect &f = game[static_cast<size_t>(first)], &s = game[static_cast<size_t>(second)];
        const float g = s.y - (f.y + f.h);
        if (std::isfinite(g) && g >= 0) gap = g;
    }
    float y = top;
    for (uint8_t b : order) {
        if (!game[b].seen || hidden[b]) continue;
        dy[b] = y - game[b].y;
        y += game[b].h + gap;
    }
    return dy;
}

float grow_up_dy(float list_y, float list_h, float column_h, const Xform& top, float ui_h) {
    if (!(top.scale > 0) || !std::isfinite(top.scale) || !std::isfinite(top.ty) || !std::isfinite(list_y) ||
        !(list_h >= 0) || !std::isfinite(list_h) || !std::isfinite(column_h) || !(ui_h > 0) || !std::isfinite(ui_h))
        return 0;
    const float desired = column_h - list_h;
    const float upper = (ui_h - top.ty) / top.scale - list_y - list_h;  // the bottom stays on screen
    const float lower = -top.ty / top.scale - list_y;                    // the top stays on screen
    return (std::max)((std::min)(desired, upper), lower);
}

void bars_vitals_open(uint8_t* node) {
    panel_disarm();  // the widget's panel call was not seen last frame
    t_health_style = nullptr;
    // a bar the game did not draw in the frame that ended (stamina while full): no area left for the HUD page to pick
    for (size_t b = 0; b < kBarCount; ++b)
        if (!t_cur[b].seen) g_bar_live[b].rect_b.store(0, std::memory_order_relaxed);
    t_vitals = node;
    t_calls = 0;
    t_base = 0;
    t_last = t_cur;
    t_cur = {};
}

void bars_vitals_close(const HudSnapshot* snap) {
    t_vitals = nullptr;
    t_arm = PanelArm{};
    if (!snap || !snap->active || !snap->panel.set || !t_health_style) return;  // health's bar ran this frame
    t_arm = PanelArm{true, pack_colour(t_health_frame), pack_colour(with_colour(t_health_frame, snap->panel))};
}

bool panel_tint(uint64_t* tint, uint64_t unused, uint64_t enable, const void* rect) {
    if (!t_arm.armed) return false;
    const PanelArm a = t_arm;
    t_arm.armed = false;  // first: only the first layer call after the close can be the panel's
    if (*tint != a.game || unused != 0 || rect != nullptr || (enable & 0xff) != 1) {
        g_panel_misses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    *tint = a.emf;
    g_panel_swaps.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void panel_disarm() { t_arm.armed = false; }

void set_panel_available(bool available) {
    g_panel_status.store(static_cast<uint8_t>(available ? PanelStatus::Available : PanelStatus::Unavailable),
                         std::memory_order_relaxed);
}

PanelStatus panel_status() { return static_cast<PanelStatus>(g_panel_status.load(std::memory_order_relaxed)); }

BarCall bar_before(void* data, const void* style, const HudSnapshot* snap, UiSpace ui) {
    BarCall c;
    if (!t_vitals) return c;
    const uintptr_t d = reinterpret_cast<uintptr_t>(data);
    if (t_calls++ == 0) {
        if (d < kBarDataFirst) return c;
        t_base = d - kBarDataFirst;  // the first call is always health's
    }
    c.bar = bar_index(d, t_base);
    if (c.bar >= 0 && style) {
        StyleBlock game;
        std::memcpy(&game, style, sizeof game);  // the caller guards the read
        if (c.bar == kBarHealth) {
            t_health_style = style;
            t_health_frame = game[kFrame];
        }
        BarGame& g = g_bar_game[static_cast<size_t>(c.bar)];
        for (size_t f = 0; f < game.size(); ++f) g.field[f].store(std::bit_cast<uint64_t>(game[f]), std::memory_order_relaxed);
        g.seen.store(true, std::memory_order_relaxed);
        if (snap && snap->active && snap->colours[static_cast<size_t>(c.bar)].any()) {
            c.style = recoloured(game, snap->colours[static_cast<size_t>(c.bar)]);
            c.recolour = true;
            g_recoloured.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (c.bar < 0 || !snap || !snap->active) return c;
    std::array<bool, kBarCount> hidden{};
    for (int b = 0; b < static_cast<int>(kBarCount); ++b)
        hidden[static_cast<size_t>(b)] = snap->edits[static_cast<size_t>(bar_element(b))].hidden;
    const ElementEdit& e = snap->edits[static_cast<size_t>(bar_element(c.bar))];
    float dx = percent_to_units(e.x_percent, ui.w);
    float dy = percent_to_units(e.y_percent, ui.h) + order_dy(t_last, snap->order, hidden)[static_cast<size_t>(c.bar)];
    if (e.hidden) {
        dx -= kHideShift;
        dy -= kHideShift;
    }
    if (dx == 0 && dy == 0) return c;
    c.push = shift_open(t_vitals, dx, dy);
    if (c.push.pushed) {
        c.dx = dx;
        c.dy = dy;
    }
    return c;
}

void bar_after(const BarCall& c, void* result, uint64_t now_ms) {
    if (c.push.pushed) scale_close(c.push);
    if (c.bar < 0 || !result) return;
    float* r = static_cast<float*>(result);
    t_cur[static_cast<size_t>(c.bar)] = BarRect{r[0], r[1], r[2], r[3], true};
    r[0] += c.dx;
    r[1] += c.dy;
    BarLive& l = g_bar_live[static_cast<size_t>(c.bar)];
    l.rect_a.store(pack2(r[0], r[1]), std::memory_order_relaxed);
    l.rect_b.store(pack2(r[2], r[3]), std::memory_order_relaxed);
    l.last_seen_ms.store(now_ms, std::memory_order_relaxed);
    l.seen.store(true, std::memory_order_relaxed);
}

void* bar_style(BarCall& c, void* style) { return c.recolour ? static_cast<void*>(c.style.data()) : style; }

void* bar_call_path(void* out, void* data, void* style, BarFn original, const HudSnapshot* snap, UiSpace ui,
                    uint64_t now_ms) {
    BarCall c = bar_before(data, style, snap, ui);
    void* r = original(out, data, bar_style(c, style));
    bar_after(c, r, now_ms);
    return r;
}

LiveInfo bar_live(int bar) {
    LiveInfo li;
    if (bar < 0 || bar >= static_cast<int>(kBarCount)) return li;
    const BarLive& l = g_bar_live[static_cast<size_t>(bar)];
    li.seen = l.seen.load(std::memory_order_relaxed);
    li.last_seen_ms = l.last_seen_ms.load(std::memory_order_relaxed);
    const auto a = unpack2(l.rect_a.load(std::memory_order_relaxed)), b = unpack2(l.rect_b.load(std::memory_order_relaxed));
    li.rect[0] = a[0];
    li.rect[1] = a[1];
    li.rect[2] = b[0];
    li.rect[3] = b[1];
    return li;
}

std::optional<StyleBlock> bar_game_colours(int bar) {
    if (bar < 0 || bar >= static_cast<int>(kBarCount)) return std::nullopt;
    const BarGame& g = g_bar_game[static_cast<size_t>(bar)];
    if (!g.seen.load(std::memory_order_relaxed)) return std::nullopt;
    StyleBlock b;
    for (size_t f = 0; f < b.size(); ++f) b[f] = std::bit_cast<UiColour>(g.field[f].load(std::memory_order_relaxed));
    return b;
}

ColourStats colour_stats() {
    ColourStats s;
    s.recoloured = g_recoloured.load(std::memory_order_relaxed);
    s.panel_swaps = g_panel_swaps.load(std::memory_order_relaxed);
    s.panel_misses = g_panel_misses.load(std::memory_order_relaxed);
    return s;
}

void bars_reset_for_tests() {
    t_vitals = nullptr;
    t_calls = 0;
    t_base = 0;
    t_cur = {};
    t_last = {};
    for (auto& l : g_bar_live) {
        l.seen = false;
        l.last_seen_ms = 0;
        l.rect_a = 0;
        l.rect_b = 0;
    }
    for (auto& g : g_bar_game) {
        g.seen = false;
        for (auto& f : g.field) f = 0;
    }
    g_recoloured = 0;
    g_panel_swaps = 0;
    g_panel_misses = 0;
    t_health_style = nullptr;
    t_health_frame = UiColour{};
    t_arm = PanelArm{};
    g_panel_status = static_cast<uint8_t>(PanelStatus::Unknown);
}

}  // namespace emf::core::hud
