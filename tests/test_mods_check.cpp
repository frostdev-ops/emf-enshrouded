// The mod check on join: the list text, its limits, the compare and the words (src/core/mods_check.h).
#include <doctest.h>

#include <string>
#include <vector>

#include "core/mods_check.h"
#include "core/mods_check_content.h"
#include "core/mods_check_service.h"
#include "core/mods_policy.h"
#include "core/mods_refuse.h"

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
    CHECK(note.rfind("Missing mods for this server (items from them can be lost): ", 0) == 0);  // the risk first
    CHECK(note.find("beasts_pack 2.2.0 (you: none)") != std::string::npos);
    CHECK(note.find("emberbrand 1.2.0 (you: 1.1.0)") != std::string::npos);
    CHECK(note.find("EMF.exe") != std::string::npos);
    CHECK(note.size() <= 200);  // 3 rows of an on-screen note
    CHECK(note.find("beastmaster") == std::string::npos);  // a server mod: players do not need it
    CHECK(note.find("lua") == std::string::npos);          // unknown side: not compared
    CHECK(diff_text(d).find("extra: c my_pack 1.0.0") != std::string::npos);
}

TEST_CASE("mods check: without a content mod missing, the note says features may not work; the same mods say nothing") {
    const auto d = compare_mods({{'b', "emberbrand", "1.2.0"}}, {});
    const std::string note = player_note(d);
    CHECK(note.find("emberbrand 1.2.0 (you: none)") != std::string::npos);
    CHECK(note.find("can be lost") == std::string::npos);
    CHECK(note.find("may not work") != std::string::npos);
    const auto same = compare_mods({{'c', "beasts_pack", "2.2.0"}}, {{'c', "beasts_pack", "2.2.0"}, {'l', "clock", "1"}});
    CHECK(player_note(same).empty());
    CHECK(diff_text(same) == "same mods");
    const auto only_extra = compare_mods({}, {{'c', "my_pack", "1.0.0"}});
    CHECK(player_note(only_extra).empty());  // harmless for the world: no note
}

TEST_CASE("mods check: a long list in the note is cut after 3, the rest in the console") {
    std::vector<ModEntry> server;
    for (int i = 0; i < 12; ++i) server.push_back({'c', "pack" + std::to_string(i), "1.0"});
    const std::string note = player_note(compare_mods(server, {}));
    CHECK(note.find("pack2") != std::string::npos);
    CHECK(note.find("pack3") == std::string::npos);
    CHECK(note.find("and 9 more in the console") != std::string::npos);
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
    CHECK(server.m.mine().size() == 3);
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
    CHECK(game.notes[0].find("beasts_pack 2.2.0 (you: none)") != std::string::npos);
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

TEST_CASE("mods check: the content mods are the EML mods of a start whose content was built or is current") {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring game = std::wstring(tmp) + L"emf_mods_check_content_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(game.c_str(), nullptr);
    CreateDirectoryW((game + L"\\mods").c_str(), nullptr);
    CreateDirectoryW((game + L"\\mods\\beasts").c_str(), nullptr);
    {
        FILE* f = _wfopen((game + L"\\mods\\beasts\\mod.json").c_str(), L"wb");
        REQUIRE(f);
        std::fputs(R"({"id": "beasts_pack", "name": "Beasts", "version": "2.2.0", "capabilities": ["patch"]})", f);
        std::fclose(f);
    }
    emf::loader::ContentStatus built;
    built.kind = "built";
    const auto got = emf::core::applied_content_mods(built, game);
    REQUIRE(got.size() == 1);
    CHECK(got[0].first == "beasts_pack");
    CHECK(got[0].second == "2.2.0");
    emf::loader::ContentStatus current;
    current.kind = "current";
    CHECK(emf::core::applied_content_mods(current, game).size() == 1);
    emf::loader::ContentStatus failed;
    failed.kind = "failed";
    CHECK(emf::core::applied_content_mods(failed, game).empty());  // the game runs without them
    CHECK(emf::core::applied_content_mods(std::nullopt, game).empty());
    DeleteFileW((game + L"\\mods\\beasts\\mod.json").c_str());
    RemoveDirectoryW((game + L"\\mods\\beasts").c_str());
    RemoveDirectoryW((game + L"\\mods").c_str());
    RemoveDirectoryW(game.c_str());
}

TEST_CASE("mods check: players.mods shows a player's list only while that very connection has emf/mods") {
    std::map<uint32_t, bool> peers{{2, true}};
    ModsCheck m(ModsCheck::Deps{[] { return std::vector<ModEntry>{{'e', "emf", "0.6.3"}}; }, [] { return false; },
                                [] { return 0u; }, [] { return 1u; }, [](const std::string&) { return true; },
                                [](uint32_t, const std::string&) { return true; }, [](const std::string&) {},
                                [](const std::string&) {}, [&](uint32_t p) { return peers[p]; }});
    m.on_server_message(2, 0xBB, "e emf 0.6.3\n");
    REQUIRE(m.current(0xBB, 2));
    peers[2] = false;  // back without EMF (or an older one)
    CHECK_FALSE(m.current(0xBB, 2));
    peers[5] = true;
    CHECK_FALSE(m.current(0xBB, 5));  // another entity than the list came from
}

TEST_CASE("mods policy: warn by default, refuse when asked, anything else warns and says so") {
    using emf::core::parse_mods_policy;
    std::vector<std::string> problems;
    CHECK_FALSE(parse_mods_policy("", "", "", &problems).refuse);
    CHECK_FALSE(parse_mods_policy("warn", "", "", &problems).refuse);
    CHECK(parse_mods_policy(" Refuse ", "", "", &problems).refuse);
    CHECK(problems.empty());
    CHECK_FALSE(parse_mods_policy("kick", "", "", &problems).refuse);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("\"kick\"") != std::string::npos);
    CHECK(problems[0].find("warn") != std::string::npos);
}

