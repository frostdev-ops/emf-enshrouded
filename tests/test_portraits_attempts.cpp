// What the loader remembers of its creature pictures calls (src/loader/portraits_attempts.h): a failed key waits for
// a changed DLL, game or entry; a drawn key is asked again after a game or DLL change.
#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "loader/content_portraits.h"
#include "loader/portraits_attempts.h"
#include "test_scratch.h"

namespace {
emf::loader::PortraitItem item(const std::string& key, int frames = 24) {
    emf::loader::PortraitItem p;
    p.mod = "m";
    p.key = key;
    p.template_guid = "503fa1d1-dce2-45a1-85a5-185a22666d33";
    p.frames = frames;
    return p;
}

emf::loader::PortraitAttempt attempt(const emf::loader::PortraitItem& p, const emf::loader::PortraitStamps& s, bool ok,
                                     const std::string& message = "") {
    emf::loader::PortraitAttempt a;
    a.key = p.key;
    a.entry = emf::loader::portrait_entry(p);
    a.dll = s.dll;
    a.game = s.game;
    a.ok = ok;
    a.message = message;
    return a;
}
}  // namespace

TEST_CASE("portraits memory: a failed key is not asked again until the DLL, the game or the entry changes") {
    const emf::loader::PortraitStamps now{"dll1", "game1"};
    const auto cached = [](const std::string&) { return false; };
    const auto bad = item("bad");
    const std::vector<emf::loader::PortraitAttempt> memory{attempt(bad, now, false, "no such model")};
    std::vector<std::string> remembered;
    CHECK(emf::loader::portraits_to_ask({bad, item("new")}, memory, now, cached, &remembered).size() == 1u);
    REQUIRE(remembered.size() == 1u);
    CHECK(remembered[0].find("no such model") != std::string::npos);
    CHECK(emf::loader::portraits_to_ask({bad}, memory, {"dll2", "game1"}, cached).size() == 1u);  // a new DLL
    CHECK(emf::loader::portraits_to_ask({bad}, memory, {"dll1", "game2"}, cached).size() == 1u);  // a game update
    CHECK(emf::loader::portraits_to_ask({item("bad", 12)}, memory, now, cached).size() == 1u);    // the mod's entry
}

TEST_CASE("portraits memory: a drawn key is asked again after a game or DLL change, not before") {
    const emf::loader::PortraitStamps before{"dll1", "game1"};
    const auto wolf = item("wolf");
    const std::vector<emf::loader::PortraitAttempt> memory{attempt(wolf, before, true)};
    const auto cached = [](const std::string&) { return true; };
    CHECK(emf::loader::portraits_to_ask({wolf}, memory, before, cached).empty());
    CHECK(emf::loader::portraits_to_ask({wolf}, memory, {"dll1", "game2"}, cached).size() == 1u);
    CHECK(emf::loader::portraits_to_ask({wolf}, memory, {"dll2", "game1"}, cached).size() == 1u);
    CHECK(emf::loader::portraits_to_ask({wolf}, memory, before, [](const std::string&) { return false; }).size() == 1u);
    // a cache from before the memory: asked once
    CHECK(emf::loader::portraits_to_ask({wolf}, {}, before, cached).size() == 1u);
}

TEST_CASE("portraits memory: the limit of 32 counts the keys asked, not the ones left alone") {
    const emf::loader::PortraitStamps now{"d", "g"};
    std::vector<emf::loader::PortraitItem> items;
    std::vector<emf::loader::PortraitAttempt> memory;
    for (int i = 0; i < 40; ++i) {
        items.push_back(item("k" + std::to_string(i)));
        if (i < 5) memory.push_back(attempt(items.back(), now, false, "x"));
    }
    const auto asked = emf::loader::portraits_to_ask(items, memory, now, [](const std::string&) { return false; });
    REQUIRE(asked.size() == emf::loader::kMaxPortraitsPerStart);
    CHECK(asked[0].key == "k5");
}

TEST_CASE("portraits memory: the file's text reads back, and a damaged file is no memory") {
    const emf::loader::PortraitStamps s{"dll\"1", "game1"};
    const auto text =
        emf::loader::attempts_text({attempt(item("wolf"), s, true), attempt(item("bad"), s, false, "line\nbreak")});
    const auto back = emf::loader::read_attempts(text);
    REQUIRE(back.size() == 2u);
    CHECK(back[0].ok);
    CHECK(back[0].dll == "dll\"1");
    CHECK(back[1].message == "line\nbreak");
    CHECK_FALSE(back[1].ok);
    CHECK(emf::loader::read_attempts("{not json").empty());
    CHECK(emf::loader::read_attempts("{\"attempts\": [{\"key\": \"a\", \"result\": \"maybe\"}]}").empty());
}

