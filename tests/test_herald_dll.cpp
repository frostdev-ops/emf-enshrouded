// Server Herald's DLL (mods/herald) loaded over the Lua tests' fake API (tests/lua_test_fake.h): what it does once the
// Mods tab switched it off. Its rules alone are test_herald.cpp.
#include <windows.h>

#include <doctest.h>

#include <string>

#include "emf/generated/components.hpp"
#include "lua_test_fake.h"
#include "test_scratch.h"

namespace {
std::wstring herald_exe_dir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    return s.substr(0, s.find_last_of(L"\\/"));
}

// The Mods tab's switch: emf\config\herald.ini [herald] enabled, then the core's "enabled" change call.
void switch_herald(Fake& f, const std::wstring& ini, bool on) {
    WritePrivateProfileStringW(L"herald", L"enabled", on ? L"1" : L"0", ini.c_str());
    for (const auto& [mod, watcher] : f.setting_watchers)
        if (mod == "herald") watcher.first(kServer, "herald", "enabled", watcher.second);
}

// The herald's on_tick, to run one step of the server world by hand.
EmfWorldCallback g_herald_tick = nullptr;
void* g_herald_tick_user = nullptr;

// A setting changed in the Mods tab: the core's change call for `key` (its value is in f.settings already).
void change_setting(Fake& f, const char* key) {
    for (const auto& [mod, watcher] : f.setting_watchers)
        if (mod == "herald") watcher.first(kServer, "herald", key, watcher.second);
}

std::string all_replies(Fake& f) {
    std::string out;
    for (const auto& r : f.replies) out += r + "\n";
    return out;
}
}  // namespace

TEST_CASE("herald.dll: switched off in the Mods tab, every herald command says so and sends nothing") {
    const test_scratch::BuildFolderLock lock;  // herald.ini is <exe folder>\emf\config\herald.ini for every run of this build
    const std::wstring dir = herald_exe_dir();
    CreateDirectoryW((dir + L"\\emf").c_str(), nullptr);
    CreateDirectoryW((dir + L"\\emf\\config").c_str(), nullptr);
    const std::wstring ini = dir + L"\\emf\\config\\herald.ini";
    struct Remove {
        std::wstring path;
        ~Remove() { DeleteFileW(path.c_str()); }
    } remove{ini};
    Rig r(L"herald_dll");
    r.fake.settings["herald.chat_log"] = "0";  // nothing is written into the build folder's emf\logs
    r.fake.settings["herald.playtime"] = "0";
    static EmfApi api;  // the DLL keeps the pointer for the life of the process
    api = r.api;
    api.on_tick = [](EmfWorldCallback, void*) {};  // the herald's second: not run here
    api.local_player = []() -> uint32_t { return 1; };
    HMODULE m = LoadLibraryW((dir + L"\\dist\\emf\\mods\\herald\\herald.dll").c_str());
    REQUIRE(m != nullptr);
    auto load = reinterpret_cast<EmfModLoadFn>(GetProcAddress(m, "emf_mod_load"));
    REQUIRE(load != nullptr);
    REQUIRE(load(&api));
    switch_herald(r.fake, ini, true);
    r.run_command("herald.say Hello all");
    REQUIRE(r.fake.broadcasts.size() == 1u);

    switch_herald(r.fake, ini, false);
    for (const char* command : {"herald.say Still here?", "herald.tell #1 hi", "herald.restart 5 nightly", "herald.cancel",
                                "herald.save", "herald.seen Ember", "herald.reload", "players", "playtime", "motd"}) {
        const std::string line = command;
        CAPTURE(line);
        r.fake.replies.clear();
        r.run_command(command);
        CHECK(all_replies(r.fake).find("Server Herald is switched off") != std::string::npos);
    }
    CHECK(r.fake.broadcasts.size() == 1u);  // nothing more went out
    CHECK(r.fake.tells.empty());
    CHECK(r.fake.saves == 0);

    switch_herald(r.fake, ini, true);  // on again: the commands work again
    r.fake.replies.clear();
    r.run_command("herald.say Back");
    CHECK(r.fake.broadcasts.size() == 2u);
    switch_herald(r.fake, ini, false);  // left off: the DLL stays loaded in this process
}

