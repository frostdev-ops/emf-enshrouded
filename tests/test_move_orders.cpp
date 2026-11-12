// Move orders (API v40): a creature sent to a point through the game's guard order on a waypoint entity (fake world).
// Live spike 2026-11-12: a guard order on an entity 25 m away walked a Vukah straight there (Guard state, ~5 m/s).
#include <doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/api.h"
#include "core/api_sizes.h"
#include "core/move_orders.h"
#include "emf/emf_api.h"
#include "emf/generated/components.hpp"

using namespace emf::core;

namespace {
namespace gen = emf::gen::ecs;

constexpr uint32_t kEnemy = 70, kPlayer = 1, kBeast = 72;

struct Fake {
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> comps;
    uint64_t now = 1000;
    uint32_t next_spawn = 5000;
    std::vector<std::array<double, 3>> spawned;  // waypoints, in order
    std::vector<uint32_t> destroyed;
    std::vector<std::pair<uint32_t, uint32_t>> guards;  // (guarding, guarded)
    std::map<uint32_t, bool> dead;
    bool spawn_ok = true;

    template <class T>
    T& add(uint32_t e) {
        auto& v = comps[{T::kHash, e}];
        v.assign(sizeof(T), 0);
        return *reinterpret_cast<T*>(v.data());
    }
    template <class T>
    T& get(uint32_t e) {
        return *reinterpret_cast<T*>(comps.at({T::kHash, e}).data());
    }
    void place(uint32_t e, double x, double y, double z) {
        auto& v = comps[{gen::CurrentTransform::kHash, e}];
        v.assign(56, 0);
        const int64_t raw[3] = {std::llround(x * 4294967296.0), std::llround(y * 4294967296.0),
                                std::llround(z * 4294967296.0)};
        std::memcpy(v.data(), raw, sizeof raw);
    }
    MoveOrders::Deps deps() {
        MoveOrders::Deps d;
        d.get = [this](EmfWorld*, uint32_t hash, uint32_t e, uint64_t* size) -> void* {
            auto it = comps.find({hash, e});
            if (it == comps.end()) return nullptr;
            if (size) *size = it->second.size();
            return it->second.data();
        };
        d.now_ms = [this] { return now; };
        d.spawn = [this](EmfWorld*, double x, double y, double z) -> uint32_t {
            if (!spawn_ok) return 0;
            spawned.push_back({x, y, z});
            place(next_spawn, x, y, z);
            return next_spawn++;
        };
        d.destroy = [this](EmfWorld*, uint32_t e) {
            destroyed.push_back(e);
            comps.erase({gen::CurrentTransform::kHash, e});
        };
        d.guard = [this](uint32_t a, uint32_t b) { guards.push_back({a, b}); };
        d.dead = [this](EmfWorld*, uint32_t e) { return dead.count(e) && dead[e]; };
        return d;
    }
};

EmfWorld* W() { return reinterpret_cast<EmfWorld*>(0x1000); }

Fake world() {
    Fake f;
    f.add<gen::DynamicEnemy01BehaviorState>(kEnemy);
    f.add<gen::DynamicAggro>(kEnemy);
    f.add<gen::SelectedTargets>(kEnemy);
    f.add<gen::Faction>(kEnemy).faction = static_cast<emf::gen::FactionType>(3);
    f.place(kEnemy, 0, 10, 0);
    f.place(kPlayer, 50, 10, 50);
    f.add<gen::PlayerState>(kPlayer);
    return f;
}

struct Done {
    uint32_t id = 0, status = 99;
    int calls = 0;
};
MoveOrders::Callback cb(Done& d) {
    return [&d](EmfWorld*, uint32_t id, uint32_t status) {
        d.id = id;
        d.status = status;
        ++d.calls;
    };
}
std::vector<std::array<double, 3>> pts(std::initializer_list<std::array<double, 3>> l) { return l; }
}  // namespace

TEST_CASE("move orders: a waypoint is spawned at the first point and the enemy is told to guard it") {
    Fake f = world();
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    const uint32_t id = o.add(W(), kEnemy, pts({{20, 10, 0}}), 2.0, 60, 0, cb(d), &why);
    REQUIRE(id != 0u);
    REQUIRE(f.spawned.size() == 1u);
    CHECK(f.spawned[0][0] == doctest::Approx(20));
    REQUIRE(f.guards.size() == 1u);
    CHECK(f.guards[0] == std::make_pair(kEnemy, 5000u));
    CHECK(d.calls == 0);
}

