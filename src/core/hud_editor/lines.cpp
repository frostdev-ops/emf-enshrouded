#include "core/hud_editor/lines.h"

#include <array>
#include <atomic>
#include <bit>

namespace emf::core::hud {

namespace {

constexpr int kPlaces = 3;

struct LinesLive {
    std::atomic<bool> seen{false};
    std::atomic<uint64_t> last_seen_ms{0};
    std::atomic<uint64_t> rect_a{0}, rect_b{0};  // x,y and w,h as float pairs
};
LinesLive g_lines[kPlaces];

uint64_t pack2(float a, float b) { return std::bit_cast<uint64_t>(std::array<float, 2>{a, b}); }
std::array<float, 2> unpack2(uint64_t v) { return std::bit_cast<std::array<float, 2>>(v); }

}  // namespace

void set_lines_live(int place, const float rect_units[4], uint64_t now_ms) {
    if (place < 0 || place >= kPlaces || !rect_units) return;
    LinesLive& l = g_lines[place];
    l.rect_a.store(pack2(rect_units[0], rect_units[1]), std::memory_order_relaxed);
    l.rect_b.store(pack2(rect_units[2], rect_units[3]), std::memory_order_relaxed);
    l.last_seen_ms.store(now_ms, std::memory_order_relaxed);
    l.seen.store(true, std::memory_order_relaxed);
}

void lines_not_drawn(int place) {
    if (place < 0 || place >= kPlaces) return;
    g_lines[place].rect_b.store(0, std::memory_order_relaxed);
}

LiveInfo lines_live(int place) {
    LiveInfo li;
    if (place < 0 || place >= kPlaces) return li;
    const LinesLive& l = g_lines[place];
    li.seen = l.seen.load(std::memory_order_relaxed);
    li.last_seen_ms = l.last_seen_ms.load(std::memory_order_relaxed);
    const auto a = unpack2(l.rect_a.load(std::memory_order_relaxed)), b = unpack2(l.rect_b.load(std::memory_order_relaxed));
    li.rect[0] = a[0];
    li.rect[1] = a[1];
    li.rect[2] = b[0];
    li.rect[3] = b[1];
    return li;
}

void lines_reset_for_tests() {
    for (auto& l : g_lines) {
        l.seen = false;
        l.last_seen_ms = 0;
        l.rect_a = 0;
        l.rect_b = 0;
    }
}

}  // namespace emf::core::hud
