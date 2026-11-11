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
};
std::vector<Did> g_did;
void on_action(EmfWorld*, uint32_t player, const char* id, double x, double z, void*) { g_did.push_back({player, id, x, z}); }

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
    CHECK(a.on_done(nullptr, 7, done_msg("teleport", 1500, 4740), 1000));
    REQUIRE(g_did.size() == 1);
    CHECK(g_did[0].player == 7u);
    CHECK(g_did[0].id == "teleport");
    CHECK(g_did[0].x == 1500);
    CHECK(g_did[0].z == 4740);
}

TEST_CASE("map actions: an unknown action, a spot outside the world, junk or a flood never reach the mod") {
    g_did.clear();
    std::vector<std::string> logs;
    MapActions a([&](const std::string& l) { logs.push_back(l); });
    a.add("lua", "teleport", "Teleport here", 0, on_action, nullptr);
    CHECK_FALSE(a.on_done(nullptr, 7, done_msg("nope", 1, 1), 1000));
    CHECK_FALSE(a.on_done(nullptr, 7, R"({"i": "teleport", "x": "nan", "z": 1})", 1000));  // JSON has no NaN
    CHECK_FALSE(a.on_done(nullptr, 7, done_msg("teleport", 1, 20000), 1000));
    CHECK_FALSE(a.on_done(nullptr, 7, "{broken", 1000));
    CHECK(g_did.empty());
    CHECK_FALSE(logs.empty());
    for (int i = 0; i < 20; ++i) CHECK(a.on_done(nullptr, 8, done_msg("teleport", 1, 1), 2000));
    CHECK_FALSE(a.on_done(nullptr, 8, done_msg("teleport", 1, 1), 2000));  // 20 in 10 s
    CHECK(a.on_done(nullptr, 8, done_msg("teleport", 1, 1), 13000));      // later again
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