TEST_CASE("mods policy: required ids are split by commas and spaces, lower-cased, each once; bad ones named and dropped") {
    using emf::core::parse_mods_policy;
    std::vector<std::string> problems;
    const auto p = parse_mods_policy("refuse", "beastmaster, Emberbrand  beastmaster;x,,beastmaster", "", &problems);
    CHECK(p.required == std::vector<std::string>{"beastmaster", "emberbrand"});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("\"beastmaster;x\"") != std::string::npos);
    std::string many;
    for (int i = 0; i < 70; ++i) many += "m" + std::to_string(i) + ",";
    problems.clear();
    CHECK(parse_mods_policy("refuse", many, "", &problems).required.size() == emf::core::kMaxRequiredMods);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("64") != std::string::npos);
    CHECK(emf::core::valid_list_id("beasts_pack-2.x"));
    CHECK_FALSE(emf::core::valid_list_id("Beasts"));
}

TEST_CASE("mods policy: the info text is trimmed, control characters become spaces, cut at 200 bytes on a UTF-8 boundary") {
    using emf::core::parse_mods_policy;
    CHECK(parse_mods_policy("refuse", "", "  nexusmods.com/enshrouded	or discord  ", nullptr).info ==
          "nexusmods.com/enshrouded or discord");
    const std::string text = std::string(199, 'a') + "Ã©";  // the 2-byte character would end at byte 201
    CHECK(parse_mods_policy("", "", text, nullptr).info == std::string(199, 'a'));
    CHECK(parse_mods_policy("", "", std::string(300, 'b'), nullptr).info.size() == emf::core::kMaxModsInfoBytes);
}

namespace {
using emf::core::ModsRefuse;
using emf::core::Needed;
using emf::core::RosterSeen;
emf::core::RosterSeen seen(uint32_t entity, uint64_t account, bool ready = true, bool host = false) {
    emf::core::RosterSeen s;
    s.entity = entity;
    s.account = account;
    s.ready = ready;
    s.host = host;
    return s;
}
emf::core::ModsPolicy refusing(std::vector<std::string> required = {}, std::string info = "") {
    emf::core::ModsPolicy p;
    p.refuse = true;
    p.required = std::move(required);
    p.info = std::move(info);
    return p;
}
const std::vector<ModEntry> kFull = {{'e', "emf", "0.6.3"}, {'c', "beasts_pack", "2.2.0"}, {'b', "emberbrand", "1.2.0"}};
const std::vector<ModEntry> kLacking = {{'e', "emf", "0.6.3"}, {'b', "emberbrand", "1.2.0"}};
// The server's end with fake deps: what it told, kicked and logged.
struct RefuseRig {
    std::vector<ModEntry> server = {{'e', "emf", "0.6.3"}, {'c', "beasts_pack", "2.2.0"}, {'b', "emberbrand", "1.2.0"},
                                    {'s', "beastmaster", "1.15.1"}};
    std::map<uint64_t, std::vector<ModEntry>> lists;
    std::set<uint64_t> admins;
    std::vector<std::pair<uint64_t, std::string>> tells;
    size_t tell_tries = 0;
    bool tell_ok = true;
    std::vector<uint64_t> kicks;
    std::vector<std::string> logs;
    ModsRefuse r;
    explicit RefuseRig(emf::core::ModsPolicy p = refusing())
        : r(std::move(p),
            ModsRefuse::Deps{[this](uint64_t a, uint32_t) -> std::optional<std::vector<ModEntry>> {
                                 const auto it = lists.find(a);
                                 if (it == lists.end()) return std::nullopt;
                                 return it->second;
                             },
                             [this] { return server; }, [this](uint32_t, uint64_t a) { return admins.count(a) > 0; },
                             [this](uint32_t, uint64_t a, const std::string& t) {
                                 ++tell_tries;
                                 if (tell_ok) tells.emplace_back(a, t);
                                 return tell_ok;
                             },
                             [this](uint64_t a) {
                                 kicks.push_back(a);
                                 return true;
                             },
                             [this](const std::string& l) { logs.push_back(l); }}) {}
    size_t logged(const std::string& part) const {
        size_t n = 0;
        for (const auto& l : logs) n += l.find(part) != std::string::npos;
        return n;
    }
};
}  // namespace

