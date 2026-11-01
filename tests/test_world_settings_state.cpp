// World settings: the change queue, the session's base values and the pacing of the game's resync.
#include <doctest.h>

#include <string>

#include "core/world_settings.h"
#include "core/world_settings_state.h"
#include "world_settings_test_util.h"

using namespace emf::core::ws;
using ws_test::bytes;
using ws_test::defaults;

namespace {
bool any_line(const State::Step& s, const char* needle) {
    for (const auto& l : s.lines)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST_CASE("world settings state: the first step keeps the base and applies persisted overrides") {
    State s;
    s.set_persisted({{"resourceDropStackAmountFactor", 2.0}}, 1);
    auto g = defaults();
    CHECK_FALSE(s.has_base());
    const State::Step st = s.on_server_step(bytes(g), 1, 0);
    CHECK(s.has_base());
    CHECK(g.resourceDropStackAmountFactor == doctest::Approx(2));
    CHECK(s.base_value(*find("loot")) == doctest::Approx(1));  // the world's own value
    CHECK(st.wrote);
    CHECK(st.sync);
    CHECK(any_line(st, "loot"));
}

TEST_CASE("world settings state: a change is one write and one sync; a second change within 1 s waits for its sync") {
    State s;
    auto g = defaults();
    State::Step st = s.on_server_step(bytes(g), 1, 0);  // the base
    CHECK_FALSE(st.wrote);
    CHECK_FALSE(st.sync);
    s.queue({find("loot"), 2, false, false}, 1);
    s.queue({find("day_minutes"), 10, false, false}, 1);
    st = s.on_server_step(bytes(g), 1, 1000);
    CHECK(st.wrote);
    CHECK(st.sync);
    CHECK(st.changed.size() == 2);
    CHECK(g.resourceDropStackAmountFactor == doctest::Approx(2));
    CHECK(g.dayTimeDuration.value == 600'000'000'000);
    s.queue({find("loot"), 1.5, false, false}, 1);
    st = s.on_server_step(bytes(g), 1, 1500);
    CHECK(st.wrote);
    CHECK_FALSE(st.sync);  // the last sync was 0.5 s ago
    st = s.on_server_step(bytes(g), 1, 2100);
    CHECK_FALSE(st.wrote);
    CHECK(st.sync);  // carried over
    st = s.on_server_step(bytes(g), 1, 3200);
    CHECK_FALSE(st.sync);
    s.queue({find("loot"), 1.5, false, false}, 1);  // the value it has: nothing to write
    st = s.on_server_step(bytes(g), 1, 5000);
    CHECK_FALSE(st.wrote);
    CHECK_FALSE(st.sync);
}

TEST_CASE("world settings state: reset writes the base back; reset all every field") {
    State s;
    auto g = defaults();
    s.on_server_step(bytes(g), 1, 0);
    s.queue({find("loot"), 2, false, false}, 1);
    s.queue({find("xp_combat"), 2, false, false}, 1);
    s.on_server_step(bytes(g), 1, 1000);
    s.queue({find("loot"), 0, true, false}, 1);
    s.on_server_step(bytes(g), 1, 3000);
    CHECK(g.resourceDropStackAmountFactor == doctest::Approx(1));
    CHECK(g.experienceCombatFactor == doctest::Approx(2));
    s.queue({nullptr, 0, true, false}, 1);
    s.on_server_step(bytes(g), 1, 5000);
    auto d = defaults();
    CHECK(equal(bytes(g), bytes(d)));
}

TEST_CASE("world settings state: a change queued in another session is dropped and logged") {
    State s;
    auto g = defaults();
    s.on_server_step(bytes(g), 1, 0);
    s.queue({find("loot"), 2, false, false}, 1);
    auto other = defaults();  // a reload: session 2, another struct
    const State::Step st = s.on_server_step(bytes(other), 2, 1000);
    CHECK(other.resourceDropStackAmountFactor == doctest::Approx(1));
    CHECK_FALSE(st.wrote);
    CHECK(any_line(st, "dropped"));
}

TEST_CASE("world settings state: persist adds and reset persist removes the key") {
    State s;
    auto g = defaults();
    s.on_server_step(bytes(g), 1, 0);
    s.queue({find("loot"), 2, false, true}, 1);
    s.on_server_step(bytes(g), 1, 1000);
    REQUIRE(s.persisted().count("resourceDropStackAmountFactor") == 1);
    CHECK(s.persisted().at("resourceDropStackAmountFactor") == doctest::Approx(2));
    s.queue({find("loot"), 0, true, true}, 1);
    s.on_server_step(bytes(g), 1, 3000);
    CHECK(s.persisted().empty());
    s.queue({find("loot"), 2, false, true}, 1);
    s.queue({find("xp_combat"), 2, false, true}, 1);
    s.on_server_step(bytes(g), 1, 5000);
    s.queue({nullptr, 0, true, true}, 1);
    s.on_server_step(bytes(g), 1, 7000);
    CHECK(s.persisted().empty());
}

TEST_CASE("world settings state: a change that would make the struct unsafe is refused (logged) and nothing is written") {
    State s;
    auto g = defaults();
    s.on_server_step(bytes(g), 1, 0);
    s.queue({find("loot"), 2, false, false}, 1);
    s.queue({find("fishingDifficulty"), 9, false, false}, 1);  // past parse(): an array index the game would overrun
    const State::Step st = s.on_server_step(bytes(g), 1, 1000);
    CHECK_FALSE(st.wrote);
    CHECK_FALSE(st.sync);
    CHECK(any_line(st, "refused"));
    CHECK(g.resourceDropStackAmountFactor == doctest::Approx(1));
    CHECK(static_cast<int>(g.fishingDifficulty) == 2);
}

TEST_CASE("world settings state: kept values belong to the session whose file was read; another world never gets them") {
    State s;
    s.set_persisted({{"resourceDropStackAmountFactor", 2.0}}, 1);
    auto g1 = defaults();
    s.on_server_step(bytes(g1), 1, 0);
    CHECK(g1.resourceDropStackAmountFactor == doctest::Approx(2));
    // session 2: another world, whose file never came (no on_load on this build)
    auto g2 = defaults();
    const State::Step st = s.on_server_step(bytes(g2), 2, 5000);
    CHECK(g2.resourceDropStackAmountFactor == doctest::Approx(1));
    CHECK_FALSE(any_line(st, "kept for this world"));
    CHECK(s.persisted().empty());
}

TEST_CASE("world settings state: on an unknown build a base that does not look like the game's settings turns writes off") {
    State s;
    s.require_plausible_base(true);
    auto odd = defaults();
    odd.playerHealthFactor = 50.0f;  // a shifted layout: this would be another field's bytes
    const State::Step first = s.on_server_step(bytes(odd), 1, 0);
    CHECK(any_line(first, "changes are off"));
    std::string why;
    CHECK_FALSE(s.writes_allowed(1, &why));
    CHECK(why.find("this game version") != std::string::npos);
    s.queue({find("loot"), 2, false, false}, 1);
    s.on_server_step(bytes(odd), 1, 1000);
    CHECK(odd.resourceDropStackAmountFactor == doctest::Approx(1));
    // a plausible base on an unknown build works as usual
    State ok;
    ok.require_plausible_base(true);
    auto g = defaults();
    ok.on_server_step(bytes(g), 1, 0);
    CHECK(ok.writes_allowed(1, nullptr));
    ok.queue({find("loot"), 2, false, false}, 1);
    ok.on_server_step(bytes(g), 1, 1000);
    CHECK(g.resourceDropStackAmountFactor == doctest::Approx(2));
}
