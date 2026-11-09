// The mod check on join: the list text, its limits, the compare and the words (src/core/mods_check.h).
#include <doctest.h>

#include <string>
#include <vector>

#include "core/mods_check.h"
#include "core/mods_check_service.h"

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

TEST_CASE("mods check: this game's list is its content mods, then the running mods by their side") {
    using emf::core::ModRecord;
    using emf::core::ModStatus;
    auto rec = [](const char* id, const char* version, const char* side, ModStatus st) {
        ModRecord r;
        r.id = id;
        r.version = version;
        r.side = side;
        r.status = st;
        return r;
    };
    const std::vector<ModRecord> mods = {rec("beastmaster", "1.15.1", "server", ModStatus::Script),
                                         rec("emberbrand", "1.2.0", "both", ModStatus::Loaded),
                                         rec("clock", "1.0.0", "client", ModStatus::Loaded),
                                         rec("off", "1.0", "both", ModStatus::Disabled),
                                         rec("broken", "1.0", "both", ModStatus::Failed),
                                         rec("lua", "1.20.0", "", ModStatus::Loaded),
                                         rec("odd", "1 0", "both", ModStatus::Loaded)};  // a version with a space: left out
    const auto list = emf::core::this_games_mods(mods, {{"beasts_pack", "2.2.0"}});
    CHECK(encode_mods(list) == "c beasts_pack 2.2.0\ns beastmaster 1.15.1\nb emberbrand 1.2.0\nl clock 1.0.0\nu lua 1.20.0\n");
}

namespace {
using emf::core::ModsCheck;
// One end of the exchange: what it sent, noted and logged.
struct End {
    std::vector<ModEntry> mods;
    bool joined = true;
    uint32_t server_version = 1;
    uint32_t session = 1;
    std::vector<std::string> up;                                // to the server
    std::vector<std::pair<uint32_t, std::string>> down;        // to players
    std::vector<std::string> notes, logs;
    ModsCheck m;
    explicit End(std::vector<ModEntry> mine)
        : mods(std::move(mine)),
          m(ModsCheck::Deps{[this] { return mods; }, [this] { return joined; }, [this] { return server_version; },
                            [this] { return session; },
                            [this](const std::string& d) {
                                up.push_back(d);
                                return true;
                            },
                            [this](uint32_t p, const std::string& d) {
                                down.emplace_back(p, d);
                                return true;
                            },
                            [this](const std::string& n) { notes.push_back(n); },
                            [this](const std::string& l) { logs.push_back(l); }}) {}
    size_t logged(const std::string& part) const {
        size_t n = 0;
        for (const auto& l : logs) n += l.find(part) != std::string::npos;
        return n;
    }
};
}  // namespace

TEST_CASE("mods check: a joined game sends its list once per session when the server has emf/mods; never on the host") {
    End game({{'e', "emf", "0.6.3"}, {'b', "emberbrand", "1.1.0"}});
    game.server_version = 0;  // an older server, or not welcomed yet
    game.m.client_tick();
    CHECK(game.up.empty());
    game.server_version = 1;
    game.m.client_tick();
    game.m.client_tick();
    REQUIRE(game.up.size() == 1);
    CHECK(game.up[0] == "e emf 0.6.3\nb emberbrand 1.1.0\n");
    game.session = 2;  // another server (or the same again)
    game.m.client_tick();
    CHECK(game.up.size() == 2);
    End host({{'e', "emf", "0.6.3"}});
    host.joined = false;  // the host's own game: its server is this process
    host.m.client_tick();
    CHECK(host.up.empty());
}

TEST_CASE("mods check: the server keeps a player's list, logs the differences once and answers with its own") {
    End server({{'e', "emf", "0.6.3"}, {'c', "beasts_pack", "2.2.0"}, {'b', "emberbrand", "1.2.0"}});
    server.m.on_server_message(2, 0xBB, "e emf 0.6.3\nb emberbrand 1.1.0\n");
    REQUIRE(server.down.size() == 1);
    CHECK(server.down[0].first == 2);
    CHECK(server.down[0].second == "e emf 0.6.3\nc beasts_pack 2.2.0\nb emberbrand 1.2.0\n");
    CHECK(server.logged("0x00000000000000bb") == 1);
    CHECK(server.logged("missing: c beasts_pack 2.2.0 (none), b emberbrand 1.2.0 (1.1.0)") == 1);
    const auto seen = server.m.player(0xBB);
    REQUIRE(seen);
    CHECK(seen->player == 2);
    CHECK(seen->emf == "0.6.3");
    CHECK(seen->diff.find("beasts_pack") != std::string::npos);
    CHECK(server.m.server_has_content());
    server.m.on_server_message(5, 0xBB, "e emf 0.6.3\nc beasts_pack 2.2.0\nb emberbrand 1.2.0\n");  // back, updated
    REQUIRE(server.m.player(0xBB));
    CHECK(server.m.player(0xBB)->player == 5);
    CHECK(server.m.player(0xBB)->diff == "same mods");
    CHECK_FALSE(server.m.player(0xCC));
}

TEST_CASE("mods check: the joined game notes what it lacks once per session; the same mods note nothing") {
    End game({{'e', "emf", "0.6.3"}, {'b', "emberbrand", "1.1.0"}});
    const std::string server_list = "e emf 0.6.3\nc beasts_pack 2.2.0\nb emberbrand 1.2.0\n";
    game.m.on_player_message(server_list);
    REQUIRE(game.notes.size() == 1);
    CHECK(game.notes[0].find("beasts_pack (server 2.2.0, you: none)") != std::string::npos);
    CHECK(game.logged("beasts_pack") == 1);  // and the whole list in the log
    game.m.on_player_message(server_list);
    CHECK(game.notes.size() == 1);  // once per session
    game.session = 2;
    game.m.on_player_message(server_list);
    CHECK(game.notes.size() == 2);
    End same({{'e', "emf", "0.6.3"}, {'c', "beasts_pack", "2.2.0"}});
    same.m.on_player_message("e emf 0.6.4\nc beasts_pack 2.2.0\ns beastmaster 1.15.1\n");
    CHECK(same.notes.empty());
}

TEST_CASE("mods check: a list that is too big or broken is dropped and logged once") {
    End server({{'e', "emf", "0.6.3"}});
    for (int i = 0; i < 3; ++i) server.m.on_server_message(2, 0xBB, std::string(20000, 'x'));
    CHECK(server.down.empty());
    CHECK(server.logged("16384") == 1);
    End game({{'e', "emf", "0.6.3"}});
    game.m.on_player_message(std::string(20000, 'x'));
    CHECK(game.notes.empty());
    CHECK(game.logged("16384") == 1);
}
