// The mod check on join: the list text, its limits, the compare and the words (src/core/mods_check.h).
#include <doctest.h>

#include <string>
#include <vector>

#include "core/mods_check.h"

using emf::core::compare_mods;
using emf::core::decode_mods;
using emf::core::diff_text;
using emf::core::encode_mods;
using emf::core::ModEntry;
using emf::core::player_note;

TEST_CASE("mods check: a list goes as text and comes back the same") {
    const std::vector<ModEntry> mine = {{'c', "beasts_pack", "2.2.0"}, {'b', "emberbrand", "1.2.0"}, {'s', "beastmaster", "1.15.1"},
                                        {'l', "clock", "1.0.0"}, {'u', "lua", "1.20.0"}};
    const std::string text = encode_mods(mine);
    CHECK(text == "c beasts_pack 2.2.0\nb emberbrand 1.2.0\ns beastmaster 1.15.1\nl clock 1.0.0\nu lua 1.20.0\n");
    std::string why;
    const auto back = decode_mods(text, &why);
    REQUIRE(back);
    REQUIRE(back->size() == 5);
    CHECK((*back)[0].kind == 'c');
    CHECK((*back)[0].id == "beasts_pack");
    CHECK((*back)[4].version == "1.20.0");
}

TEST_CASE("mods check: bad lines are skipped, the first of a repeated id counts, too much is refused") {
    std::string why;
    const auto got = decode_mods("c ok 1.0\nx kind 1.0\nc Bad_Id 1.0\nc nover\nc spaced 1.0 extra\nb ok 2.0\n"
                                 "c long " + std::string(33, '9') + "\n\nb fine 0.1-beta\n", &why);
    REQUIRE(got);
    REQUIRE(got->size() == 2);
    CHECK((*got)[0].id == "ok");
    CHECK((*got)[0].version == "1.0");
    CHECK((*got)[1].id == "fine");
    std::string many;
    for (int i = 0; i < 513; ++i) many += "u m" + std::to_string(i) + " 1\n";
    CHECK_FALSE(decode_mods(many, &why));
    CHECK(why.find("512") != std::string::npos);
    CHECK_FALSE(decode_mods(std::string(16385, 'a'), &why));
    CHECK(why.find("16384") != std::string::npos);
}

TEST_CASE("mods check: what the player's game lacks or has in another version, content mods first") {
    const std::vector<ModEntry> server = {{'b', "emberbrand", "1.2.0"}, {'c', "beasts_pack", "2.2.0"}, {'s', "beastmaster", "1.15.1"},
                                          {'u', "lua", "1.20.0"}};
    const std::vector<ModEntry> player = {{'b', "emberbrand", "1.1.0"}, {'c', "my_pack", "1.0.0"}, {'u', "lua", "1.19.0"}};
    const auto d = compare_mods(server, player);
    REQUIRE(d.missing.size() == 2);
    CHECK(d.missing[0].first.id == "beasts_pack");  // content first
    CHECK(d.missing[0].second == "");
    CHECK(d.missing[1].first.id == "emberbrand");
    CHECK(d.missing[1].second == "1.1.0");
    REQUIRE(d.extra.size() == 1);
    CHECK(d.extra[0].id == "my_pack");
    const std::string note = player_note(d);
    CHECK(note.find("beasts_pack (server 2.2.0, you: none)") != std::string::npos);
    CHECK(note.find("emberbrand (server 1.2.0, you: 1.1.0)") != std::string::npos);
    CHECK(note.find("[Invalid Item Stack]") != std::string::npos);
    CHECK(note.find("beastmaster") == std::string::npos);  // a server mod: players do not need it
    CHECK(note.find("lua") == std::string::npos);          // unknown side: not compared
    CHECK(diff_text(d).find("extra: c my_pack 1.0.0") != std::string::npos);
}

TEST_CASE("mods check: without a content mod missing, the note says features may not work; the same mods say nothing") {
    const auto d = compare_mods({{'b', "emberbrand", "1.2.0"}}, {});
    const std::string note = player_note(d);
    CHECK(note.find("emberbrand (server 1.2.0, you: none)") != std::string::npos);
    CHECK(note.find("[Invalid Item Stack]") == std::string::npos);
    CHECK(note.find("may not work") != std::string::npos);
    const auto same = compare_mods({{'c', "beasts_pack", "2.2.0"}}, {{'c', "beasts_pack", "2.2.0"}, {'l', "clock", "1"}});
    CHECK(player_note(same).empty());
    CHECK(diff_text(same) == "same mods");
    const auto only_extra = compare_mods({}, {{'c', "my_pack", "1.0.0"}});
    CHECK(player_note(only_extra).empty());  // harmless for the world: no note
}

TEST_CASE("mods check: a long list in the note is cut after 8 with how many more") {
    std::vector<ModEntry> server;
    for (int i = 0; i < 12; ++i) server.push_back({'c', "pack" + std::to_string(i), "1.0"});
    const std::string note = player_note(compare_mods(server, {}));
    CHECK(note.find("pack7") != std::string::npos);
    CHECK(note.find("pack8") == std::string::npos);
    CHECK(note.find("and 4 more") != std::string::npos);
}
