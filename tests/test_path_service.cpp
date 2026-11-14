// path_find (API v37): the queries' life in EMF's path service, over a fake engine (the game's create / poll / destroy).
#include <doctest.h>

#include <cmath>
#include <cstring>
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

TEST_CASE("path query bytes: EMF's fields over the game's defaults, at the offsets the game reads") {
    uint8_t b[kPathQueryBytes];
    for (auto& x : b) x = 0xAB;  // stands in for the game's defaults: what EMF does not set stays
    EmfPathQuery q = query(10, 4);
    q.from[1] = 51.5f;
    q.moves = EMF_PATH_JUMP_UP | EMF_PATH_JUMP_DOWN;  // walk is added
    q.max_jump_up = 4;
    q.max_jump_down = 6;
    q.width = 1.2f;   // 3 half metres (rounded up)
    q.height = 2.0f;  // 4
    q.closest = 1;
    q.max_nodes = 0;  // EMF's default
    fill_path_query(b, q, 77);
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto f32 = [&](size_t o) { float v; std::memcpy(&v, b + o, 4); return v; };
    auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, b + o, 2); return v; };
    CHECK(u32(0x00) == 77u);
    CHECK(f32(0x10) == 0.0f);
    CHECK(f32(0x14) == 51.5f);
    CHECK(f32(0x1c) == 10.0f);
    CHECK(f32(0x24) == 4.0f);
    CHECK(b[0x36] == 2);  // UseClosestTargetNodeAsFallback
    CHECK(u32(0x48) == kPathDefaultNodes);
    CHECK(u16(0x54) == (EMF_PATH_WALK | EMF_PATH_JUMP_UP | EMF_PATH_JUMP_DOWN));
    CHECK(b[0x56] == 4);
    CHECK(b[0x57] == 6);
    CHECK(b[0xca] == 1);         // the time-slice flag
    CHECK(u32(0xf4) == 4u);      // start search radius vertical (half metres), as the game's own callers
    CHECK(u32(0xfc) == 4u);      // search radius vertical
    CHECK(u32(0x100) == 3u);     // min horizontal space
    CHECK(u32(0x104) == 4u);     // min vertical space
    CHECK(b[0x28] == 0xAB);      // priority: the game's default kept
    CHECK(b[0x2a] == 0xAB);      // unknown bytes kept
    CHECK(f32(0x6c) != 0.0f);    // the costs kept (0xABABABAB here)
    q.closest = 0;
    q.max_nodes = 9000;
    q.width = 0;
    fill_path_query(b, q, 77);
    CHECK(b[0x36] == 0);
    CHECK(u32(0x48) == 9000u);
    CHECK(u32(0x100) == 0u);
}

TEST_CASE("path answer text: what the console's path command prints") {
    EmfPathResult r{};
    r.size = sizeof r;
    r.status = EMF_PATH_FOUND;
    r.reaches = 1;
    r.count = 3;
    r.length = 52.24f;
    const EmfPathPoint pts[3] = {{1, 2, 3, 0}, {4, 5, 6, 2}, {10, 50, 4, 5}};
    CHECK(path_answer_text(7, r, pts) ==
          "path 7: found, reaches the target, 3 points, 52.2 m, ends at (10.0, 50.0, 4.0); 1 jump, 1 door");
    r.reaches = 0;
    CHECK(path_answer_text(7, r, pts).find("found, stops short of the target") != std::string::npos);
    r = EmfPathResult{};
    r.status = EMF_PATH_NO_START;
    CHECK(path_answer_text(8, r, nullptr) == "path 8: no path grid at the start (not loaded, or inside something)");
    r.status = EMF_PATH_CANCELLED;
    CHECK(path_answer_text(9, r, nullptr) == "path 9: cancelled");
}

TEST_CASE("path service (review M2): a cancel while the step talks to the game still ends the query cancelled") {
    PathService s;
    FakeEngine fe;
    const uint32_t id = s.request(query(), 7, cb_noop, nullptr, 0, nullptr);
    s.step(fe.engine(), 10);
    fe.finish(fe.created[0], 1, {{10, 50, 0, 0}});
    PathEngine e = fe.engine();
    const auto poll = e.poll;
    e.poll = [&](uint64_t gid) {
        s.cancel(id);  // another thread, in the middle of the step
        return poll(gid);
    };
    const auto done = s.step(e, 20);
    REQUIRE(done.size() == 1u);
    CHECK(done[0].result.status == EMF_PATH_CANCELLED);
    CHECK(done[0].result.count == 0u);
}

// A modder's base (0.6.9, 2026-11-14): a closed gate with a floor piece under it was walked through (no doors, no jumps),
// while the raiders were stopped there. The game lets a walk or jump move enter a ClosedDoor node (type 19) that counts
// as ground unless a node rule forbids it; the door move ignores node rules. So EMF always forbids type 19.
TEST_CASE("path query bytes (0.6.11): closed doors are forbidden for every move but the door move (node rule bit 19)") {
    uint8_t b[kPathQueryBytes] = {};
    const uint32_t game_rule = 1u << 3;  // a forbidden rule of the game's defaults stays
    std::memcpy(b + 0x60, &game_rule, 4);
    EmfPathQuery q = query(10, 4);
    fill_path_query(b, q, 77);
    uint32_t forbidden = 0, required = 0;
    std::memcpy(&forbidden, b + 0x60, 4);
    std::memcpy(&required, b + 0x5c, 4);
    CHECK(forbidden == (game_rule | (1u << 19)));
    CHECK(required == 0u);
    q.moves = EMF_PATH_OPEN_DOOR;  // with the door move a closed door is still crossed (as a DOOR point)
    fill_path_query(b, q, 77);
    std::memcpy(&forbidden, b + 0x60, 4);
    CHECK(forbidden == (game_rule | (1u << 19)));
}