TEST_CASE("herald.dll: its lines go without a speaker (a setting, on by default); the MOTD fills in the character name") {
    const test_scratch::BuildFolderLock lock;
    const std::wstring dir = herald_exe_dir();
    CreateDirectoryW((dir + L"\\emf").c_str(), nullptr);
    CreateDirectoryW((dir + L"\\emf\\config").c_str(), nullptr);
    const std::wstring ini = dir + L"\\emf\\config\\herald.ini";
    struct Remove {
        std::wstring path;
        ~Remove() { DeleteFileW(path.c_str()); }
    } remove{ini};
    Rig r(L"herald_dll_system");
    r.fake.settings["herald.chat_log"] = "0";
    r.fake.settings["herald.playtime"] = "0";
    r.fake.settings["herald.motd"] = "Welcome {character} ({name})!";
    r.fake.settings["herald.motd_delay_seconds"] = "0";
    static EmfApi api;  // the DLL keeps the pointer for the life of the process
    api = r.api;
    api.on_tick = [](EmfWorldCallback cb, void* user) {
        g_herald_tick = cb;
        g_herald_tick_user = user;
    };
    api.local_player = []() -> uint32_t { return 1; };
    HMODULE m = LoadLibraryW((dir + L"\\dist\\emf\\mods\\herald\\herald.dll").c_str());
    REQUIRE(m != nullptr);
    auto load = reinterpret_cast<EmfModLoadFn>(GetProcAddress(m, "emf_mod_load"));
    REQUIRE(load != nullptr);
    REQUIRE(load(&api));
    REQUIRE(g_herald_tick != nullptr);
    switch_herald(r.fake, ini, true);

    // player 2 joins: the join line for everyone and the MOTD for them, both without a speaker
    namespace ecs = emf::gen::ecs;
    r.fake.components[{ecs::PlayerState::kHash, 2}] = std::vector<uint8_t>(ecs::PlayerState::kSize, 0);
    r.fake.components[{ecs::CurrentTransform::kHash, 2}] = std::vector<uint8_t>(sizeof(ecs::CurrentTransform), 0);
    r.fake.player_info(1, 2, 0x22);  // BasicPlayerInfos: entry id - 1 holds its account
    r.fake.names[2] = "Ember";
    r.fake.character_names[2] = "Elara";
    g_herald_tick(kServer, g_herald_tick_user);
    REQUIRE(r.fake.tells.size() == 1u);
    CHECK(r.fake.tells[0] == std::make_pair(uint32_t(2), std::string("[Server] Welcome Elara (Ember)!")));
    CHECK(r.fake.tell_flags == std::vector<uint32_t>{EMF_CHAT_SYSTEM});
    REQUIRE(r.fake.broadcasts.size() == 1u);
    CHECK(r.fake.broadcasts[0] == "[Server] Ember joined (1 online)");
    CHECK(r.fake.broadcast_flags == std::vector<uint32_t>{EMF_CHAT_SYSTEM});

    r.run_command("herald.say Hello all");
    r.run_command("herald.tell #1 hi");
    CHECK(r.fake.broadcasts.size() == 2u);
    CHECK(r.fake.broadcast_flags == std::vector<uint32_t>{EMF_CHAT_SYSTEM, EMF_CHAT_SYSTEM});
    CHECK(r.fake.tells.size() == 2u);
    CHECK(r.fake.tell_flags == std::vector<uint32_t>{EMF_CHAT_SYSTEM, EMF_CHAT_SYSTEM});

    r.fake.settings["herald.system_lines"] = "0";  // switched off: normal chat lines
    change_setting(r.fake, "system_lines");
    r.run_command("herald.say Plain");
    r.run_command("herald.tell #1 plain");
    CHECK(r.fake.broadcasts.size() == 3u);
    CHECK(r.fake.broadcast_flags.size() == 2u);
    CHECK(r.fake.tells.size() == 3u);
    CHECK(r.fake.tell_flags.size() == 2u);

    // /motd from player 2's chat: the character name
    r.fake.sender = EmfCommandSender{};
    r.fake.sender.size = sizeof(EmfCommandSender);
    r.fake.sender.entity = 2;
    r.fake.sender.account = 0x22;
    r.fake.sender.from_chat = 1;
    r.fake.replies.clear();
    r.run_command("motd");
    CHECK(all_replies(r.fake) == "[Server] Welcome Elara (Ember)!\n");

    // a core older than v30 (its table ends before player_character_name): normal lines, still sent, and {character}
    // is the player's name
    r.fake.settings["herald.system_lines"] = "1";
    change_setting(r.fake, "system_lines");
    api.size = static_cast<uint32_t>(offsetof(EmfApi, player_character_name));
    r.run_command("herald.say Old core");
    r.run_command("herald.tell #1 old core");
    r.fake.replies.clear();
    r.run_command("motd");
    api.size = r.api.size;
    CHECK(r.fake.broadcasts.size() == 4u);
    CHECK(r.fake.broadcast_flags.size() == 2u);
    CHECK(r.fake.tells.size() == 4u);
    CHECK(r.fake.tell_flags.size() == 2u);
    CHECK(all_replies(r.fake) == "[Server] Welcome Ember (Ember)!\n");
    switch_herald(r.fake, ini, false);  // left off, as the test above leaves it
}
