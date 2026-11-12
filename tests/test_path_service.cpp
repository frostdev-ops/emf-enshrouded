// path_find (API v37): the queries' life in EMF's path service, over a fake engine (the game's create / poll / destroy).
#include <doctest.h>

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "core/path_service.h"
#include "emf/emf_api.h"

using namespace emf::core;

namespace {
struct FakeEngine {
    uint64_t next = 100;
    std::map<uint64_t, PathAnswer> answers;  // set: the game finished that query
    std::vector<uint64_t> created, destroyed;
    std::vector<EmfPathQuery> queries;
    std::vector<uint32_t> owners;
    std::vector<uint32_t> gone;  // entities that no longer exist
    PathEngine engine() {
        PathEngine e;
        e.create = [this](const EmfPathQuery& q, uint32_t owner) {
            queries.push_back(q);
            owners.push_back(owner);
            created.push_back(next);
            return next++;
        };
        e.poll = [this](uint64_t id) {
            auto it = answers.find(id);
            return it == answers.end() ? PathAnswer{} : it->second;
        };
        e.destroy = [this](uint64_t id, uint32_t) { destroyed.push_back(id); };
        e.exists = [this](uint32_t entity) {
            for (uint32_t g : gone)
                if (g == entity) return false;
            return true;
        };
        return e;
    }
    void finish(uint64_t id, uint32_t game_result, std::vector<EmfPathPoint> pts) {
        PathAnswer a;
        a.done = true;
        a.game_result = game_result;
        a.points = std::move(pts);
        answers[id] = a;
    }
};

EmfPathQuery query(float tx = 10, float tz = 0) {
    EmfPathQuery q{};
    q.size = sizeof q;
    q.from[0] = 0;
    q.from[1] = 50;
    q.from[2] = 0;
    q.to[0] = tx;
    q.to[1] = 50;
    q.to[2] = tz;
    return q;
}

void cb_noop(EmfWorld*, uint32_t, const EmfPathResult*, const EmfPathPoint*, void*) {}
}  // namespace

TEST_CASE("path service: a query is created on the next step, polled until done, destroyed, and answered once") {
    PathService s;
    FakeEngine fe;
    std::string why;
    const uint32_t id = s.request(query(), 7, cb_noop, nullptr, 1000, &why);
    REQUIRE(id != 0);
    CHECK(s.open() == 1u);
    CHECK(s.step(fe.engine(), 1010).empty());  // created, no answer yet
    REQUIRE(fe.created.size() == 1u);
    CHECK(fe.owners[0] == 7u);
    CHECK(s.step(fe.engine(), 1020).empty());  // still searching
    fe.finish(fe.created[0], 1, {{3, 50, 0, 0}, {6, 51, 0, 2}, {10, 51, 0, 0}});
    const auto done = s.step(fe.engine(), 1030);
    REQUIRE(done.size() == 1u);
    CHECK(done[0].id == id);
    CHECK(done[0].cb == cb_noop);
    CHECK(done[0].result.size == sizeof(EmfPathResult));
    CHECK(done[0].result.status == EMF_PATH_FOUND);
    CHECK(done[0].result.reaches == 1);
    CHECK(done[0].result.count == 3u);
    CHECK(done[0].points[1].move == 2u);
    CHECK(done[0].result.length == doctest::Approx(3 + std::sqrt(9.0 + 1.0) + 4));  // from `from` along the points
    CHECK(fe.destroyed == fe.created);
    CHECK(s.open() == 0u);
    CHECK(s.step(fe.engine(), 1040).empty());  // answered once
}

TEST_CASE("path service: FOUND that stops short of the target (the closest-node fallback) does not reach it") {
    PathService s;
    FakeEngine fe;
    EmfPathQuery q = query(40, 0);
    q.closest = 1;
    REQUIRE(s.request(q, 7, cb_noop, nullptr, 0, nullptr));
    s.step(fe.engine(), 10);
    fe.finish(fe.created[0], 1, {{5, 50, 0, 0}, {20, 50, 0, 0}});  // ends at a wall 20 m short
    const auto done = s.step(fe.engine(), 20);
    REQUIRE(done.size() == 1u);
    CHECK(done[0].result.status == EMF_PATH_FOUND);
    CHECK(done[0].result.reaches == 0);
}

TEST_CASE("path service: the game's results become EMF's statuses; FOUND with no points is no path") {
    CHECK(path_status(1, 3) == EMF_PATH_FOUND);
    CHECK(path_status(1, 0) == EMF_PATH_NO_PATH);
    CHECK(path_status(2, 0) == EMF_PATH_NO_PATH);
    CHECK(path_status(3, 0) == EMF_PATH_NO_PATH);  // NoFreePath
    CHECK(path_status(4, 0) == EMF_PATH_NO_PATH);  // NoOccupiedPath
    CHECK(path_status(5, 0) == EMF_PATH_NO_START);
    CHECK(path_status(6, 0) == EMF_PATH_NO_TARGET);
    CHECK(path_status(7, 0) == EMF_PATH_TOO_LONG);
    CHECK(path_status(8, 0) == EMF_PATH_NO_MEMORY);
    CHECK(path_status(9, 0) == EMF_PATH_NO_MEMORY);
    CHECK(path_status(0, 0) == EMF_PATH_OTHER);
    CHECK(path_status(42, 0) == EMF_PATH_OTHER);
}

