// Markers step 2 (API v34): ground_at's service: requests answered after server steps from a (fake) voxel ray.
#include <doctest.h>

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

TEST_CASE("ground service: a place that does not load in 5 s is answered 'not loaded'") {
    g_now = 1000;
    Rig r;
    r.rays = {RayResult{}};
    std::string why;
    REQUIRE(r.svc.request(W(), 100, 200, r.cb(), &why));
    for (int i = 0; i < 10; ++i) {
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
