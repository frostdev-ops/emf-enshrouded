// Markers step 2 (API v34): ground_at's service: requests answered after server steps from a (fake) voxel ray.
#include <doctest.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "core/ground_service.h"

using namespace emf::core;

namespace {

uint64_t g_now = 1000;

struct Answer {
    bool ok;
    double y;
    std::string why;
    uint32_t status;
    double hop_y;
};

struct Rig {
    std::vector<RayResult> rays;  // what the ray answers, one per tick (the last repeats)
    size_t next = 0;
    bool limits_ok = true;
    bool sides = false;
    std::vector<Answer> answers;
    GroundService svc{deps()};
    GroundService::Deps deps() {
        GroundService::Deps d;
        d.limits = [this](EmfWorld*, GroundLimits* l, std::string* why) {
            if (!limits_ok) {
                *why = "the world's height limit is not known yet";
                return false;
            }
            l->box_top = 1200;
            l->grid_top = 1100;
            l->grid_bottom = -100;
            l->has_sides = sides;
            l->box_min_x = l->box_min_z = 64;
            l->box_max_x = l->box_max_z = 10176;
            return true;
        };
        d.ray = [this](EmfWorld*, double, double, double, double, RayResult* out) {
            if (rays.empty()) return false;
            *out = rays[std::min(next, rays.size() - 1)];
            ++next;
            return true;
        };
        d.now_ms = [] { return g_now; };
        return d;
    }
    GroundService::Callback cb() {
        return [this](EmfWorld*, const GroundAnswer& a) {
            answers.push_back({a.status == EMF_GROUND_OK, a.y, a.why, a.status, a.hop_y});
        };
    }
};

EmfWorld* W() { return reinterpret_cast<EmfWorld*>(0x1000); }
EmfWorld* W2() { return reinterpret_cast<EmfWorld*>(0x2000); }

RayResult hit(double y) {
    RayResult r;
    r.loaded = r.hit = true;
    r.y = y;
    return r;
}

}  // namespace

TEST_CASE("ground service: a loaded spot answers with the ground after two agreeing steps") {
    g_now = 1000;
    Rig r;
    r.rays = {hit(300), hit(300.2)};
    std::string why;
    REQUIRE(r.svc.request(W(), 100, 200, r.cb(), &why));
    r.svc.on_server_tick(W());
    CHECK(r.answers.empty());
    r.svc.on_server_tick(W());
    REQUIRE(r.answers.size() == 1);
    CHECK(r.answers[0].ok);
    CHECK(r.answers[0].status == EMF_GROUND_OK);
    CHECK(r.answers[0].y == doctest::Approx(300.2));
    CHECK(r.svc.waiting() == 0u);
}

TEST_CASE("ground service: a place that does not load in 12 s is answered 'not loaded'") {
    g_now = 1000;
    Rig r;
    r.rays = {RayResult{}};
    std::string why;
    REQUIRE(r.svc.request(W(), 100, 200, r.cb(), &why));
    for (int i = 0; i < 10; ++i) {  // 6 s: still loading (the game takes about 5 s for a column)
        g_now += 600;
        r.svc.on_server_tick(W());
    }
    CHECK(r.answers.empty());
    for (int i = 0; i < 12; ++i) {
        g_now += 600;
        r.svc.on_server_tick(W());
    }
    REQUIRE(r.answers.size() == 1);
    CHECK_FALSE(r.answers[0].ok);
    CHECK(r.answers[0].why.find("not loaded") != std::string::npos);
    CHECK(r.answers[0].status == EMF_GROUND_NOT_LOADED);
    CHECK(r.answers[0].hop_y == doctest::Approx(1100));  // min(box top 1200 - 8, grid top 1100): a player may wait there
}

TEST_CASE("ground service: refused at once outside the world, without limits, or 64 waiting; a world reset ends the rest") {
    g_now = 1000;
    Rig r;
    std::string why;
    CHECK_FALSE(r.svc.request(W(), -5, 200, r.cb(), &why));
    CHECK_FALSE(why.empty());
    r.limits_ok = false;
    CHECK_FALSE(r.svc.request(W(), 100, 200, r.cb(), &why));
    CHECK(why.find("height limit") != std::string::npos);
    r.limits_ok = true;
    for (int i = 0; i < 64; ++i) REQUIRE(r.svc.request(W(), 100, 200, r.cb(), &why));
    CHECK_FALSE(r.svc.request(W(), 100, 200, r.cb(), &why));
    r.svc.on_server_tick(W2());  // another world's step: not these requests
    CHECK(r.answers.empty());
    r.svc.world_reset();
    CHECK(r.answers.size() == 64u);
    CHECK_FALSE(r.answers[0].ok);
    CHECK(r.svc.waiting() == 0u);
}

TEST_CASE("ground service: a ray that cannot be cast ends the request with a reason") {
    g_now = 1000;
    Rig r;  // no rays: the ray function says it could not run
    std::string why;
    REQUIRE(r.svc.request(W(), 100, 200, r.cb(), &why));
    r.svc.on_server_tick(W());
    REQUIRE(r.answers.size() == 1);
    CHECK_FALSE(r.answers[0].ok);
    CHECK(r.answers[0].status == EMF_GROUND_FAILED);
    CHECK_FALSE(r.answers[0].why.empty());
}


