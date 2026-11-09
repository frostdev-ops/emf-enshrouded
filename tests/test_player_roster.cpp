// The player roster behind on_player (API v28): who joined, left or died, from each server step's player list.
#include <doctest.h>

#include "core/player_roster.h"

using emf::core::PlayerRoster;
using emf::core::RosterEvent;
using emf::core::RosterSeen;

namespace {
RosterSeen seen(uint32_t entity, uint64_t account, bool ready = true, bool host = false, const char* name = "") {
    return RosterSeen{entity, account, ready, host, name};
}
std::string kinds(const std::vector<RosterEvent>& ev) {
    std::string s;
    for (const auto& e : ev) {
        if (!s.empty()) s += ' ';
        s += (e.kind == EMF_PLAYER_JOINED ? "+" : e.kind == EMF_PLAYER_LEFT ? "-" : "x") + std::to_string(e.entity);
    }
    return s;
}
}  // namespace

TEST_CASE("player roster: nothing before it is armed; then the ready players join, the host first") {
    PlayerRoster r;
    r.reset(1000);
    CHECK(r.update({seen(3, 0xC), seen(1, 0xA, true, true)}, 500).empty());
    const auto ev = r.update({seen(3, 0xC, true, false, "Cy"), seen(1, 0xA, true, true, "Al")}, 1000);
    CHECK(kinds(ev) == "+1 +3");
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].host);
    CHECK(ev[0].account == 0xA);
    CHECK(ev[0].name == "Al");
    CHECK_FALSE(ev[1].host);
    CHECK(r.update({seen(3, 0xC), seen(1, 0xA, true, true)}, 1100).empty());  // once per arrival
}

TEST_CASE("player roster: a player joins when ready, and only once their account is known") {
    PlayerRoster r;
    r.reset(0);
    CHECK(r.update({seen(2, 0xB, false)}, 10).empty());
    CHECK(r.update({seen(2, 0, true)}, 20).empty());
    CHECK(kinds(r.update({seen(2, 0xB, true)}, 30)) == "+2");
}

TEST_CASE("player roster: a player gone is left with the last name; back as another entity is left then joined") {
    PlayerRoster r;
    r.reset(0);
    r.update({seen(2, 0xB, true, false, "Bea"), seen(4, 0xD)}, 10);
    const auto gone = r.update({seen(4, 0xD)}, 20);
    REQUIRE(gone.size() == 1);
    CHECK(gone[0].kind == EMF_PLAYER_LEFT);
    CHECK(gone[0].entity == 2);
    CHECK(gone[0].account == 0xB);
    CHECK(gone[0].name == "Bea");
    CHECK(kinds(r.update({seen(5, 0xD)}, 30)) == "-4 +5");
}

TEST_CASE("player roster: a new world forgets everyone; they join again once it is armed") {
    PlayerRoster r;
    r.reset(0);
    r.update({seen(1, 0xA, true, true), seen(2, 0xB)}, 10);
    r.reset(5000);
    CHECK(r.update({seen(1, 0xA, true, true), seen(2, 0xB)}, 4000).empty());  // no "left" for the old world
    CHECK(kinds(r.update({seen(1, 0xA, true, true), seen(2, 0xB)}, 5000)) == "+1 +2");
}

TEST_CASE("player roster: a joined player's death names the last hitter within 10 s") {
    PlayerRoster r;
    r.reset(0);
    r.update({seen(2, 0xB)}, 10);
    r.hit(2, 900, 1000);
    auto d = r.died(2, 5000);
    REQUIRE(d);
    CHECK(d->kind == EMF_PLAYER_DIED);
    CHECK(d->entity == 2);
    CHECK(d->account == 0xB);
    CHECK(d->killer == 900);
    r.hit(2, 901, 6000);
    d = r.died(2, 16001);  // more than 10 s after that hit
    REQUIRE(d);
    CHECK(d->killer == 0);
    r.hit(2, 2, 20000);  // its own (a fall): no killer
    d = r.died(2, 20001);
    REQUIRE(d);
    CHECK(d->killer == 0);
    CHECK_FALSE(r.died(77, 20002));  // not a joined player
}