TEST_CASE("move orders: within arrive distance of the last point it is done: arrived; the waypoint goes") {
    Fake f = world();
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    const uint32_t id = o.add(W(), kEnemy, pts({{20, 10, 0}}), 2.0, 60, 0, cb(d), &why);
    auto& ai = f.get<gen::DynamicEnemy01BehaviorState>(kEnemy);
    ai.guardEntityId.id = 5000;  // the game took the guard order
    ai.state = emf::gen::enemy01::State::Guard;
    f.place(kEnemy, 10, 10, 0);
    f.now += 1000;
    o.step(W());
    CHECK(d.calls == 0);
    f.place(kEnemy, 18.5, 10, 0);
    f.now += 1000;
    o.step(W());
    CHECK(d.calls == 1);
    CHECK(d.id == id);
    CHECK(d.status == EMF_MOVE_ARRIVED);
    CHECK(f.destroyed == std::vector<uint32_t>{5000});
    CHECK(o.count() == 0u);
    CHECK(ai.guardEntityId.id == 0u);  // the waypoint's id may soon be another entity's
    CHECK(ai.state == emf::gen::enemy01::State::Idle);
    CHECK(ai.homeZonePosition.x == doctest::Approx(20));  // it lives there now
}

TEST_CASE("move orders: a new move on a moving creature ends the old one (cancelled) and takes over") {
    Fake f = world();
    MoveOrders o(f.deps());
    Done d, e;
    std::string why;
    const uint32_t a = o.add(W(), kEnemy, pts({{40, 10, 0}}), 2.0, 60, 0, cb(d), &why);
    const uint32_t b = o.add(W(), kEnemy, pts({{0, 10, 40}}), 2.0, 60, 0, cb(e), &why);
    REQUIRE(b != 0u);
    CHECK(d.id == a);
    CHECK(d.status == EMF_MOVE_CANCELLED);
    CHECK(o.count() == 1u);
    CHECK(o.moving(W(), kEnemy) == b);
    CHECK(f.guards.back() == std::make_pair(kEnemy, 5001u));
}

TEST_CASE("move orders: several points: a new waypoint at each next point, and the guard order on it") {
    Fake f = world();
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    REQUIRE(o.add(W(), kEnemy, pts({{10, 10, 0}, {10, 10, 10}, {0, 10, 10}}), 2.0, 60, 0, cb(d), &why) != 0u);
    f.place(kEnemy, 10, 10, 1);
    o.step(W());
    REQUIRE(f.spawned.size() == 2u);
    CHECK(f.spawned[1][2] == doctest::Approx(10));
    CHECK(f.guards.back() == std::make_pair(kEnemy, 5001u));
    CHECK(f.destroyed == std::vector<uint32_t>{5000});
    f.place(kEnemy, 10, 10, 10);
    o.step(W());
    f.place(kEnemy, 1, 10, 10);
    o.step(W());
    CHECK(d.status == EMF_MOVE_ARRIVED);
    CHECK(f.spawned.size() == 3u);
}

TEST_CASE("move orders: the guard order is sent again every 3 s (the game ends a guard on Stuck)") {
    Fake f = world();
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    REQUIRE(o.add(W(), kEnemy, pts({{40, 10, 0}}), 2.0, 60, 0, cb(d), &why) != 0u);
    f.now += 1000;
    f.place(kEnemy, 5, 10, 0);
    o.step(W());
    CHECK(f.guards.size() == 1u);
    f.now += 2500;
    f.place(kEnemy, 10, 10, 0);
    o.step(W());
    CHECK(f.guards.size() == 2u);
}

TEST_CASE("move orders: no progress for 15 s is stuck; past its time is timeout; the enemy dead or gone is gone") {
    for (int c = 0; c < 3; ++c) {
        CAPTURE(c);
        Fake f = world();
        MoveOrders o(f.deps());
        Done d;
        std::string why;
        REQUIRE(o.add(W(), kEnemy, pts({{40, 10, 0}}), 2.0, c == 1 ? 10 : 60, 0, cb(d), &why) != 0u);
        if (c == 0) {
            for (int i = 0; i < 16; ++i) {  // it stays where it is
                f.now += 1000;
                o.step(W());
            }
            CHECK(d.status == EMF_MOVE_STUCK);
        }
        if (c == 1) {
            for (int i = 0; i < 11; ++i) {  // it walks, slowly, but the 10 s run out
                f.now += 1000;
                f.place(kEnemy, 1.0 * (i + 1), 10, 0);
                o.step(W());
            }
            CHECK(d.status == EMF_MOVE_TIMEOUT);
        }
        if (c == 2) {
            f.dead[kEnemy] = true;
            o.step(W());
            CHECK(d.status == EMF_MOVE_GONE);
        }
        CHECK(d.calls == 1);
        CHECK(f.destroyed.size() == 1u);
    }
}

