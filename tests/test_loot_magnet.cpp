// Loot Magnet (mods/loot_magnet): which corpses are looted into which player, and what is never touched.
#include <doctest.h>

#include <map>
#include <string>
#include <vector>

#include "../mods/loot_magnet/magnet_logic.h"

using namespace magnet;

namespace {
constexpr uint64_t kSec = 1000;

// A fresh enemy corpse at `pos` holding furs: Health, an inventory and an interaction offer (what the game's loot event
// needs: docs/engine_parts/re_villages.md, immediate_loot_action).
Body corpse(uint32_t id, Vec3 pos, std::vector<std::string> items = {"Material_T1_Animal_Fur"}) {
    Body b;
    b.id = id;
    b.exists = true;
    b.pos = pos;
    b.has_health = true;
    b.has_inventory = true;
    b.has_offer = true;
    b.dead = true;
    b.faction = 3;  // Vukah
    b.items = std::move(items);
    return b;
}
Rules rules() {
    Rules r;
    r.radius = 8;
    r.delay_ms = 1000;
    r.window_ms = 11000;  // counted from the death
    r.base_radius = 30;
    r.materials_only = true;
    r.include_ammo = true;
    return r;
}
const Vec3 kHere{1000, 20, 1000};
const std::vector<Vec3> kNoBases;
}  // namespace

TEST_CASE("loot magnet: a fresh corpse near the player is looted after the delay, within the window") {
    const Death d{500, kHere, 10 * kSec, 1};
    const Body b = corpse(500, kHere);
    const Vec3 player{kHere.x + 3, kHere.y, kHere.z};
    CHECK(judge(d, b, player, kNoBases, rules(), 10 * kSec + 500, false) == Why::TooSoon);
    CHECK(judge(d, b, player, kNoBases, rules(), 11 * kSec, false) == Why::Ok);
    CHECK(judge(d, b, player, kNoBases, rules(), 21 * kSec, false) == Why::Ok);
    CHECK(judge(d, b, player, kNoBases, rules(), 21 * kSec + 1, false) == Why::TooLate);  // an old corpse: exploring
    CHECK(judge(d, b, {kHere.x + 9, kHere.y, kHere.z}, kNoBases, rules(), 12 * kSec, false) == Why::TooFar);
}

TEST_CASE("loot magnet: never a corpse twice, a player, an ally, a tame or owned creature, a villager, or one that moved") {
    const Death d{500, kHere, 0, 1};
    const Vec3 p = kHere;
    const auto at = 2 * kSec;
    Body b = corpse(500, kHere);
    CHECK(judge(d, b, p, kNoBases, rules(), at, true) == Why::Looted);
    b = corpse(500, kHere);
    b.player = true;  // a dead player (their own things)
    CHECK(judge(d, b, p, kNoBases, rules(), at, false) == Why::Protected);
    for (int faction : {7, 8, 11, 12}) {  // Player, AutomatedPlayer, PlayerAlly (a companion), FarmAnimal
        b = corpse(500, kHere);
        b.faction = faction;
        CAPTURE(faction);
        CHECK(judge(d, b, p, kNoBases, rules(), at, false) == Why::Protected);
    }
    b = corpse(500, kHere);
    b.owner = 1;
    CHECK(judge(d, b, p, kNoBases, rules(), at, false) == Why::Protected);
    b = corpse(500, kHere);
    b.tame = true;
    CHECK(judge(d, b, p, kNoBases, rules(), at, false) == Why::Protected);
    b = corpse(500, kHere);
    b.villager = true;
    CHECK(judge(d, b, p, kNoBases, rules(), at, false) == Why::Protected);
    b = corpse(500, {kHere.x + 6, kHere.y, kHere.z});  // 6 m from where it died: not the same body (an id reused?)
    CHECK(judge(d, b, {kHere.x + 6, kHere.y, kHere.z}, kNoBases, rules(), at, false) == Why::Moved);
    b = corpse(500, kHere);
    b.exists = false;
    CHECK(judge(d, b, p, kNoBases, rules(), at, false) == Why::Gone);
}

TEST_CASE("loot magnet: only what the game's loot event can take: a dead creature with an inventory") {
    const Death d{500, kHere, 0, 1};
    Body b = corpse(500, kHere);
    b.has_inventory = false;
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2 * kSec, false) == Why::NoLoot);
    b = corpse(500, kHere);
    b.has_health = false;  // not a creature: a chest, a prop
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2 * kSec, false) == Why::NoLoot);
    b = corpse(500, kHere, {});  // nothing in it (yet): wait
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2 * kSec, false) == Why::Empty);
}

