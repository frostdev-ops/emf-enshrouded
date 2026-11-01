// Flame Blessings (mods/blessings): the commands, the per-player blessings, the screen line and the chat lines
// (blessings_logic.h, no game).
#include <doctest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "../mods/blessings/blessings_logic.h"

using namespace blessings;

TEST_CASE("blessings: parse set with #k and name") {
    Command c = parse_command({"god", "on", "#2"});
    CHECK(c.kind == Command::Set);
    CHECK(c.what == "god");
    CHECK(c.on);
    CHECK(c.target == "#2");

    c = parse_command({"Stamina", "OFF", "Ivar"});  // words ignore case; the name is kept as typed
    CHECK(c.kind == Command::Set);
    CHECK(c.what == "stamina");
    CHECK_FALSE(c.on);
    CHECK(c.target == "Ivar");

    c = parse_command({"nofall", "on", "Big", "Ivar"});  // a name with a space
    CHECK(c.kind == Command::Set);
    CHECK(c.target == "Big Ivar");

    c = parse_command({"refill", "on"});  // no target: the sender
    CHECK(c.kind == Command::Set);
    CHECK(c.what == "refill");
    CHECK(c.target.empty());

    c = parse_command({"heal", "#1"});
    CHECK(c.kind == Command::Heal);
    CHECK(c.target == "#1");
    c = parse_command({"off"});
    CHECK(c.kind == Command::AllOff);
    CHECK(c.target.empty());
    CHECK(parse_command({}).kind == Command::Help);
    CHECK(parse_command({"help"}).kind == Command::Help);
    CHECK(parse_command({"list"}).kind == Command::List);
}

TEST_CASE("blessings: parse bad word gives usage") {
    for (const std::vector<std::string>& words :
         std::vector<std::vector<std::string>>{{"fly", "on"}, {"god"}, {"god", "maybe"}, {"list", "extra"}, {"mana", "1"}}) {
        CAPTURE(words.front());
        const Command c = parse_command(words);
        CHECK(c.kind == Command::Bad);
        CHECK(c.error.find("blessings <god|refill|stamina|mana|nofall> <on|off> [#k|name]") != std::string::npos);
    }
    CHECK(parse_command({"fly", "on"}).error.find("fly") != std::string::npos);  // says which word it did not know
}

TEST_CASE("blessings: god wins over refill; off clears both") {
    Blessings b = apply({}, "god", true);
    CHECK(b.health == Health::God);
    b = apply(b, "refill", true);  // a second admin asks for refill: god stays
    CHECK(b.health == Health::God);
    b = apply(b, "refill", false);  // and taking refill away leaves god mode alone
    CHECK(b.health == Health::God);
    CHECK(apply(b, "god", false).health == Health::Off);

    Blessings r = apply({}, "refill", true);
    CHECK(r.health == Health::Refill);
    CHECK(apply(r, "god", true).health == Health::God);
    CHECK(apply(r, "refill", false).health == Health::Off);
    CHECK(apply(r, "god", false).health == Health::Refill);  // god was not on: nothing to turn off

    b = apply(apply(apply(b, "stamina", true), "mana", true), "nofall", true);
    CHECK(b.stamina);
    CHECK(b.mana);
    CHECK(b.nofall);
    CHECK(b.any());
    CHECK_FALSE(apply(b, "mana", false).mana);
    CHECK_FALSE(Blessings{}.any());
    CHECK(apply({}, "nofall", true).any());
}

TEST_CASE("blessings: hud text order and refill wording") {
    CHECK(hud_text({}) == "");
    Blessings all;
    all.health = Health::God;
    all.stamina = all.mana = all.nofall = true;
    CHECK(hud_text(all) == "Blessings: god mode, stamina, mana, no fall damage");
    Blessings r;
    r.health = Health::Refill;
    r.nofall = true;
    CHECK(hud_text(r) == "Blessings: endless health, no fall damage");
    Blessings m;
    m.mana = true;
    CHECK(hud_text(m) == "Blessings: mana");
}

TEST_CASE("blessings: announce text") {
    CHECK(announce_text("Ember", "god", true, "Ivar") == "[Blessings] Ember: god mode on for Ivar");
    CHECK(announce_text("Host", "refill", false, "Ivar") == "[Blessings] Host: endless health off for Ivar");
    CHECK(announce_text("Host", "stamina", true, "Host") == "[Blessings] Host: endless stamina on for Host");
    CHECK(announce_text("Ember", "mana", true, "Ivar") == "[Blessings] Ember: endless mana on for Ivar");
    CHECK(announce_text("Ember", "nofall", true, "Ivar") == "[Blessings] Ember: no fall damage on for Ivar");
    CHECK(announce_text("Ember", "heal", true, "Ivar") == "[Blessings] Ember: full heal for Ivar");
    CHECK(announce_text("Ember", "off", false, "Ivar") == "[Blessings] Ember: all blessings off for Ivar");
}

TEST_CASE("blessings: prune drops players who left") {
    Roster r;
    set(r, 0xAA, apply({}, "god", true));
    set(r, 0xBB, apply({}, "stamina", true));
    prune(r, {0xBB, 0xCC});  // 0xAA left; 0xCC is new and has none
    CHECK(r.by_account.count(0xAA) == 0);
    CHECK(of(r, 0xBB).stamina);
    CHECK_FALSE(of(r, 0xCC).any());
    CHECK(r.by_account.count(0xCC) == 0);  // asking adds nobody
    set(r, 0xBB, {});                       // all off: no entry kept
    CHECK(r.by_account.empty());
}

TEST_CASE("blessings: a reused entity id does not inherit") {
    // Player 2 (account 0xAA) has god mode and leaves; a new player gets entity id 2 with account 0xBB. The roster is
    // keyed by account and each step looks the account of entity 2 up again: the newcomer has nothing.
    Roster r;
    set(r, 0xAA, apply({}, "god", true));
    const uint64_t account_of_entity_2 = 0xBB;
    prune(r, {account_of_entity_2});
    CHECK_FALSE(of(r, account_of_entity_2).any());
    CHECK(r.by_account.empty());
    // and the same player coming back later starts without blessings too
    prune(r, {0xAA});
    CHECK_FALSE(of(r, 0xAA).any());
}

TEST_CASE("blessings: #k is the k-th player, lowest id first; a name ignores case") {
    const std::vector<PlayerRef> players = {{5, "Ivar"}, {2, "Ember"}, {9, "big ivar"}};
    std::string err;
    CHECK(find_player("#1", players, &err) == 2u);
    CHECK(find_player("#2", players, &err) == 5u);
    CHECK(find_player("#3", players, &err) == 9u);
    CHECK(find_player("ember", players, &err) == 2u);
    CHECK(find_player("BIG IVAR", players, &err) == 9u);
    CHECK(find_player("#4", players, &err) == 0u);
    CHECK(err.find("#4") != std::string::npos);
    CHECK(find_player("#0", players, &err) == 0u);
    CHECK(find_player("Nobody", players, &err) == 0u);
    CHECK(err.find("Nobody") != std::string::npos);
    const std::vector<PlayerRef> twins = {{3, "Ivar"}, {4, "ivar"}};
    CHECK(find_player("Ivar", twins, &err) == 0u);  // two players with that name: ask for #k
    CHECK(err.find("#k") != std::string::npos);
}
