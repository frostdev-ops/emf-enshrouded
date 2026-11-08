#pragma once
// Frame-time statistics for EMF's frame-time recorder (frametime_layer.cpp): the intervals between the game's presents,
// in milliseconds. Free of Windows and Vulkan, so the unit tests run it.
#include <cstddef>
#include <string>
#include <vector>

namespace emf::frametime {

struct Summary {
    size_t frames = 0;
    double seconds = 0;  // the intervals' sum
    double avg_ms = 0, p50_ms = 0, p99_ms = 0, max_ms = 0;
    size_t over_25 = 0, over_50 = 0, over_100 = 0;  // hitches: frames longer than 25, 50 and 100 ms
};

// Percentiles by nearest rank (p99 of 100 frames is the 99th shortest).
Summary summarize(std::vector<float> intervals_ms);

// "frames=600 fps=60.0 avg=16.7 p50=16.7 p99=18.0 max=40.0 >25ms=2 >50ms=0 >100ms=0" ("frames=0" for none).
std::string summary_line(const Summary& s);

// The whole run's totals, kept without keeping every frame (the recorder may run for hours).
struct RunTotals {
    size_t frames = 0;
    double total_ms = 0, max_ms = 0;
    size_t over_25 = 0, over_50 = 0, over_100 = 0;
    void add(float ms);
};

// "frames=600 fps=60.0 avg=16.7 max=40.0 >25ms=2 >50ms=0 >100ms=0" ("frames=0" for none).
std::string run_line(const RunTotals& r);

}  // namespace emf::frametime