TEST_CASE("loot magnet: nothing inside a base (the radius rule), wherever the player stands") {
    const Death d{500, kHere, 0, 1};
    const std::vector<Vec3> bases = {{kHere.x + 25, kHere.y + 10, kHere.z}};
    CHECK(judge(d, corpse(500, kHere), kHere, bases, rules(), 2 * kSec, false) == Why::InBase);
    const std::vector<Vec3> far = {{kHere.x + 31, kHere.y, kHere.z}};
    CHECK(judge(d, corpse(500, kHere), kHere, far, rules(), 2 * kSec, false) == Why::Ok);
}

TEST_CASE("loot magnet: materials only takes a corpse whose every stack is a material (ammo by choice); else everything") {
    CHECK(is_material("Material_T1_Animal_Fur", false));
    CHECK(is_material("material_t1_animal_fur", false));
    CHECK(is_material("Food_T1_raw_bad_meat_LeanMeat", false));
    CHECK(is_material("_Food_raw_T3_fish_common_03", false));
    CHECK(is_material("Build_TerrainMaterial_T2_TallTree_Bark", false));
    CHECK_FALSE(is_material("Weapon_T1_1H_SwordEpic_01", true));
    CHECK_FALSE(is_material("Ammo_Arrow_T1", false));
    CHECK(is_material("Ammo_Arrow_T1", true));
    CHECK_FALSE(is_material("Material", false));
    const Death d{500, kHere, 0, 1};
    const Body mixed = corpse(500, kHere, {"Material_T1_Animal_Fur", "Weapon_T1_1H_SwordEpic_01"});
    CHECK(judge(d, mixed, kHere, kNoBases, rules(), 2 * kSec, false) == Why::Filtered);  // the player opens that one
    Rules all = rules();
    all.materials_only = false;
    CHECK(judge(d, mixed, kHere, kNoBases, all, 2 * kSec, false) == Why::Ok);
    Rules no_ammo = rules();
    no_ammo.include_ammo = false;
    CHECK(judge(d, corpse(500, kHere, {"Ammo_Arrow_T1"}), kHere, kNoBases, no_ammo, 2 * kSec, false) == Why::Filtered);
}

namespace {
struct World {
    std::map<uint32_t, Body> bodies;
    Lookup lookup() {
        return [this](uint32_t id) {
            auto it = bodies.find(id);
            if (it == bodies.end()) {
                Body gone;
                gone.id = id;
                return gone;
            }
            return it->second;
        };
    }
};
}  // namespace

TEST_CASE("loot magnet: each corpse goes to the nearest player whose magnet is on; a corpse is sent once") {
    Magnet m;
    World w;
    w.bodies[500] = corpse(500, kHere);
    w.bodies[501] = corpse(501, {kHere.x + 50, kHere.y, kHere.z});
    m.on_death(500, kHere, 0, 1);
    m.on_death(501, {kHere.x + 50, kHere.y, kHere.z}, 0, 1);
    m.on_death(502, kHere, 0, 1);  // never had a body we could read: dropped quietly
    const std::vector<Looter> players = {{1, {kHere.x + 5, kHere.y, kHere.z}, true},
                                         {2, {kHere.x + 2, kHere.y, kHere.z}, false},  // nearer, but off
                                         {3, {kHere.x + 52, kHere.y, kHere.z}, true}};
    auto sent = m.tick(2 * kSec, 1, players, w.lookup(), kNoBases, rules());
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].corpse == 500u);
    CHECK(sent[0].player == 1u);
    CHECK(sent[1].corpse == 501u);
    CHECK(sent[1].player == 3u);
    CHECK(m.tick(3 * kSec, 1, players, w.lookup(), kNoBases, rules()).empty());  // never twice
}

