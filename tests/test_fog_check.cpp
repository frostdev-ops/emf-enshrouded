// The shroud at a map teleport's landing (the owner's ask after the live check): the game's fog classifier rebuilt
// (0.75 majority; a fog level above the world's progress is deadly) and the ground service's fog phase: after the
// ground is found, the fog 1 m and 2 m over it and 3 m around is read until it is loaded, then answered with the ground
// (game-free).
#include <doctest.h>

#include <string>
#include <vector>

#include "core/fog_check.h"
#include "core/ground_service.h"

using namespace emf::core;

namespace {

FogReading weights(int index, float w, uint32_t progress, int other = -1, float ow = 0) {
    FogReading r;
    r.loaded = true;
    r.progress = progress;
    r.w[index] = w;
    if (other >= 0) r.w[other] = ow;
    return r;
}

}  // namespace

TEST_CASE("fog classify: the game's 0.75 rule: none, removed, dangerous at or under the progress, deadly above") {
    CHECK(fog_classify(weights(0, 1.0f, 3)).type == EMF_FOG_NONE);
    CHECK(fog_classify(weights(17, 0.8f, 3)).type == EMF_FOG_TEMPORARY);
    CHECK(fog_classify(weights(2, 1.0f, 3)).type == EMF_FOG_DANGEROUS);  // level 1 (index 2) <= progress 3
    CHECK(fog_classify(weights(3, 1.0f, 3)).type == EMF_FOG_DANGEROUS);  // index 3 = progress: not above
    const FogVerdict d = fog_classify(weights(4, 1.0f, 3));              // index 4 > progress 3
    CHECK(d.type == EMF_FOG_DEATH);
    CHECK(d.deadly == doctest::Approx(1.0f));
    const FogVerdict m = fog_classify(weights(0, 0.6f, 3, 5, 0.4f));  // an edge: no majority
    CHECK(m.type == EMF_FOG_MIXED);
    CHECK(m.deadly == doctest::Approx(0.4f));
}

TEST_CASE("fog check: the worst of the points; deadly when any point is deadly or the deadly weight reaches 0.25") {
    std::vector<FogVerdict> v{{EMF_FOG_NONE, 0}, {EMF_FOG_DANGEROUS, 0}};
    CHECK(fog_worst(v).type == EMF_FOG_DANGEROUS);
    v.push_back({EMF_FOG_MIXED, 0.3f});
    CHECK(fog_worst(v).type == EMF_FOG_DEATH);  // a mixed point with 30 % deadly fog
    CHECK(fog_worst(v).deadly == doctest::Approx(0.3f));
    std::vector<FogVerdict> w{{EMF_FOG_NONE, 0}, {EMF_FOG_MIXED, 0.1f}};
    CHECK(fog_worst(w).type == EMF_FOG_NONE);  // a little deadly fog at an edge: still none
}

TEST_CASE("fog check points: 1 m and 2 m over the ground, and 3 m around at 1 m") {
    const auto p = fog_points(100, 50, 200);
    REQUIRE(p.size() == 6u);
    CHECK(p[0][1] == doctest::Approx(51));
    CHECK(p[1][1] == doctest::Approx(52));
    CHECK(p[2][0] == doctest::Approx(103));
    CHECK(p[5][2] == doctest::Approx(197));
}

namespace {

uint64_t g_now = 1000;

struct Rig {
    std::vector<GroundAnswer> answers;
    std::vector<FogReading> fogs;  // what each fog read answers (the last repeats); empty: no fog reading on this build
    size_t next = 0;
    std::vector<double> fog_y;
    GroundService svc{deps()};
    GroundService::Deps deps() {
        GroundService::Deps d;
        d.limits = [](EmfWorld*, GroundLimits* l, std::string*) {
            l->box_top = 1200;
            l->grid_top = 1100;
            l->grid_bottom = -100;
            return true;
        };
        d.ray = [](EmfWorld*, double, double, double, double, RayResult* r) {
            r->loaded = r->hit = true;
            r->y = 300;
            return true;
        };
        d.fog = [this](EmfWorld*, double, double y, double, FogReading* out) {
            if (fogs.empty()) return false;
            *out = fogs[std::min(next, fogs.size() - 1)];
            ++next;
            fog_y.push_back(y);
            return true;
        };
        d.now_ms = [] { return g_now; };
        return d;
    }
    GroundService::Callback cb() {
        return [this](EmfWorld*, const GroundAnswer& a) { answers.push_back(a); };
    }
    void ticks(int n) {
        for (int i = 0; i < n; ++i) {
            g_now += 50;
            svc.on_server_tick(reinterpret_cast<EmfWorld*>(0x1000));
        }
    }
};

}  // namespace

TEST_CASE("ground service (fog): the ground's answer waits for the fog at its points and carries the worst") {
    g_now = 1000;
    Rig r;
    FogReading not_loaded;
    r.fogs = {not_loaded, not_loaded, weights(0, 1.0f, 2), weights(0, 1.0f, 2), weights(0, 1.0f, 2),
              weights(0, 1.0f, 2), weights(0, 1.0f, 2), weights(5, 1.0f, 2)};  // the last point: deadly
    std::string why;
    REQUIRE(r.svc.request(reinterpret_cast<EmfWorld*>(0x1000), 100, 200, r.cb(), &why));
    r.ticks(2);  // two agreeing rays: the ground
    CHECK(r.answers.empty());
    r.ticks(10);
    REQUIRE(r.answers.size() == 1u);
    CHECK(r.answers[0].status == EMF_GROUND_OK);
    CHECK(r.answers[0].y == doctest::Approx(300));
    CHECK(r.answers[0].fog == EMF_FOG_DEATH);
    CHECK(r.fog_y.front() == doctest::Approx(301));  // 1 m over the ground
}

TEST_CASE("ground service (fog): no fog reading on this build: answered at once, the fog unknown") {
    g_now = 1000;
    Rig r;  // fogs empty: the dep says it cannot read
    std::string why;
    REQUIRE(r.svc.request(reinterpret_cast<EmfWorld*>(0x1000), 100, 200, r.cb(), &why));
    r.ticks(2);
    REQUIRE(r.answers.size() == 1u);
    CHECK(r.answers[0].status == EMF_GROUND_OK);
    CHECK(r.answers[0].fog == EMF_FOG_UNKNOWN);
}

TEST_CASE("ground service (fog): fog that never loads in 5 s: the ground with the fog unknown") {
    g_now = 1000;
    Rig r;
    r.fogs = {FogReading{}};
    std::string why;
    REQUIRE(r.svc.request(reinterpret_cast<EmfWorld*>(0x1000), 100, 200, r.cb(), &why));
    r.ticks(2);
    r.ticks(90);  // 4.5 s
    CHECK(r.answers.empty());
    r.ticks(12);
    REQUIRE(r.answers.size() == 1u);
    CHECK(r.answers[0].fog == EMF_FOG_UNKNOWN);
}
