// World settings: the console and chat commands world.get, world.set, world.reset, world.list and world.export.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/commands.h"
#include "core/world_commands.h"
#include "core/world_settings.h"
#include "core/world_settings_service.h"
#include "core/world_settings_state.h"
#include "world_settings_test_util.h"

using namespace emf::core;
using ws_test::bytes;
using ws_test::defaults;

namespace {
EmfWorld* const kWorld = reinterpret_cast<EmfWorld*>(0x5000);

struct Rig {
    ws::State state;
    ws_test::GS live = defaults();
    bool hosted = true;
    ws::Service svc{state, ws::ServiceDeps{[this] { return hosted ? bytes(live) : nullptr; }, [] { return 1u; }}};
    CommandCenter center;
    std::vector<std::string> out;
    Rig() {
        REQUIRE(add_world_commands(center, svc));
        state.on_server_step(bytes(live), 1, 0);  // the session's base
    }
    std::string run(const std::string& line) {
        out.clear();
        center.enqueue(line, 0);
        center.run_pending(kWorld, [this](const std::string& l) { out.push_back(l); }, 0);
        std::string all;
        for (const auto& l : out)
            if (l.rfind("> ", 0) != 0) all += l + "\n";
        return all;
    }
    void step(uint64_t now) { state.on_server_step(bytes(live), 1, now); }
};
}  // namespace

TEST_CASE("world commands: world.set queues a change for every player; the next step writes it") {
    Rig r;
    const std::string reply = r.run("world.set loot 2");
    CHECK(reply.find("loot (resourceDropStackAmountFactor) = 2") != std::string::npos);
    CHECK(reply.find("every player") != std::string::npos);
    CHECK(reply.find("until the world is loaded again") != std::string::npos);
    r.step(1000);
    CHECK(r.live.resourceDropStackAmountFactor == doctest::Approx(2));
    CHECK(r.run("world.set weather disabled persist").find("kept for this world") != std::string::npos);
    r.step(2000);
    CHECK(static_cast<int>(r.live.weatherFrequency) == 0);
}

TEST_CASE("world commands: refusals name the range, the list, or that only the host decides") {
    Rig r;
    CHECK(r.run("world.set loot 10").find("0.25 to 2") != std::string::npos);
    CHECK(r.run("world.set nope 1").find("world.list") != std::string::npos);
    CHECK(r.run("world.set loot").find("usage") != std::string::npos);
    r.hosted = false;
    CHECK(r.run("world.set loot 2").find("host") != std::string::npos);
    CHECK(r.run("world.get loot").find("host") != std::string::npos);
}

TEST_CASE("world commands: world.get shows the value, this world's own and the range; without a key what changed") {
    Rig r;
    CHECK(r.run("world.get").find("this world's own") != std::string::npos);
    r.run("world.set xp_combat 2");
    r.step(1000);
    const std::string one = r.run("world.get xp_combat");
    CHECK(one.find("xp_combat (experienceCombatFactor) = 2") != std::string::npos);
    CHECK(one.find("this world: 1") != std::string::npos);
    CHECK(one.find("0.25 to 2") != std::string::npos);
    const std::string changed = r.run("world.get");
    CHECK(changed.find("xp_combat = 2") != std::string::npos);
    CHECK(changed.find("loot") == std::string::npos);
}

TEST_CASE("world commands: world.reset puts a setting or all back to this world's values") {
    Rig r;
    r.run("world.set loot 2");
    r.run("world.set day_minutes 10");
    r.step(1000);
    CHECK(r.run("world.reset loot").find("loot") != std::string::npos);
    r.step(2000);
    CHECK(r.live.resourceDropStackAmountFactor == doctest::Approx(1));
    CHECK(r.live.dayTimeDuration.value == 600'000'000'000);
    CHECK(r.run("world.reset all").find("every setting") != std::string::npos);
    r.step(4000);
    CHECK(r.live.dayTimeDuration.value == 1'800'000'000'000);
    CHECK(r.run("world.reset nope").find("world.list") != std::string::npos);
}

TEST_CASE("world commands: world.list shows every setting by group; world.export prints the server JSON block") {
    Rig r;
    const std::string list = r.run("world.list");
    CHECK(list.find("Time:") != std::string::npos);
    CHECK(list.find("day_minutes=30 min") != std::string::npos);
    CHECK(list.find("Rates:") != std::string::npos);
    CHECK(list.find("loot=1") != std::string::npos);
    CHECK(list.find("enemyDamageFactor=1") != std::string::npos);
    const std::string block = r.run("world.export");
    CHECK(block.find("\"gameSettingsPreset\": \"Custom\"") != std::string::npos);
    CHECK(block.find("\"dayTimeDuration\": 1800000000000") != std::string::npos);
}