TEST_CASE("loot magnet: a corpse nobody with a magnet is near waits for its window; switched off, nothing is looted") {
    Magnet m;
    World w;
    w.bodies[500] = corpse(500, kHere);
    m.on_death(500, kHere, 0, 1);
    std::vector<Looter> players = {{1, {kHere.x + 20, kHere.y, kHere.z}, true}};
    CHECK(m.tick(2 * kSec, 1, players, w.lookup(), kNoBases, rules()).empty());
    players[0].on = false;
    players[0].pos = kHere;
    CHECK(m.tick(3 * kSec, 1, players, w.lookup(), kNoBases, rules()).empty());
    players[0].on = true;  // walks up and switches it on within the window
    CHECK(m.tick(4 * kSec, 1, players, w.lookup(), kNoBases, rules()).size() == 1);
    CHECK(m.watched() == 0);  // done: forgotten
}

TEST_CASE("loot magnet: deaths of an older session are forgotten (entity ids are reused after a reload)") {
    Magnet m;
    World w;
    w.bodies[500] = corpse(500, kHere);
    m.on_death(500, kHere, 0, 1);
    const std::vector<Looter> players = {{1, kHere, true}};
    CHECK(m.tick(2 * kSec, 2, players, w.lookup(), kNoBases, rules()).empty());
    CHECK(m.watched() == 0);
}

TEST_CASE("loot magnet: at most 256 deaths are watched; the oldest go first") {
    Magnet m;
    for (uint32_t i = 0; i < 300; ++i) m.on_death(1000 + i, kHere, i, 1);
    CHECK(m.watched() == 256);
}

TEST_CASE("loot magnet: no pickup from a corpse that still holds loot within 2 s: no room (said once per player and session)") {
    Magnet m;
    m.on_sent(500, 1, 10 * kSec, 1);
    m.on_sent(501, 2, 10 * kSec, 1);
    m.on_pickup(2, 501);  // player 2 got theirs
    CHECK(m.overdue(11 * kSec).empty());
    const auto late = m.overdue(12 * kSec + 1);
    REQUIRE(late.size() == 1);
    CHECK(late[0].corpse == 500u);
    CHECK(late[0].player == 1u);
    CHECK(m.overdue(13 * kSec).empty());
    CHECK(m.should_tell_full(1, 1));
    CHECK_FALSE(m.should_tell_full(1, 1));
    CHECK(m.should_tell_full(1, 2));  // a new session
}

TEST_CASE("loot magnet: each player's switch is kept by account in the world's file; the default for everyone else") {
    Toggles t;
    CHECK(t.on(0xAA, true));
    CHECK_FALSE(t.on(0xAA, false));
    t.set(0xAA, false);
    t.set(0xBB, true);
    CHECK_FALSE(t.on(0xAA, true));
    CHECK(t.on(0xBB, false));
    const std::string text = t.text();
    CHECK(text == "0x00000000000000aa off\n0x00000000000000bb on\n");
    Toggles u;
    u.parse(text + "junk line\n0xzz on\n");
    CHECK_FALSE(u.on(0xAA, true));
    CHECK(u.on(0xBB, false));
    CHECK(u.text() == text);
    u.clear();
    CHECK(u.on(0xAA, true));
}

TEST_CASE("loot magnet: an item the game cannot name yet makes a materials-only corpse wait, not be dropped") {
    const Death d{500, kHere, 0, 1};
    const Body b = corpse(500, kHere, {"Material_T1_Animal_Fur", ""});  // "" = not named yet
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2 * kSec, false) == Why::Empty);
    Rules all = rules();
    all.materials_only = false;
    CHECK(judge(d, b, kHere, kNoBases, all, 2 * kSec, false) == Why::Ok);
}

TEST_CASE("loot magnet: an id that dies again in the same session (reused by a new creature) is a new corpse") {
    Magnet m;
    World w;
    w.bodies[500] = corpse(500, kHere);
    const std::vector<Looter> players = {{1, kHere, true}};
    m.on_death(500, kHere, 0, 1);
    REQUIRE(m.tick(2 * kSec, 1, players, w.lookup(), kNoBases, rules()).size() == 1);
    m.on_death(500, kHere, 60 * kSec, 1);  // later another creature with that id dies
    CHECK(m.tick(62 * kSec, 1, players, w.lookup(), kNoBases, rules()).size() == 1);
}

// ---- the Vukah brawler's corpse, and a decision for every corpse ----
TEST_CASE("loot magnet: the brawler's corpse as the game leaves it is looted: an Inventory added at death, no interaction offer") {
    // Enemy_Midboss_Vukah_Bigfoot_BossHealthBar has RandomLoot but no Inventory and no InteractionOfferRuntime in its
    // template (export/templates.csv); at death the game adds the Inventory (inv 6938 showed 3 stacks on it). A test in
    // game looted that corpse with the bypass event, so the offer is not needed.
    const Death d{6938, kHere, 0, 1};
    Body b = corpse(6938, kHere, {"Material_TX_Trophy_Vukah_head", "Material_T1_Animal_Fur", "Material_T2_Token_WeaponUpgrade_Rune02"});
    b.has_offer = false;
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2 * kSec, false) == Why::Ok);
}