TEST_CASE("refuse: needs are the server's content and side-both mods and the required ids") {
    using emf::core::refusal_needs;
    const std::vector<ModEntry> server = {{'e', "emf", "0.6.3"}, {'c', "beasts_pack", "2.2.0"}, {'b', "emberbrand", "1.2.0"},
                                          {'s', "beastmaster", "1.15.1"}, {'l', "clock", "1.0.0"}};
    std::vector<ModEntry> theirs = {{'e', "emf", "0.6.3"}, {'c', "beasts_pack", "2.2.0"}, {'b', "emberbrand", "1.1.0"}};
    const auto n = refusal_needs(server, {"beastmaster", "hudmod"}, &theirs);
    REQUIRE(n.size() == 3);
    CHECK(n[0].id == "emberbrand");
    CHECK(n[0].version == "1.2.0");
    CHECK(n[0].theirs == "1.1.0");
    CHECK(n[1].id == "beastmaster");  // a required mod the server runs: its version
    CHECK(n[1].version == "1.15.1");
    CHECK(n[1].theirs.empty());
    CHECK(n[2].id == "hudmod");  // one it does not run: any version
    CHECK(n[2].version.empty());
    theirs[2].version = "1.2.0";
    theirs.push_back({'s', "beastmaster", "1.15.1"});
    theirs.push_back({'l', "hudmod", "0.1.0"});
    CHECK(refusal_needs(server, {"beastmaster", "hudmod"}, &theirs).empty());
    CHECK(refusal_needs(server, {}, nullptr).size() == 2);  // no list: all of them ('s' and 'l' only when listed)
    CHECK(refusal_needs(server, {"emf"}, nullptr).size() == 2);  // EMF itself is not a mod
}

TEST_CASE("refuse: the words for a player without EMF and for one with other versions") {
    using emf::core::refuse_text;
    CHECK(refuse_text({{'c', "beasts_pack", "2.2.0", ""}, {'u', "hudmod", "", ""}}, true, "nexusmods.com") ==
          "This server needs EMF and these mods: beasts_pack 2.2.0, hudmod. Get them from nexusmods.com. You will be "
          "disconnected in 5 seconds.");
    CHECK(refuse_text({{'c', "beasts_pack", "2.2.0", ""}, {'b', "emberbrand", "1.2.0", "1.1.0"}}, false, "") ==
          "This server needs these mods: beasts_pack 2.2.0 (you: none), emberbrand 1.2.0 (you: 1.1.0). Install them with "
          "EMF.exe. You will be disconnected in 5 seconds.");
    CHECK(refuse_text({}, true, "") == "This server needs EMF. You will be disconnected in 5 seconds.");
    CHECK(refuse_text({}, true, "discord.gg/x.") ==
          "This server needs EMF. Get it from discord.gg/x. You will be disconnected in 5 seconds.");
    std::vector<Needed> many;
    for (int i = 0; i < 12; ++i) many.push_back({'c', "p" + std::to_string(i), "1.0", ""});
    const std::string t = refuse_text(many, false, "");
    CHECK(t.find("p7 1.0") != std::string::npos);
    CHECK(t.find("p8 1.0") == std::string::npos);
    CHECK(t.find("and 4 more") != std::string::npos);
}

TEST_CASE("refuse: a player with the mods stays; one without is told, kicked 5 s later, and logged if still there 10 s after") {
    RefuseRig g;
    g.lists[0xAA] = kFull;
    g.lists[0xBB] = kLacking;
    const std::vector<RosterSeen> both = {seen(2, 0xAA), seen(3, 0xBB)};
    g.r.tick(both, 1000);
    REQUIRE(g.tells.size() == 1);
    CHECK(g.tells[0].first == 0xBB);
    CHECK(g.tells[0].second.find("beasts_pack 2.2.0 (you: none)") != std::string::npos);
    CHECK(g.logged("refusing player 0x00000000000000bb (entity 3)") == 1);
    g.r.tick(both, 5999);
    CHECK(g.kicks.empty());
    g.r.tick(both, 6000);
    CHECK(g.kicks == std::vector<uint64_t>{0xBB});
    CHECK(g.logged("kicked player 0x00000000000000bb") == 1);
    g.r.tick(both, 15999);
    CHECK(g.logged("still here") == 0);
    g.r.tick(both, 16000);
    CHECK(g.logged("still here") == 1);
    g.r.tick(both, 30000);
    CHECK(g.logged("still here") == 1);
    CHECK(g.kicks.size() == 1);
    CHECK(g.tells.size() == 1);
    CHECK(g.logged("0x00000000000000aa") == 0);
}

