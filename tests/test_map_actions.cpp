// Markers step 2 (API v34): map actions: the server's list announced to players' games, a player's "done" checked and
// handed to the mod; the client end that learns the list and sends the held spot (game-free).
#include <doctest.h>

#include <limits>
#include <string>
#include <vector>

#include "core/map_actions.h"

using namespace emf::core;

namespace {

struct Did {
    uint32_t player;
    std::string id;
    double x, z;
    bool admin;
};
std::vector<Did> g_did;
void on_action(EmfWorld*, const EmfMapDone* d, void*) { g_did.push_back({d->player, d->action, d->x, d->z, d->admin != 0}); }

std::string done_msg(const char* id, double x, double z) { return MapActionsClient::done_message(id, x, z); }

}  // namespace

TEST_CASE("map actions: added, listed for players' games; bad ids and the 17th refused") {
    MapActions a(nullptr);
    CHECK(a.add("lua", "teleport", "Teleport here", 0, on_action, nullptr));
    CHECK_FALSE(a.add("lua", "has space", "x", 0, on_action, nullptr));
    CHECK_FALSE(a.add("lua", "nocb", "x", 0, nullptr, nullptr));
    for (int i = 0; i < 15; ++i) CHECK(a.add("m", ("a" + std::to_string(i)).c_str(), "A", 0, on_action, nullptr));
    CHECK_FALSE(a.add("m", "a17", "A", 0, on_action, nullptr));
    CHECK(a.add("lua", "teleport", "Go", 1500, on_action, nullptr));  // the same id again replaces it
    MapActionsClient c;
    REQUIRE(c.on_announce(a.announce_message()));
    CHECK(c.first_id() == "teleport");
    CHECK(c.hold_ms() == 1500u);
    CHECK(a.remove("m", "a0"));
    CHECK_FALSE(a.remove("other", "teleport"));  // only its owner removes it
}

TEST_CASE("map actions: a player's done reaches the mod with the player and the spot") {
    g_did.clear();
    MapActions a(nullptr);
    a.add("lua", "teleport", "Teleport here", 0, on_action, nullptr);
    CHECK(a.on_done(nullptr, 7, false, done_msg("teleport", 1500, 4740), 1000));
    REQUIRE(g_did.size() == 1);
    CHECK(g_did[0].player == 7u);
    CHECK(g_did[0].id == "teleport");
    CHECK(g_did[0].x == 1500);
    CHECK(g_did[0].z == 4740);
    CHECK_FALSE(g_did[0].admin);
    CHECK(a.on_done(nullptr, 1, true, done_msg("teleport", 1, 2), 1000));  // the host's own game, or a [chat] admin
    REQUIRE(g_did.size() == 2);
    CHECK(g_did[1].admin);
}

TEST_CASE("map actions: an unknown action, a spot outside the world, junk or a flood never reach the mod") {
    g_did.clear();
    std::vector<std::string> logs;
    MapActions a([&](const std::string& l) { logs.push_back(l); });
    a.add("lua", "teleport", "Teleport here", 0, on_action, nullptr);
    CHECK_FALSE(a.on_done(nullptr, 7, false, done_msg("nope", 1, 1), 1000));
    CHECK_FALSE(a.on_done(nullptr, 7, false, R"({"i": "teleport", "x": "nan", "z": 1})", 1000));  // JSON has no NaN
    CHECK_FALSE(a.on_done(nullptr, 7, false, done_msg("teleport", 1, 20000), 1000));
    CHECK_FALSE(a.on_done(nullptr, 7, false, "{broken", 1000));
    CHECK(g_did.empty());
    CHECK_FALSE(logs.empty());
    for (int i = 0; i < 20; ++i) CHECK(a.on_done(nullptr, 8, false, done_msg("teleport", 1, 1), 2000));
    CHECK_FALSE(a.on_done(nullptr, 8, false, done_msg("teleport", 1, 1), 2000));  // 20 in 10 s
    CHECK(a.on_done(nullptr, 8, false, done_msg("teleport", 1, 1), 13000));      // later again
}

TEST_CASE("map actions client: an empty list switches the gesture off; done sends the first action and the spot") {
    MapActionsClient c;
    CHECK_FALSE(c.announced());
    MapActions a(nullptr);
    a.add("lua", "teleport", "Teleport here", 0, on_action, nullptr);
    REQUIRE(c.on_announce(a.announce_message()));
    CHECK(c.announced());
    CHECK(c.hold_ms() == 1000u);
    const std::string m = c.done(10.5, 20.25);
    CHECK(m.find("teleport") != std::string::npos);
    MapActions none(nullptr);
    REQUIRE(c.on_announce(none.announce_message()));
    CHECK_FALSE(c.announced());
    CHECK(c.done(1, 2).empty());
    CHECK_FALSE(c.on_announce("{broken"));
    c.reset();
    CHECK_FALSE(c.announced());
}

TEST_CASE("map actions: an id with its mod's prefix (a Lua script's 'mod:id'); a null callback removes the owner's action") {
    MapActions a(nullptr);
    CHECK(a.add("lua", "teleports:teleport", "Teleport here", 0, on_action, nullptr));
    MapActionsClient c;
    REQUIRE(c.on_announce(a.announce_message()));
    CHECK(c.first_id() == "teleports:teleport");
    CHECK_FALSE(a.add("other", "teleports:teleport", nullptr, 0, nullptr, nullptr));  // not its owner's
    CHECK_FALSE(a.empty());
    CHECK(a.add("lua", "teleports:teleport", nullptr, 0, nullptr, nullptr));
    CHECK(a.empty());
    CHECK_FALSE(a.add("lua", "teleports:teleport", nullptr, 0, nullptr, nullptr));  // nothing to remove
}


TEST_CASE("map actions (review M1): a logger set later hears junk and a flood (once per player)") {
    g_did.clear();
    std::vector<std::string> logs;
    MapActions a(nullptr);
    a.set_log([&](const std::string& l) { logs.push_back(l); });
    a.add("lua", "teleport", "Teleport here", 0, on_action, nullptr);
    CHECK_FALSE(a.on_done(nullptr, 7, false, "{broken", 1000));
    REQUIRE(logs.size() == 1);
    for (int i = 0; i < 20; ++i) a.on_done(nullptr, 9, false, done_msg("teleport", 1, 1), 2000);
    CHECK_FALSE(a.on_done(nullptr, 9, false, done_msg("teleport", 1, 1), 2000));
    CHECK_FALSE(a.on_done(nullptr, 9, false, done_msg("teleport", 1, 1), 2000));
    REQUIRE(logs.size() == 2);
    CHECK(logs[1].find("player 9") != std::string::npos);
}