TEST_CASE("loot magnet: a body that is alive is no corpse (it waits); another template under the id is not the dead one") {
    Death d{500, kHere, 0, 1};
    Body b = corpse(500, kHere);
    b.dead = false;  // NetworkHealth not 0: a new creature that got the id, or not dead yet
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2 * kSec, false) == Why::Alive);
    d.tpl = 0x1000;
    b = corpse(500, kHere);
    b.tpl = 0x2000;  // the id now belongs to an entity of another template
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2 * kSec, false) == Why::Reused);
    b.tpl = 0x1000;
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2 * kSec, false) == Why::Ok);
}

TEST_CASE("loot magnet: every corpse considered gets one decision: looted, skipped with its reason, or the window ended while it waited") {
    Magnet m;
    World w;
    w.bodies[500] = corpse(500, kHere);
    w.bodies[501] = corpse(501, {kHere.x + 100, kHere.y, kHere.z});
    w.bodies[502] = corpse(502, kHere);
    w.bodies[502].has_inventory = false;
    m.on_death(500, kHere, 0, 1);
    m.on_death(501, {kHere.x + 100, kHere.y, kHere.z}, 0, 1);
    m.on_death(502, kHere, 0, 1);
    const std::vector<Looter> players = {{1, {kHere.x + 3, kHere.y, kHere.z}, true}};
    std::vector<Decision> out;
    CHECK(m.tick(2 * kSec, 1, players, w.lookup(), kNoBases, rules(), &out).size() == 1);
    REQUIRE(out.size() == 1);  // 502 has no inventory yet: it waits (the game adds it 1-3 s after the death)
    CHECK(out[0].corpse == 500u);
    CHECK(out[0].why == Why::Ok);
    CHECK(out[0].player == 1u);
    CHECK(out[0].distance == doctest::Approx(3.0));
    out.clear();
    CHECK(m.tick(5 * kSec, 1, players, w.lookup(), kNoBases, rules(), &out).empty());
    CHECK(out.empty());  // 501 and 502 still wait: nothing to say yet
    m.tick(12 * kSec, 1, players, w.lookup(), kNoBases, rules(), &out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].corpse == 501u);
    CHECK(out[0].why == Why::TooLate);
    CHECK(out[0].waited == Why::TooFar);  // nobody with a magnet came near
    CHECK(out[0].distance == doctest::Approx(97.0));  // the nearest player with a magnet
    CHECK(out[1].corpse == 502u);
    CHECK(out[1].why == Why::TooLate);
    CHECK(out[1].waited == Why::NoLoot);  // no loot appeared within the window
    CHECK(std::string(why_text(Why::NoLoot)).find("inventory") != std::string::npos);
    CHECK(std::string(why_text(Why::TooFar)).find("magnet") != std::string::npos);
}

TEST_CASE("loot magnet: a loot event nothing came from is sent again (up to 5 tries, 2 s apart): nothing moved, nothing doubles") {
    Magnet m;
    m.on_sent(500, 1, 0, 1);
    auto late = m.overdue(2001);
    REQUIRE(late.size() == 1);
    CHECK(late[0].tries == 1);
    CHECK(m.should_retry(late[0]));
    m.on_sent(500, 1, 2001, 1, late[0].tries + 1);
    late = m.overdue(4002);
    REQUIRE(late.size() == 1);
    CHECK(late[0].tries == 2);
    late[0].tries = Magnet::kMaxTries;
    CHECK_FALSE(m.should_retry(late[0]));  // the last try: then "no room" (or the game refused)
}