TEST_CASE("ground service (review M2): a spot outside the reset box's sides is refused (the game would send a player home)") {
    g_now = 1000;
    Rig r;
    r.sides = true;
    std::string why;
    CHECK_FALSE(r.svc.request(W(), 40, 200, r.cb(), &why));
    CHECK(why.find("outside") != std::string::npos);
    CHECK_FALSE(r.svc.request(W(), 5000, 10200, r.cb(), &why));
    CHECK(r.svc.request(W(), 5000, 5000, r.cb(), &why));
}

// The water at a landing (private research water_2026-11-12): the owner landed on a lake bed after a map teleport.
namespace {
struct WaterRig : Rig {
    WaterReading reading;
    bool readable = true;
    std::vector<std::array<double, 4>> asked;  // x, y, z, max_down
    std::vector<double> fog_y;
    std::vector<std::pair<bool, double>> water;  // per answer: water, water_y
    GroundService wsvc{wdeps()};
    GroundService::Deps wdeps() {
        GroundService::Deps d = deps();
        d.water = [this](EmfWorld*, double x, double y, double z, float max_down, WaterReading* out) {
            asked.push_back({x, y, z, max_down});
            if (!readable) return false;
            *out = reading;
            return true;
        };
        d.fog = [this](EmfWorld*, double, double y, double, FogReading* f) {
            fog_y.push_back(y);
            f->loaded = true;
            f->w[0] = 1;  // no fog
            return true;
        };
        return d;
    }
    GroundService::Callback wcb() {
        return [this](EmfWorld*, const GroundAnswer& a) {
            answers.push_back({a.status == EMF_GROUND_OK, a.y, a.why, a.status, a.hop_y});
            water.push_back({a.water, a.water_y});
        };
    }
    void run(double ground) {
        rays = {hit(ground), hit(ground)};
        std::string why;
        REQUIRE(wsvc.request(W(), 100, 200, wcb(), &why));
        wsvc.on_server_tick(W());
        wsvc.on_server_tick(W());
        REQUIRE(answers.size() == 1);
    }
};
}  // namespace

TEST_CASE("ground service: over a lake the answer has the water: its surface, the ground kept; the shroud read at the surface") {
    g_now = 1000;
    WaterRig r;
    r.reading.loaded = r.reading.water = true;
    r.reading.surface_y = 304.0;
    r.run(300.0);
    CHECK(r.answers[0].ok);
    CHECK(r.answers[0].y == doctest::Approx(300.0));  // the ground (a mod placing something wants it)
    CHECK(r.water[0].first);
    CHECK(r.water[0].second == doctest::Approx(304.0));
    REQUIRE_FALSE(r.asked.empty());
    CHECK(r.asked[0][1] == doctest::Approx(300.5));  // read half a metre over the bed, 1 m down at most
    CHECK(r.asked[0][3] == doctest::Approx(1.0));
    REQUIRE_FALSE(r.fog_y.empty());
    CHECK(r.fog_y[0] == doctest::Approx(305.0));  // 1 m over the surface, where a player lands
}

TEST_CASE("ground service: shallow water (under 1 m), water not loaded yet, or no water query: no water in the answer") {
    for (int c = 0; c < 4; ++c) {
        CAPTURE(c);
        g_now = 1000;
        WaterRig r;
        r.reading.loaded = true;
        r.reading.water = true;
        r.reading.surface_y = 300.6;           // c 0: 0.6 m deep
        if (c == 1) r.reading.loaded = false;  // the world's water still loading
        if (c == 2) r.reading.water = false;   // dry land
        if (c == 3) r.readable = false;        // no water query on this build
        r.run(300.0);
        CHECK(r.answers[0].ok);
        CHECK_FALSE(r.water[0].first);
        REQUIRE_FALSE(r.fog_y.empty());
        CHECK(r.fog_y[0] == doctest::Approx(301.0));  // 1 m over the ground
    }
}

// Review: a player who sinks after a water landing ends on the bed: the shroud there counts too.
TEST_CASE("ground service: over a lake the shroud is read at the surface and at the bed; a deadly bed makes the spot deadly") {
    g_now = 1000;
    WaterRig r;
    r.reading.loaded = r.reading.water = true;
    r.reading.surface_y = 304.0;
    r.run(300.0);
    bool at_surface = false, at_bed = false;
    for (double y : r.fog_y) {
        at_surface |= std::abs(y - 305.0) < 0.01;
        at_bed |= std::abs(y - 301.0) < 0.01;
    }
    CHECK(at_surface);
    CHECK(at_bed);
}

// Review: a misread plane (an unknown game build, a waterfall joined to its pool) must not send a player into the sky.
TEST_CASE("ground service: a water surface more than 64 m over the ground is not believed (no water in the answer)") {
    g_now = 1000;
    WaterRig r;
    r.reading.loaded = r.reading.water = true;
    r.reading.surface_y = 380.0;
    r.run(300.0);
    CHECK(r.answers[0].ok);
    CHECK_FALSE(r.water[0].first);
}