TEST_CASE("refuse: no list within 60 s of being ready means no EMF; the time starts at ready, not at loading") {
    RefuseRig g;
    g.r.tick({seen(2, 0xCC, false)}, 0);
    g.r.tick({seen(2, 0xCC, false)}, 120000);  // still loading
    CHECK(g.tells.empty());
    g.r.tick({seen(2, 0xCC)}, 130000);  // ready now
    g.r.tick({seen(2, 0xCC)}, 189999);
    CHECK(g.tells.empty());
    g.r.tick({seen(2, 0xCC)}, 190000);
    REQUIRE(g.tells.size() == 1);
    CHECK(g.tells[0].second.rfind("This server needs EMF and these mods: beasts_pack 2.2.0, emberbrand 1.2.0.", 0) == 0);
    CHECK(g.logged("no EMF") == 1);
    RefuseRig late;
    late.r.tick({seen(2, 0xDD)}, 0);
    late.lists[0xDD] = kFull;  // the list came at 30 s
    late.r.tick({seen(2, 0xDD)}, 30000);
    late.r.tick({seen(2, 0xDD)}, 70000);
    CHECK(late.tells.empty());
}

TEST_CASE("refuse: the host and admins are never refused") {
    RefuseRig g;
    g.admins.insert(0xEE);
    g.lists[0xEE] = kLacking;
    const std::vector<RosterSeen> here = {seen(1, 0x11, true, true), seen(2, 0xEE)};
    for (uint64_t t = 0; t <= 80000; t += 1000) g.r.tick(here, t);
    CHECK(g.tells.empty());
    CHECK(g.kicks.empty());
    CHECK(g.logged("is an admin: not refused") == 1);
    CHECK(g.logged("0x0000000000000011") == 0);
}

TEST_CASE("refuse: a player who left is forgotten; back as another entity starts over; gone during the wait is not kicked") {
    RefuseRig g;
    g.lists[0xBB] = kLacking;
    g.r.tick({seen(3, 0xBB)}, 0);
    CHECK(g.tells.size() == 1);
    g.r.tick({}, 2000);  // left during the wait
    g.r.tick({}, 6000);
    CHECK(g.kicks.empty());
    g.r.tick({seen(4, 0xBB)}, 10000);  // back: told again
    CHECK(g.tells.size() == 2);
    RefuseRig h;
    h.r.tick({seen(3, 0xCC)}, 0);
    h.r.tick({seen(5, 0xCC)}, 50000);  // back as another entity with no step between
    h.r.tick({seen(5, 0xCC)}, 70000);
    CHECK(h.tells.empty());  // 60 s from this entity's ready
    h.r.tick({seen(5, 0xCC)}, 110000);
    CHECK(h.tells.size() == 1);
}

TEST_CASE("refuse: a line that could not be queued is tried again until the kick") {
    RefuseRig g;
    g.lists[0xBB] = kLacking;
    g.tell_ok = false;
    g.r.tick({seen(3, 0xBB)}, 0);
    g.r.tick({seen(3, 0xBB)}, 1000);
    CHECK(g.tell_tries == 2);
    g.tell_ok = true;
    g.r.tick({seen(3, 0xBB)}, 2000);
    g.r.tick({seen(3, 0xBB)}, 3000);
    CHECK(g.tells.size() == 1);
    CHECK(g.tell_tries == 3);
    g.r.tick({seen(3, 0xBB)}, 5000);
    CHECK(g.kicks.size() == 1);
}

TEST_CASE("refuse: state_of says what happened") {
    RefuseRig g;
    g.admins.insert(0xEE);
    g.lists[0xAA] = kFull;
    g.lists[0xBB] = kLacking;
    g.lists[0xEE] = kLacking;
    g.r.tick({seen(2, 0xAA), seen(3, 0xBB), seen(4, 0xEE)}, 0);
    CHECK(g.r.state_of(0xAA).empty());
    CHECK(g.r.state_of(0xBB).rfind("refused", 0) == 0);
    CHECK(g.r.state_of(0xBB).find("beasts_pack") != std::string::npos);
    CHECK(g.r.state_of(0xEE).rfind("admin", 0) == 0);
    CHECK(g.r.state_of(0x99).empty());
}
