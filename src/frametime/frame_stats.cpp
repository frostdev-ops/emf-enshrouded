#include "frame_stats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace emf::frametime {

Summary summarize(std::vector<float> ms) {
    Summary s;
    s.frames = ms.size();
    if (ms.empty()) return s;
    std::sort(ms.begin(), ms.end());
    double sum = 0;
    for (const float v : ms) {
        sum += v;
        s.over_25 += v > 25.0f;
        s.over_50 += v > 50.0f;
        s.over_100 += v > 100.0f;
    }
    const auto rank = [&](double p) {
        const auto i = static_cast<size_t>(std::ceil(p * static_cast<double>(ms.size())));
        return static_cast<double>(ms[i ? i - 1 : 0]);
    };
    s.seconds = sum / 1000.0;
    s.avg_ms = sum / static_cast<double>(ms.size());
    s.p50_ms = rank(0.50);
    s.p99_ms = rank(0.99);
    s.max_ms = ms.back();
    return s;
}

std::string summary_line(const Summary& s) {
    if (!s.frames) return "frames=0";
    char buf[256];
    std::snprintf(buf, sizeof buf, "frames=%zu fps=%.1f avg=%.1f p50=%.1f p99=%.1f max=%.1f >25ms=%zu >50ms=%zu >100ms=%zu",
                  s.frames, s.seconds > 0 ? static_cast<double>(s.frames) / s.seconds : 0.0, s.avg_ms, s.p50_ms,
                  s.p99_ms, s.max_ms, s.over_25, s.over_50, s.over_100);
    return buf;
}

}  // namespace emf::frametime