TEST_CASE("path service: refused at once: a small size, bad numbers, too many nodes, unknown moves, 32 open") {
    PathService s;
    std::string why;
    EmfPathQuery q = query();
    q.size = 8;
    CHECK(s.request(q, 7, cb_noop, nullptr, 0, &why) == 0u);
    q = query();
    q.to[1] = std::numeric_limits<float>::quiet_NaN();
    CHECK(s.request(q, 7, cb_noop, nullptr, 0, &why) == 0u);
    CHECK(why.find("numbers") != std::string::npos);
    q = query(1e7f, 0);
    CHECK(s.request(q, 7, cb_noop, nullptr, 0, &why) == 0u);
    q = query(150, 150);  // 212 m: the game answers nothing over 200 m without searching
    CHECK(s.request(q, 7, cb_noop, nullptr, 0, &why) == 0u);
    CHECK(why.find("200 m") != std::string::npos);
    CHECK(s.request(query(140, 140), 7, cb_noop, nullptr, 0, &why) != 0u);  // 198 m
    q = query();
    q.max_nodes = 20001;
    CHECK(s.request(q, 7, cb_noop, nullptr, 0, &why) == 0u);
    q = query();
    q.width = -1;
    CHECK(s.request(q, 7, cb_noop, nullptr, 0, &why) == 0u);
    q = query();
    q.moves = 0x8000;
    CHECK(s.request(q, 7, cb_noop, nullptr, 0, &why) == 0u);
    CHECK(s.request(query(), 0, cb_noop, nullptr, 0, &why) == 0u);  // no owner
    CHECK(why.find("owner") != std::string::npos);
    PathService full;
    for (size_t i = 0; i < kMaxOpenPaths; ++i) REQUIRE(full.request(query(), 7, cb_noop, nullptr, 0, &why) != 0u);
    CHECK(full.request(query(), 7, cb_noop, nullptr, 0, &why) == 0u);
    CHECK(why.find("32") != std::string::npos);
}

TEST_CASE("path service: at most 8 new queries a step; each id once") {
    PathService s;
    FakeEngine fe;
    std::vector<uint32_t> ids;
    for (int i = 0; i < 12; ++i) ids.push_back(s.request(query(), 7, cb_noop, nullptr, 0, nullptr));
    for (size_t i = 1; i < ids.size(); ++i) CHECK(ids[i] != ids[i - 1]);
    s.step(fe.engine(), 10);
    CHECK(fe.created.size() == kPathCreatesPerStep);
    s.step(fe.engine(), 20);
    CHECK(fe.created.size() == 12u);
}

TEST_CASE("path service: cancel before and after the game has it; the owner gone; no answer in 10 s") {
    PathService s;
    FakeEngine fe;
    const uint32_t a = s.request(query(), 7, cb_noop, nullptr, 0, nullptr);
    CHECK(s.cancel(a));
    auto done = s.step(fe.engine(), 10);
    REQUIRE(done.size() == 1u);
    CHECK(done[0].result.status == EMF_PATH_CANCELLED);
    CHECK(fe.created.empty());  // never sent
    CHECK_FALSE(s.cancel(a));   // gone

    const uint32_t b = s.request(query(), 7, cb_noop, nullptr, 20, nullptr);
    s.step(fe.engine(), 30);
    CHECK(s.cancel(b));
    done = s.step(fe.engine(), 40);
    REQUIRE(done.size() == 1u);
    CHECK(done[0].result.status == EMF_PATH_CANCELLED);
    CHECK(fe.destroyed == fe.created);

    const uint32_t c = s.request(query(), 9, cb_noop, nullptr, 50, nullptr);
    s.step(fe.engine(), 60);
    fe.gone.push_back(9);
    done = s.step(fe.engine(), 70);
    REQUIRE(done.size() == 1u);
    CHECK(done[0].id == c);
    CHECK(done[0].result.status == EMF_PATH_CANCELLED);

    const uint32_t d = s.request(query(), 7, cb_noop, nullptr, 100, nullptr);
    s.step(fe.engine(), 110);
    CHECK(s.step(fe.engine(), 100 + kPathTimeoutMs - 1).empty());
    done = s.step(fe.engine(), 100 + kPathTimeoutMs + 20);
    REQUIRE(done.size() == 1u);
    CHECK(done[0].id == d);
    CHECK(done[0].result.status == EMF_PATH_OTHER);
    CHECK(fe.destroyed.size() == fe.created.size());
}

TEST_CASE("path service: a new server world ends every query as cancelled, without calling the old world's game") {
    PathService s;
    FakeEngine fe;
    s.request(query(), 7, cb_noop, nullptr, 0, nullptr);
    s.request(query(), 7, cb_noop, nullptr, 0, nullptr);
    s.step(fe.engine(), 10);
    const auto done = s.world_reset();
    CHECK(done.size() == 2u);
    for (const auto& d : done) CHECK(d.result.status == EMF_PATH_CANCELLED);
    CHECK(fe.destroyed.empty());
    CHECK(s.open() == 0u);
}

TEST_CASE("path service: at most 512 points are kept; the count says how many were kept") {
    PathService s;
    FakeEngine fe;
    REQUIRE(s.request(query(190, 0), 7, cb_noop, nullptr, 0, nullptr));
    s.step(fe.engine(), 10);
    std::vector<EmfPathPoint> pts;
    for (int i = 1; i <= 700; ++i) pts.push_back({float(i) * 190.0f / 700.0f, 50, 0, 0});
    fe.finish(fe.created[0], 1, pts);
    const auto done = s.step(fe.engine(), 20);
    REQUIRE(done.size() == 1u);
    CHECK(done[0].result.count == kMaxPathPoints);
    CHECK(done[0].points.size() == kMaxPathPoints);
    CHECK(done[0].result.reaches == 1);  // judged on the game's last point, not the last kept one
}