// ---- the game adds the corpse's Inventory 1-3 s after the death ----
TEST_CASE("loot magnet: a corpse whose Inventory comes 2 s after the death is looted once it is there (it waits, not skipped)") {
    Magnet m;
    World w;
    Body b = corpse(6928, kHere);
    b.has_inventory = false;  // at the death and 1 s later: "entity 6928 has no inventory"
    b.items.clear();
    w.bodies[6928] = b;
    m.on_death(6928, kHere, 0, 1);
    const std::vector<Looter> players = {{1, {kHere.x + 1, kHere.y, kHere.z}, true}};
    std::vector<Decision> out;
    for (uint64_t t = 1000; t <= 2000; t += 500) CHECK(m.tick(t, 1, players, w.lookup(), kNoBases, rules(), &out).empty());
    CHECK(out.empty());   // nothing decided while it has no loot yet
    CHECK(m.watched() == 1);
    w.bodies[6928] = corpse(6928, kHere, {"Material_TX_Trophy_Vukah_head", "Material_T1_Animal_Fur"});  // at 3 s: 2 stacks
    const auto sent = m.tick(3000, 1, players, w.lookup(), kNoBases, rules(), &out);
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].corpse == 6928u);
    REQUIRE(out.size() == 1);
    CHECK(out[0].why == Why::Ok);
}

TEST_CASE("loot magnet: only lasting reasons skip a corpse at once (player, owned, tame, villager, ally, inside a base)") {
    const Death d{500, kHere, 0, 1};
    Body b = corpse(500, kHere);
    b.has_inventory = false;
    b.has_health = false;
    b.owner = 7;  // owned, and no loot yet: skipped for the owner, not kept waiting
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2000, false) == Why::Protected);
    b.owner = 0;
    const std::vector<Vec3> bases = {kHere};
    CHECK(judge(d, b, kHere, bases, rules(), 2000, false) == Why::InBase);
    CHECK(judge(d, b, kHere, kNoBases, rules(), 2000, false) == Why::NoLoot);  // a wait (Magnet::tick keeps it)
    CHECK(is_final(Why::Protected));
    CHECK(is_final(Why::InBase));
    CHECK_FALSE(is_final(Why::NoLoot));
    CHECK_FALSE(is_final(Why::Empty));
    CHECK_FALSE(is_final(Why::Alive));
    CHECK_FALSE(is_final(Why::TooSoon));
    CHECK_FALSE(is_final(Why::TooFar));
}

// ---- most creatures leave their loot in a loot container where they died (a fresh kill's loot) ----
namespace {
// What most creatures leave where they die (templates LootContainer_Enemy_*: Inventory, DefaultInventory, DestroyOnLoot,
// an interaction offer; no Health): the glowing loot spot. The creature itself fades and is removed.
Body loot_spot(uint32_t id, Vec3 pos, std::vector<std::string> items = {"Material_T1_Animal_Fur"}) {
    Body b;
    b.id = id;
    b.exists = true;
    b.pos = pos;
    b.has_inventory = true;
    b.has_offer = true;
    b.container = true;
    b.items = std::move(items);
    return b;
}
Body faded_creature(uint32_t id, Vec3 pos) {  // dead, no loot on it: the game puts the loot in a loot container
    Body b = corpse(id, pos, {});
    b.has_inventory = false;
    return b;
}
}  // namespace

TEST_CASE("loot magnet: a fresh kill whose loot the game leaves in a loot container where it died is looted from it") {
    Magnet m;
    World w;
    w.bodies[6938] = faded_creature(6938, kHere);
    m.on_death(6938, kHere, 0, 1);
    const std::vector<Looter> players = {{1, {kHere.x + 3, kHere.y, kHere.z}, true}};
    std::vector<Decision> out;
    std::vector<Body> spots;  // nothing there yet
    for (uint64_t t = 1000; t <= 2000; t += 500) CHECK(m.tick(t, 1, players, w.lookup(), kNoBases, rules(), &out, spots).empty());
    CHECK(out.empty());
    // 2.5 s: the creature has faded away, and its loot lies in a loot container half a metre from where it died
    w.bodies.erase(6938);
    spots.push_back(loot_spot(7100, {kHere.x + 0.5, kHere.y - 0.4, kHere.z + 0.3}, {"Material_T1_Animal_Fur", "Food_T1_raw_bad_meat_LeanMeat"}));
    const auto sent = m.tick(2500, 1, players, w.lookup(), kNoBases, rules(), &out, spots);
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].corpse == 7100u);  // the loot event goes to the container
    CHECK(sent[0].player == 1u);
    REQUIRE(out.size() == 1);
    CHECK(out[0].corpse == 6938u);
    CHECK(out[0].container == 7100u);
    CHECK(out[0].why == Why::Ok);
    CHECK(m.watched() == 0);
    CHECK(m.tick(3000, 1, players, w.lookup(), kNoBases, rules(), &out, spots).empty());  // never twice
}