TEST_CASE("move orders: cancel calls back cancelled; a world reset ends all with cancelled (no world)") {
    Fake f = world();
    MoveOrders o(f.deps());
    Done d, e;
    std::string why;
    const uint32_t id = o.add(W(), kEnemy, pts({{40, 10, 0}}), 2.0, 60, 0, cb(d), &why);
    CHECK(o.cancel(id));
    CHECK(d.status == EMF_MOVE_CANCELLED);
    CHECK_FALSE(o.cancel(id));
    REQUIRE(o.add(W(), kEnemy, pts({{40, 10, 0}}), 2.0, 60, 0, cb(e), &why) != 0u);
    o.world_reset();
    CHECK(e.status == EMF_MOVE_CANCELLED);
    CHECK(o.count() == 0u);
}

TEST_CASE("move orders: peaceful clears its fights against players each step; a monster fight stays") {
    Fake f = world();
    f.add<gen::Faction>(71).faction = static_cast<emf::gen::FactionType>(10);  // a skeleton
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    REQUIRE(o.add(W(), kEnemy, pts({{40, 10, 0}}), 2.0, 60, EMF_MOVE_PEACEFUL, cb(d), &why) != 0u);
    auto& ag = f.get<gen::DynamicAggro>(kEnemy);
    ag.targets[0].entityId.id = kPlayer;
    ag.targets[0].state = gen::AggroTargetState::Alarmed;
    ag.targets[0].aggro = 20;
    ag.targets[1].entityId.id = 71;
    ag.targets[1].state = gen::AggroTargetState::Alarmed;
    ag.targets[1].aggro = 20;
    f.get<gen::SelectedTargets>(kEnemy).targets[0].targetEntityId.id = kPlayer;
    f.place(kEnemy, 5, 10, 0);
    o.step(W());
    CHECK(ag.targets[0].state == gen::AggroTargetState::Unknown);
    CHECK(ag.targets[0].aggro == 0.0f);
    CHECK(ag.targets[1].state == gen::AggroTargetState::Alarmed);
    CHECK(f.get<gen::SelectedTargets>(kEnemy).targets[0].targetEntityId.id == 0u);
}

TEST_CASE("move orders: a tamed beast needs EMF_MOVE_ALLIES, and guards its owner again after (arrived or not)") {
    Fake f = world();
    f.add<gen::DynamicEnemy01BehaviorState>(kBeast).guardEntityId.id = kPlayer;
    f.add<gen::DynamicAggro>(kBeast);
    f.add<gen::SelectedTargets>(kBeast);
    f.add<gen::Faction>(kBeast).faction = static_cast<emf::gen::FactionType>(11);
    f.place(kBeast, 0, 10, 0);
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    CHECK(o.add(W(), kBeast, pts({{10, 10, 0}}), 2.0, 60, 0, cb(d), &why) == 0u);
    CHECK(why.find("EMF_MOVE_ALLIES") != std::string::npos);
    const uint32_t id = o.add(W(), kBeast, pts({{10, 10, 0}}), 2.0, 60, EMF_MOVE_ALLIES, cb(d), &why);
    REQUIRE(id != 0u);
    f.place(kBeast, 9.5, 10, 0);
    o.step(W());
    CHECK(d.status == EMF_MOVE_ARRIVED);
    CHECK(f.guards.back() == std::make_pair(kBeast, kPlayer));  // back to its owner
}

