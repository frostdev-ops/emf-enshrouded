// World settings: the game side (the server step writes the struct and sets the GameServer instance's send-settings
// flag) and the service the commands, the C API and Lua use. Fake memory stands in for the game.
#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "core/world_settings.h"
#include "core/world_settings_glue.h"
#include "core/world_settings_service.h"
#include "core/world_settings_state.h"
#include "world_settings_test_util.h"

using namespace emf::core::ws;
using ws_test::bytes;
using ws_test::defaults;

namespace {
constexpr uint32_t kPtr = 0x1758, kFlag = 0x18E0;

struct Rig {
    State state;
    ws_test::GS live = defaults();
    std::vector<uint8_t> instance = std::vector<uint8_t>(0x2000, 0);
    std::vector<std::string> log;
    std::vector<std::string> saved;
    Glue glue;
    explicit Rig(uint32_t ptr = kPtr, uint32_t flag = kFlag)
        : glue(state, GlueDeps{ptr, flag, [this](const std::string& l) { log.push_back(l); },
                               [this](const std::string& t) {
                                   saved.push_back(t);
                                   return true;
                               }}) {
        point_at(bytes(live));
    }
    void point_at(void* p) { std::memcpy(instance.data() + kPtr, &p, sizeof p); }
    uint8_t flag() const { return instance[kFlag]; }
    State::Step step(uint32_t session, uint64_t now, const std::string* file = nullptr) {
        return glue.server_step(bytes(live), instance.data(), session, now, file);
    }
    bool logged(const char* needle) const {
        for (const auto& l : log)
            if (l.find(needle) != std::string::npos) return true;
        return false;
    }
};
}  // namespace

TEST_CASE("world settings glue: a queued change is written and the flag set when the pointer matches") {
    Rig r;
    r.step(1, 0);
    CHECK(r.flag() == 0);
    r.state.queue({find("loot"), 2, false, false}, 1);
    const State::Step st = r.step(1, 1000);
    CHECK(st.wrote);
    CHECK(r.live.resourceDropStackAmountFactor == doctest::Approx(2));
    CHECK(r.flag() == 1);
    CHECK(r.logged("loot"));
}

TEST_CASE("world settings glue: no flag when the instance's pointer is another struct (logged)") {
    Rig r;
    auto other = defaults();
    r.point_at(&other);
    r.step(1, 0);
    r.state.queue({find("loot"), 2, false, false}, 1);
    r.step(1, 1000);
    CHECK(r.live.resourceDropStackAmountFactor == doctest::Approx(2));  // the server has it
    CHECK(r.flag() == 0);
    CHECK(r.logged("reconnect"));
}

TEST_CASE("world settings glue: flag offset 0 (not found on this build): writes, no flag, the log says players may lag") {
    Rig r(kPtr, 0);
    CHECK_FALSE(r.glue.can_sync());
    r.step(1, 0);
    r.state.queue({find("xp_combat"), 2, false, false}, 1);
    r.step(1, 1000);
    CHECK(r.live.experienceCombatFactor == doctest::Approx(2));
    CHECK(r.flag() == 0);
    CHECK(r.logged("reconnect"));
}

TEST_CASE("world settings glue: the world's kept overrides apply; persist saves the file, reset persist rewrites it") {
    Rig r;
    const std::string file = "resourceDropStackAmountFactor=2\nnoSuchKey=1\n";
    r.step(1, 0, &file);  // the file came on the session's first step
    CHECK(r.live.resourceDropStackAmountFactor == doctest::Approx(2));
    CHECK(r.logged("noSuchKey"));
    Rig late;  // the file comes a few steps after the base was taken
    late.step(1, 0);
    late.step(1, 100, &file);
    late.step(1, 200);
    CHECK(late.live.resourceDropStackAmountFactor == doctest::Approx(2));
    r.state.queue({find("day_minutes"), 10, false, true}, 1);
    r.step(1, 2000);
    REQUIRE_FALSE(r.saved.empty());
    CHECK(r.saved.back().find("dayTimeDuration=10") != std::string::npos);
    CHECK(r.saved.back().find("resourceDropStackAmountFactor=2") != std::string::npos);
    r.state.queue({nullptr, 0, true, true}, 1);
    r.step(1, 4000);
    CHECK(r.saved.back().find('=') == std::string::npos);  // nothing kept
    CHECK(r.live.resourceDropStackAmountFactor == doctest::Approx(1));
}

TEST_CASE("world settings service: set, get and reset go through the queue; refusals say why") {
    State st;
    auto live = defaults();
    uint8_t* where = bytes(live);
    uint32_t session = 1;
    Service svc(st, ServiceDeps{[&] { return where; }, [&] { return session; }});
    st.on_server_step(bytes(live), 1, 0);  // the base
    std::string why;
    double v = 0;
    REQUIRE(svc.get("loot", &v, &why));
    CHECK(v == doctest::Approx(1));
    CHECK(svc.set("loot", 2, false, &why));
    CHECK(svc.set_text("weather", "disabled", false, &why));
    st.on_server_step(bytes(live), 1, 1000);
    REQUIRE(svc.get("resourceDropStackAmountFactor", &v, &why));
    CHECK(v == doctest::Approx(2));
    CHECK(static_cast<int>(live.weatherFrequency) == 0);
    const auto rows = svc.rows();
    CHECK(rows.size() == 38);
    size_t changed = 0;
    for (const auto& row : rows) changed += row.changed ? 1 : 0;
    CHECK(changed == 2);
    CHECK_FALSE(svc.set("loot", 10, false, &why));
    CHECK(why.find("0.25") != std::string::npos);
    CHECK_FALSE(svc.set("nope", 1, false, &why));
    CHECK(why.find("world.list") != std::string::npos);
    CHECK(svc.reset("all", false, &why));
    st.on_server_step(bytes(live), 1, 3000);
    REQUIRE(svc.get("loot", &v, &why));
    CHECK(v == doctest::Approx(1));
    where = nullptr;  // no server world here (a joined client, the main menu)
    CHECK_FALSE(svc.set("loot", 2, false, &why));
    CHECK(why.find("host") != std::string::npos);
    CHECK_FALSE(svc.get("loot", &v, &why));
}

TEST_CASE("world settings service: change callbacks get each changed key after the write") {
    State st;
    auto live = defaults();
    Service svc(st, ServiceDeps{[&] { return bytes(live); }, [] { return 1u; }});
    std::vector<std::string> got;
    svc.on_changed([&](const std::string& key, double value) { got.push_back(key + "=" + std::to_string(int(value))); });
    st.on_server_step(bytes(live), 1, 0);
    std::string why;
    svc.set("xp_combat", 2, false, &why);
    svc.notify(st.on_server_step(bytes(live), 1, 1000));
    REQUIRE(got.size() == 1);
    CHECK(got[0] == "experienceCombatFactor=2");
}

TEST_CASE("world settings glue: sync_ok is false while the instance's pointer is another struct, true again when it matches") {
    Rig r;
    CHECK(r.glue.sync_ok());
    r.state.queue({find("loot"), 2, false, false}, 1);
    ws_test::GS other = defaults();
    r.point_at(bytes(other));
    r.step(1, 0);
    CHECK_FALSE(r.glue.sync_ok());
    r.point_at(bytes(r.live));
    r.state.queue({find("loot"), 1.5, false, false}, 1);
    r.step(1, 2000);
    CHECK(r.glue.sync_ok());
    Rig off(kPtr, 0);  // the flag not found on this build
    CHECK_FALSE(off.glue.sync_ok());
}