TEST_CASE("loot magnet: a loot container already there when the creature died, or too far from it, is not its loot") {
    Magnet m;
    World w;
    Death d{6938, kHere, 0, 1};
    d.before = {7100};  // an earlier kill's loot (or a chest) lying there at the death
    m.on_death(d);
    const std::vector<Looter> players = {{1, kHere, true}};
    const std::vector<Body> spots = {loot_spot(7100, kHere), loot_spot(7101, {kHere.x + 6, kHere.y, kHere.z})};
    std::vector<Decision> out;
    CHECK(m.tick(3000, 1, players, w.lookup(), kNoBases, rules(), &out, spots).empty());
    CHECK(out.empty());  // it waits for its own
    m.tick(12 * kSec, 1, players, w.lookup(), kNoBases, rules(), &out, spots);  // the window ends, also for a body gone
    REQUIRE(out.size() == 1);
    CHECK(out[0].why == Why::TooLate);
    CHECK(out[0].waited == Why::Gone);
}

TEST_CASE("loot magnet: a loot container follows the corpse's rules: materials only, a base, an ally's or a player's death") {
    const std::vector<Looter> players = {{1, kHere, true}};
    auto run = [&](Death d, Body spot, const std::vector<Vec3>& bases, const Rules& r) {
        Magnet m;
        World w;
        m.on_death(d);
        std::vector<Decision> out;
        auto sent = m.tick(3000, 1, players, w.lookup(), bases, r, &out, {spot});
        return std::make_pair(sent.size(), out.empty() ? Why::Ok : out[0].why);
    };
    const Death d{6938, kHere, 0, 1};
    const Body gear = loot_spot(7100, kHere, {"Material_T1_Animal_Fur", "Weapon_T1_1H_SwordEpic_01"});
    CHECK(run(d, gear, kNoBases, rules()) == std::make_pair(size_t(0), Why::Filtered));  // left for the player to open
    Rules all = rules();
    all.materials_only = false;
    CHECK(run(d, gear, kNoBases, all) == std::make_pair(size_t(1), Why::Ok));
    CHECK(run(d, loot_spot(7100, kHere), {{kHere.x + 10, kHere.y, kHere.z}}, rules()) == std::make_pair(size_t(0), Why::InBase));
    Death ally = d;
    ally.guarded = true;  // a player's, the companion's or a summon's death: whatever it leaves is not taken
    CHECK(run(ally, loot_spot(7100, kHere), kNoBases, rules()) == std::make_pair(size_t(0), Why::Protected));
    Body empty = loot_spot(7100, kHere, {});  // its loot is not in it yet: wait
    CHECK(run(d, empty, kNoBases, rules()) == std::make_pair(size_t(0), Why::Ok));  // no decision yet
}

TEST_CASE("loot magnet: two kills side by side: each loot container is looted once") {
    Magnet m;
    World w;
    m.on_death(500, kHere, 0, 1);
    m.on_death(501, {kHere.x + 2, kHere.y, kHere.z}, 0, 1);
    const std::vector<Looter> players = {{1, kHere, true}};
    const std::vector<Body> spots = {loot_spot(7100, kHere), loot_spot(7101, {kHere.x + 2, kHere.y, kHere.z})};
    const auto sent = m.tick(3000, 1, players, w.lookup(), kNoBases, rules(), nullptr, spots);
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].corpse != sent[1].corpse);
    CHECK(m.tick(3500, 1, players, w.lookup(), kNoBases, rules(), nullptr, spots).empty());
}

TEST_CASE("loot magnet: a corpse that holds its loot itself is still looted itself (the loot event takes it there)") {
    Magnet m;
    World w;
    w.bodies[6938] = corpse(6938, kHere);
    m.on_death(6938, kHere, 0, 1);
    const std::vector<Looter> players = {{1, kHere, true}};
    const auto sent = m.tick(2000, 1, players, w.lookup(), kNoBases, rules(), nullptr, {});
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].corpse == 6938u);
}

