#include <doctest/doctest.h>

#include <vector>

#include "frametime/frame_stats.h"

using emf::frametime::summarize;
using emf::frametime::summary_line;

TEST_CASE("frame times: no frames gives an empty summary") {
    const auto s = summarize({});
    CHECK(s.frames == 0u);
    CHECK(s.max_ms == 0.0);
    CHECK(summary_line(s) == "frames=0");
}

TEST_CASE("frame times: average, median, 99th percentile, worst frame and hitch counts") {
    std::vector<float> ms(98, 10.0f);
    ms.push_back(120.0f);  // order does not matter
    ms.insert(ms.begin() + 10, 60.0f);
    const auto s = summarize(ms);
    CHECK(s.frames == 100u);
    CHECK(s.seconds == doctest::Approx(1.16));
    CHECK(s.avg_ms == doctest::Approx(11.6));
    CHECK(s.p50_ms == doctest::Approx(10.0));
    CHECK(s.p99_ms == doctest::Approx(60.0));  // nearest rank: the 99th of 100
    CHECK(s.max_ms == doctest::Approx(120.0));
    CHECK(s.over_25 == 2u);
    CHECK(s.over_50 == 2u);
    CHECK(s.over_100 == 1u);
    CHECK(summary_line(s) == "frames=100 fps=86.2 avg=11.6 p50=10.0 p99=60.0 max=120.0 >25ms=2 >50ms=2 >100ms=1");
}

TEST_CASE("frame times: a frame exactly at a hitch limit is not a hitch") {
    const auto s = summarize({25.0f, 50.0f, 100.0f, 25.1f});
    CHECK(s.over_25 == 3u);
    CHECK(s.over_50 == 1u);
    CHECK(s.over_100 == 0u);
}