TEST_CASE("portraits memory: record_attempts takes each key's result; a call that failed whole fails every key asked") {
    const emf::loader::PortraitStamps s{"d", "g"};
    const auto a = item("a"), b = item("b"), c = item("c");
    const auto old_a = attempt(a, {"old", "old"}, false, "old failure");
    emf::loader::PortraitsOutcome out;
    out.drawn = {"a"};
    out.failed = {"b: no such model"};
    const auto merged = emf::loader::record_attempts({old_a, attempt(item("z"), s, true)}, {a, b, c}, out, s);
    REQUIRE(merged.size() == 4u);  // z kept, a replaced, b and c added
    const auto find = [&merged](const std::string& k) {
        for (const auto& m : merged)
            if (m.key == k) return m;
        return emf::loader::PortraitAttempt{};
    };
    CHECK(find("a").ok);
    CHECK(find("a").dll == "d");
    CHECK_FALSE(find("b").ok);
    CHECK(find("b").message == "no such model");
    CHECK_FALSE(find("c").ok);  // asked, no word back
    CHECK(find("c").key == "c");
    CHECK(find("z").ok);

    emf::loader::PortraitsOutcome whole;
    whole.call.code = emf::loader::kBuildNoExport;
    whole.call.message = "this emf_content.dll draws no portraits";
    const auto none = emf::loader::record_attempts({}, {a, b}, whole, s);
    REQUIRE(none.size() == 2u);
    CHECK_FALSE(none[0].ok);
    CHECK(none[0].message.find("draws no portraits") != std::string::npos);

    // a DLL that stays loaded after a call that answered: the keys it answered for are done, the rest wait for a change
    emf::loader::PortraitsOutcome stuck;
    stuck.drawn = {"a"};
    stuck.call.unloaded = false;
    stuck.call.message = "emf_content.dll is still loaded";
    const auto left = emf::loader::record_attempts({}, {a, b}, stuck, s);
    REQUIRE(left.size() == 2u);
    CHECK(left[0].ok);
    CHECK_FALSE(left[1].ok);
    CHECK(left[1].message.find("still loaded") != std::string::npos);
}

TEST_CASE("portraits memory: stamps change with the game's exe or data and with the DLL, by size and time") {
    const std::wstring root = test_scratch::temp_path(L"emf_portraits_stamps");
    test_scratch::remove_at_exit(root);
    std::filesystem::create_directories(root);
    const auto write = [&](const wchar_t* name, const std::string& text) {
        std::ofstream f(std::filesystem::path(root + L"\\" + name), std::ios::binary);
        f << text;
    };
    write(L"enshrouded.exe", "exe");
    write(L"enshrouded.kfc", "kfc");
    write(L"emf_content.dll", "dll");
    const auto one = emf::loader::portraits_stamps(root, "enshrouded", root + L"\\emf_content.dll");
    CHECK(one.dll.rfind("sha256:", 0) == 0);
    CHECK(emf::loader::portraits_stamps(root, "enshrouded", root + L"\\emf_content.dll").game == one.game);
    write(L"enshrouded.exe", "a longer exe, as after a game update");
    const auto two = emf::loader::portraits_stamps(root, "enshrouded", root + L"\\emf_content.dll");
    CHECK(two.game != one.game);
    CHECK(two.dll == one.dll);
    write(L"emf_content.dll", "another dll");
    CHECK(emf::loader::portraits_stamps(root, "enshrouded", root + L"\\emf_content.dll").dll != one.dll);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("portraits memory: wanted_portraits with a memory leaves a failed key out and asks a changed game's keys again") {
    const std::wstring root = test_scratch::temp_path(L"emf_portraits_wanted_memory");
    test_scratch::remove_at_exit(root);
    const std::wstring mods = root + L"\\mods", cache = root + L"\\cache";
    std::filesystem::create_directories(mods + L"\\m");
    std::filesystem::create_directories(cache + L"\\wolf");
    {
        std::ofstream f(std::filesystem::path(mods + L"\\m\\mod.json"), std::ios::binary);
        const std::string g = "503fa1d1-dce2-45a1-85a5-185a22666d33";
        f << "{\"portraits\": [{\"key\": \"bad\", \"template\": \"" + g + "\"}, {\"key\": \"wolf\", \"template\": \"" + g +
                 "\"}]}";
    }
    {
        std::ofstream f(std::filesystem::path(cache + L"\\wolf\\index.json"), std::ios::binary);
        f << "{}";
    }
    const emf::loader::PortraitStamps s1{"d", "g1"};
    const std::vector<emf::loader::PortraitAttempt> memory{attempt(item("bad"), s1, false, "nope"),
                                                           attempt(item("wolf"), s1, true)};
    std::vector<std::string> problems;
    emf::loader::PortraitMemory mem{memory, s1};
    CHECK(emf::loader::wanted_portraits(mods, cache, &problems, &mem).empty());
    REQUIRE(problems.size() == 1u);
    CHECK(problems[0].find("not tried again") != std::string::npos);
    problems.clear();
    mem.stamps = {"d", "g2"};  // the game was updated
    const auto asked = emf::loader::wanted_portraits(mods, cache, &problems, &mem);
    REQUIRE(asked.size() == 2u);
    CHECK(asked[0].key == "bad");
    CHECK(asked[1].key == "wolf");  // cached, but drawn under the old game: the DLL decides whether it is current
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("portraits memory: a mod.json over 1 MiB is skipped with a line saying so") {
    const std::wstring root = test_scratch::temp_path(L"emf_portraits_big_manifest");
    test_scratch::remove_at_exit(root);
    const std::wstring mods = root + L"\\mods";
    std::filesystem::create_directories(mods + L"\\big");
    {
        std::ofstream f(std::filesystem::path(mods + L"\\big\\mod.json"), std::ios::binary);
        f << "{\"portraits\": [], \"pad\": \"" << std::string((1u << 20) + 10, 'x') << "\"}";
    }
    std::vector<std::string> problems;
    CHECK(emf::loader::wanted_portraits(mods, root + L"\\cache", &problems).empty());
    REQUIRE(problems.size() == 1u);
    CHECK(problems[0].rfind("big: ", 0) == 0);
    CHECK(problems[0].find("1 MiB") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