TEST_CASE("move orders: refused: no enemy AI, a player, a boss, no points, too many, bad numbers, no waypoint") {
    Fake f = world();
    f.add<gen::Boss>(73);
    f.add<gen::DynamicEnemy01BehaviorState>(73);
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    CHECK(o.add(W(), 999, pts({{1, 1, 1}}), 2.0, 60, 0, cb(d), &why) == 0u);
    CHECK(why.find("enemy AI") != std::string::npos);
    CHECK(o.add(W(), kPlayer, pts({{1, 1, 1}}), 2.0, 60, 0, cb(d), &why) == 0u);
    CHECK(o.add(W(), 73, pts({{1, 1, 1}}), 2.0, 60, 0, cb(d), &why) == 0u);
    CHECK(why.find("boss") != std::string::npos);
    CHECK(o.add(W(), kEnemy, {}, 2.0, 60, 0, cb(d), &why) == 0u);
    std::vector<std::array<double, 3>> many(17, {1, 1, 1});
    CHECK(o.add(W(), kEnemy, many, 2.0, 60, 0, cb(d), &why) == 0u);
    CHECK(o.add(W(), kEnemy, pts({{NAN, 1, 1}}), 2.0, 60, 0, cb(d), &why) == 0u);
    CHECK(o.add(W(), kEnemy, pts({{1, 1, 1}}), 0.1, 60, 0, cb(d), &why) == 0u);
    CHECK(o.add(W(), kEnemy, pts({{1, 1, 1}}), 2.0, 99999, 0, cb(d), &why) == 0u);
    CHECK(o.add(W(), kEnemy, pts({{1, 1, 1}}), 2.0, 60, 0x80, cb(d), &why) == 0u);
    f.spawn_ok = false;
    CHECK(o.add(W(), kEnemy, pts({{1, 1, 1}}), 2.0, 60, 0, cb(d), &why) == 0u);
    CHECK(why.find("waypoint") != std::string::npos);
    CHECK(d.calls == 0);
}

TEST_CASE("api table (v40): version 40, the three move functions at the end") {
    const EmfApi* api = emf::core::api_table();
    CHECK(api->version == 40u);
    CHECK(api->size == kApiV40Size);
    CHECK(api->enemy_move != nullptr);
    CHECK(api->enemy_move_cancel != nullptr);
    CHECK(api->enemy_moving != nullptr);
    EmfMoveOrder tiny{};
    tiny.size = 8;  // an older or broken caller
    CHECK(api->enemy_move(nullptr, &tiny, nullptr, nullptr) == 0u);
    CHECK_FALSE(api->enemy_move_cancel(12345));
    CHECK(api->enemy_moving(nullptr, 70) == 0u);
}

TEST_CASE("move orders: a new enemy order on a moving creature ends its move; a move ends its enemy order") {
    Fake f = world();
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    const uint32_t id = o.add(W(), kEnemy, pts({{40, 10, 0}}), 2.0, 60, 0, cb(d), &why);
    REQUIRE(id != 0u);
    CHECK(o.cancel_enemy(W(), kEnemy));  // what enemy_order calls first
    CHECK(d.status == EMF_MOVE_CANCELLED);
    CHECK_FALSE(o.cancel_enemy(W(), kEnemy));
}

TEST_CASE("move orders: a cancel from outside the server world ends the move at the next step") {
    Fake f = world();
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    const uint32_t id = o.add(W(), kEnemy, pts({{40, 10, 0}}), 2.0, 60, 0, cb(d), &why);
    CHECK(o.cancel(id, false));
    CHECK(d.calls == 0);
    CHECK(f.destroyed.empty());  // nothing touches the world off its thread
    o.step(W());
    CHECK(d.status == EMF_MOVE_CANCELLED);
    CHECK(f.destroyed == std::vector<uint32_t>{5000});
    CHECK(o.count() == 0u);
}

// Live 2026-11-12: a Vukah guarding a waypoint 27 m away walked there and stopped 2.4 m short (its guard distance,
// currentGuardTargetDistance, was 3.0), then went Idle: with arrive 2 the move ended stuck.
TEST_CASE("move orders: a point counts as reached within the creature's guard distance + 1 m (it stops that far short)") {
    Fake f = world();
    MoveOrders o(f.deps());
    Done d;
    std::string why;
    REQUIRE(o.add(W(), kEnemy, pts({{20, 10, 0}}), 2.0, 60, 0, cb(d), &why) != 0u);
    auto& ai = f.get<gen::DynamicEnemy01BehaviorState>(kEnemy);
    ai.currentGuardTargetDistance = 3.0f;
    f.place(kEnemy, 15.5, 10, 0);  // 4.5 m: not yet
    o.step(W());
    CHECK(d.calls == 0);
    f.place(kEnemy, 16.6, 10.3, 0.8);  // 3.5 m
    o.step(W());
    CHECK(d.status == EMF_MOVE_ARRIVED);
}