TEST_CASE("loot magnet: who is guarded at their death: a player, an ally, a farm animal, anything owned or tame, a villager") {
    Body b = corpse(500, kHere);
    CHECK_FALSE(guarded(b));
    b.player = true;
    CHECK(guarded(b));
    b = corpse(500, kHere);
    b.faction = 11;  // PlayerAlly: the companion, summons
    CHECK(guarded(b));
    b = corpse(500, kHere);
    b.owner = 1;
    CHECK(guarded(b));
    b = corpse(500, kHere);
    b.tame = true;
    CHECK(guarded(b));
    b = corpse(500, kHere);
    b.villager = true;
    CHECK(guarded(b));
}

// ---- the game's clock decides what appeared after the death: a loot spot may spawn in the death's own step ----
namespace {
constexpr int64_t kNs = 1000000000;  // keen::Time counts nanoseconds
Body spot_at(uint32_t id, Vec3 pos, int64_t spawned, std::vector<std::string> items = {"Material_T1_Animal_Fur"}) {
    Body b = loot_spot(id, pos, std::move(items));
    b.spawned = spawned;
    return b;
}
}  // namespace

TEST_CASE("loot magnet: a loot spot spawned in the death's own step is that kill's loot; an older one is not, and why is told") {
    Magnet m;
    World w;
    Death d{6938, kHere, 0, 1};
    d.time = 500 * kNs;    // EntityDiedEvent.timeStamp
    d.before = {7100};     // a snapshot taken after the death's step already holds the kill's own spot
    m.on_death(d);
    const std::vector<Looter> players = {{1, kHere, true}};
    const std::vector<Body> spots = {
        spot_at(7099, {kHere.x + 1, kHere.y, kHere.z}, 497 * kNs),    // an older kill's loot, 3 s before the death
        spot_at(7098, {kHere.x + 7, kHere.y, kHere.z}, 500 * kNs),    // 7 m away: out of reach
        spot_at(7101, {kHere.x + 0.5, kHere.y, kHere.z}, 0),          // its spawn time is not set yet
        spot_at(7100, {kHere.x + 0.2, kHere.y, kHere.z}, 500 * kNs),  // spawned in the same step as the death
    };
    std::vector<Decision> out;
    const auto sent = m.tick(1500, 1, players, w.lookup(), kNoBases, rules(), &out, spots);
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].corpse == 7100u);
    const auto skips = m.take_skips();
    REQUIRE(skips.size() == 3);
    CHECK(skips[0].corpse == 6938u);
    CHECK(skips[0].container == 7099u);
    CHECK(skips[0].why == Skip::Before);
    CHECK(skips[0].seconds == doctest::Approx(3.0));
    CHECK(skips[1].container == 7098u);
    CHECK(skips[1].why == Skip::OutOfReach);
    CHECK(skips[1].distance == doctest::Approx(7.0));
    CHECK(skips[2].container == 7101u);
    CHECK(skips[2].why == Skip::NoSpawnTime);
    CHECK(std::string(skip_text(Skip::Before)).find("before") != std::string::npos);
}

TEST_CASE("loot magnet: each skipped loot spot is told once per kill, not every round") {
    Magnet m;
    World w;
    Death d{6938, kHere, 0, 1};
    d.time = 500 * kNs;
    m.on_death(d);
    const std::vector<Looter> players = {{1, kHere, true}};
    const std::vector<Body> spots = {spot_at(7099, kHere, 400 * kNs)};
    m.tick(1500, 1, players, w.lookup(), kNoBases, rules(), nullptr, spots);
    CHECK(m.take_skips().size() == 1);
    m.tick(2000, 1, players, w.lookup(), kNoBases, rules(), nullptr, spots);
    CHECK(m.take_skips().empty());
}

TEST_CASE("loot magnet: a loot spot id the game gives the next kill's spot again is looted again (claims go by id and spawn time)") {
    Magnet m;
    World w;
    const std::vector<Looter> players = {{1, kHere, true}};
    Death d1{500, kHere, 0, 1};
    d1.time = 100 * kNs;
    m.on_death(d1);
    REQUIRE(m.tick(2000, 1, players, w.lookup(), kNoBases, rules(), nullptr, {spot_at(7100, kHere, 100 * kNs)}).size() == 1);
    // the looted spot is gone; 5 s later the next kill there gets a spot with the same id
    Death d2{501, kHere, 5000, 1};
    d2.time = 105 * kNs;
    m.on_death(d2);
    const auto sent = m.tick(7000, 1, players, w.lookup(), kNoBases, rules(), nullptr, {spot_at(7100, kHere, 105 * kNs)});
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].corpse == 7100u);
}
