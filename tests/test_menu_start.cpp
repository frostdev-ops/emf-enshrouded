// load_world's state machine (src/core/menu_start.h) against a fake game: fake save state (save2), game app and pregame
// objects, fake command posters and a fake pregame dispatcher that applies what was posted, as the game does.
#include <doctest.h>

#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "core/menu_start.h"

using namespace emf::core;
namespace L = emf::core::menu_layout;

namespace {

struct FakeGame {
    std::vector<uint8_t> save2 = std::vector<uint8_t>(0x119400, uint8_t{0});
    std::vector<uint8_t> app = std::vector<uint8_t>(0x35300, uint8_t{0});
    alignas(8) uint8_t pregame[0x200] = {};
    std::deque<std::string> texts;  // record names (stable addresses)
    struct Post {
        uint8_t cmd;
        uint16_t value;
        std::thread::id thread;
        void* ui;
    };
    std::vector<Post> posts;
    size_t applied = 0;
    bool server_running = false, player = false, any_running = false, started = false, queue_full = false;
    MenuStart* menu = nullptr;  // set: 0x28 runs Start Play as the game does (the guard, back to the menu on 0x1F)
    int refused_starts = 0;     // Start Play runs that the character check stopped ("No Character selected")
    size_t foreign_queued = 0;  // commands other code queued that the game has not run yet (the menu's own clicks)
    uint64_t now = 1000;
    std::vector<std::string> log;

    L::AppLayout lay;  // where this build's game app keeps its fields (load_world reads them there)
    explicit FakeGame(const L::AppLayout& layout = L::kApp1273043) : lay(layout) {
        const uintptr_t a = reinterpret_cast<uintptr_t>(app.data()), s = reinterpret_cast<uintptr_t>(save2.data());
        std::memcpy(pregame + L::kPregameGameApp, &a, 8);
        std::memcpy(pregame + L::kPregameSave2, &s, 8);
        save2[L::kWorldListState] = 3;  // the lists are loaded
    }
    void put_name(std::vector<uint8_t>& mem, size_t at, const char* name) {
        texts.emplace_back(name);
        const char* p = texts.back().c_str();
        const uint64_t n = texts.back().size();
        std::memcpy(mem.data() + at, &p, 8);
        std::memcpy(mem.data() + at + 8, &n, 8);
    }
    void world(uint32_t storage, uint32_t index, uint8_t state, const char* name) {
        const size_t rec = L::kWorldRecords + storage * L::kWorldStorageStride + index * L::kWorldRecordSize;
        save2[rec] = state;
        put_name(save2, rec + L::kWorldRecordName, name);
    }
    void character(uint32_t storage, uint32_t index, uint8_t state, const char* name) {
        const size_t rec = L::kCharacterRecords + storage * L::kCharacterStorageStride + index * L::kCharacterRecordSize;
        save2[rec] = state;
        put_name(save2, rec + L::kCharacterRecordName, name);
    }
    void set16(std::vector<uint8_t>& mem, size_t at, uint16_t v) { std::memcpy(mem.data() + at, &v, 2); }
    // The game's pregame dispatcher: runs the commands posted since the last call, as 0x140869090 does.
    void dispatch() {
        for (; applied < posts.size(); ++applied) {
            const Post& p = posts[applied];
            if (p.cmd == 0x11) {  // selectCharacter
                set16(save2, L::kSelectedCharacter, p.value);
                save2[L::kSelectedCharacterValid] = 1;
            } else if (p.cmd == 0x16) {  // selectWorld, then playWorld
                set16(save2, L::kSelectedWorld, p.value);
                save2[L::kSelectedWorldValid] = 1;
                set16(app, lay.play_slot, p.value);
                app[lay.play_slot_set] = 1;
                app[lay.adventure] = 0;
                const size_t rec = L::kWorldRecords + (p.value >> 8) * L::kWorldStorageStride +
                                   ((p.value & 0xFF) - 1) * L::kWorldRecordSize;
                std::memcpy(app.data() + lay.session_name, save2.data() + rec + L::kWorldRecordName, 16);
            } else if (p.cmd == 0x28) {
                if (!menu) {
                    started = true;
                    continue;
                }
                // Start Play: the main menu is left, Start Play's character check runs (the guard is its post-hook)
                menu->on_menu_state(pg(), 2);
                menu->on_other_state(pg(), 3, 0);
                uint8_t result[16] = {};
                menu->guard(result, s2());
                if (result[0] == L::kNoCharacterSelected) {  // "No Character selected": back to the main menu
                    ++refused_starts;
                    menu->on_other_state(pg(), 3, 2);
                    menu->on_menu_state(pg(), 0);
                } else {
                    started = true;
                }
            }
        }
    }
    MenuStart::Deps deps() {
        MenuStart::Deps d;
        d.read = [](uintptr_t a, void* out, size_t n) {
            std::memcpy(out, reinterpret_cast<const void*>(a), n);
            return true;
        };
        d.select_character = [this](void* ui, uint16_t slot) { return post(0x11, slot, ui); };
        d.select_world = [this](void* ui, uint16_t slot) { return post(0x16, slot, ui); };
        d.session_area = [this](void* ui, uint8_t area) { return post(0x27, area, ui); };
        d.start = [this](void* ui) { return post(0x28, 0, ui); };
        d.world_ready = [this] { return server_running && player; };
        d.any_world_running = [this] { return any_running || server_running; };
        d.queued_commands = [this](void* ui) -> size_t { return ui ? posts.size() - applied + foreign_queued : SIZE_MAX; };
        d.now_ms = [this] { return now; };
        d.log = [this](const std::string& s) { log.push_back(s); };
        d.app = lay;
        return d;
    }
    bool post(uint8_t cmd, uint16_t value, void* ui) {
        if (queue_full) return false;
        posts.push_back({cmd, value, std::this_thread::get_id(), ui});
        return true;
    }
    void* pg() { return pregame; }
    uintptr_t s2() { return reinterpret_cast<uintptr_t>(save2.data()); }
};

// The game app layouts of the two game exes load_world knows: every test below runs on both.
struct On1273043 {
    static constexpr L::AppLayout app = L::kApp1273043;
};
struct On1283531 {
    static constexpr L::AppLayout app = L::kApp1283531;
};

int g_ui = 0;  // the UI context (its address is all the machine needs)
void* const kUi = &g_ui;

struct Capture {
    int calls = 0;
    LoadWorldResult last;
    MenuStart::Done done() {
        return [this](const LoadWorldResult& r) {
            ++calls;
            last = r;
        };
    }
};

LoadWorldRequest req(uint32_t world = 1, uint32_t character = 1, const char* name = "") {
    LoadWorldRequest r;
    r.world = world;
    r.character = character;
    r.expect_name = name;
    r.timeout_ms = 60000;
    return r;
}

// the LOCAL test world and character, plus the user's Steam character and world in the same slots
void standard_saves(FakeGame& g) {
    g.world(0, 0, L::kRecordExists, "My World 1");
    g.character(0, 0, L::kRecordExists, "Character 2");
    g.world(1, 0, L::kRecordExists, "Steam World");
    g.character(1, 0, L::kRecordExists, "Steam Hero");
}

}  // namespace

TYPE_TO_STRING(On1273043);
TYPE_TO_STRING(On1283531);

TEST_CASE("load_world layout: 1273043's and 1283531's game exes; build 1283531's game app and UI fields are 0x20 later") {
    const int c1273043 = emf::known_build_index(1273043), c1283531 = emf::known_build_index(1283531);
    REQUIRE(menu_app_layout(c1273043).has_value());
    REQUIRE(menu_app_layout(c1283531).has_value());
    const L::AppLayout a = *menu_app_layout(c1273043), b = *menu_app_layout(c1283531);
    CHECK(a.ui_command_queue == 0x33CC0u);
    CHECK(a.mode == 0x34FA8u);
    CHECK(a.session_name == 0x34FE8u);
    CHECK(a.play_slot == 0x35058u);
    CHECK(a.play_slot_set == 0x3505Au);
    CHECK(a.adventure == 0x35270u);
    CHECK(b.ui_command_queue == a.ui_command_queue + 0x20);  // counted in the new client's code (356 uses)
    CHECK(b.mode == a.mode + 0x20);
    CHECK(b.session_name == a.session_name + 0x20);
    CHECK(b.play_slot == a.play_slot + 0x20);
    CHECK(b.play_slot_set == a.play_slot_set + 0x20);
    CHECK(b.adventure == a.adventure + 0x20);
    // 1285601 (a hotfix): each of those offsets is used in its client's code as often as in 1283531's
    const int c1285601 = emf::known_build_index(1285601);
    REQUIRE(menu_app_layout(c1285601).has_value());
    const L::AppLayout c = *menu_app_layout(c1285601);
    CHECK(c.ui_command_queue == b.ui_command_queue);
    CHECK(c.mode == b.mode);
    CHECK(c.session_name == b.session_name);
    CHECK(c.play_slot == b.play_slot);
    CHECK(c.play_slot_set == b.play_slot_set);
    CHECK(c.adventure == b.adventure);
    CHECK_FALSE(menu_app_layout(emf::known_build_index(1285601, true)).has_value());
    REQUIRE(menu_app_layout(emf::known_build_index(1287369)).has_value());  // 1287369: 1283531's fields again
    CHECK_FALSE(menu_app_layout(emf::known_build_index(1287369, true)).has_value());
    // the dedicated servers have no menu; 1269579 and unknown builds were never checked
    CHECK_FALSE(menu_app_layout(emf::known_build_index(1273043, true)).has_value());
    CHECK_FALSE(menu_app_layout(emf::known_build_index(1283531, true)).has_value());
    CHECK_FALSE(menu_app_layout(emf::known_build_index(1269579)).has_value());
    CHECK_FALSE(menu_app_layout(-1).has_value());
}

namespace {
// postStart's first bytes: ... sub rsp, 0x20; mov rax, [rcx+0xD0]; mov rbx, [rax+queue]; lea r8, [rbx+0x18] ...
std::vector<uint8_t> post_start_code(uint32_t queue) {
    std::vector<uint8_t> c = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x81, 0xD0,
                              0x00, 0x00, 0x00, 0x48, 0x8B, 0x98};
    for (int i = 0; i < 4; ++i) c.push_back(static_cast<uint8_t>(queue >> (8 * i)));
    c.insert(c.end(), {0x4C, 0x8D, 0x43, 0x18});
    return c;
}
}  // namespace

TEST_CASE("load_world layout: postStart's own code must read the UI's command queue where the build's layout says") {
    const auto code = post_start_code(0x33CE0);
    CHECK(menu_queue_problem(L::kApp1283531, code.data(), code.size()).empty());
    const std::string wrong = menu_queue_problem(L::kApp1273043, code.data(), code.size());
    CHECK(wrong == "postStart reads the UI's command queue at +0x33ce0, the layout says +0x33cc0");
    const auto old_code = post_start_code(0x33CC0);
    CHECK(menu_queue_problem(L::kApp1273043, old_code.data(), old_code.size()).empty());
    auto other = code;
    other[18] = 0x8D;  // lea, not mov: not the instruction the layout was read from
    CHECK(menu_queue_problem(L::kApp1283531, other.data(), other.size()) ==
          "postStart's code does not read the UI's command queue where EMF expects (mov rbx, [rax+disp32] at +0x11)");
    CHECK_FALSE(menu_queue_problem(L::kApp1283531, code.data(), 20).empty());  // too short to hold the disp32
    CHECK_FALSE(menu_queue_problem(L::kApp1283531, nullptr, 0).empty());
}

TEST_CASE_TEMPLATE("load_world: frame A selects the LOCAL character and world, frame B checks them and posts offline + start", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);  // the main menu is up
    Capture c;
    LoadWorldResult no;
    REQUIRE(m.request(req(1, 1, "Character 2"), c.done(), &no));
    CHECK(g.posts.empty());  // nothing is posted outside the UI frame
    m.on_ui_frame(kUi);      // frame A
    REQUIRE(g.posts.size() == 2);
    CHECK(g.posts[0].cmd == 0x11);
    CHECK(g.posts[0].value == 0x0001);  // LOCAL character 1: storage byte 0
    CHECK(g.posts[1].cmd == 0x16);
    CHECK(g.posts[1].value == 0x0001);  // LOCAL world slot 1
    CHECK(g.posts[0].ui == kUi);
    CHECK(m.armed());  // the guard is on from here
    m.on_ui_frame(kUi);  // the game has not run the commands yet: nothing more is posted
    CHECK(g.posts.size() == 2);
    g.dispatch();
    m.on_ui_frame(kUi);  // frame B
    REQUIRE(g.posts.size() == 4);
    CHECK(g.posts[2].cmd == 0x27);
    CHECK(g.posts[2].value == 0);  // session area 0 = Offline
    CHECK(g.posts[3].cmd == 0x28);  // start
    m.on_menu_state(g.pg(), 2);     // the game leaves the main menu for Start Play
    g.now += 1000;
    g.server_running = true;  // the server world steps a second after the start, while the save still loads
    m.on_ui_frame(kUi);
    CHECK(c.calls == 0);
    CHECK(m.armed());
    g.now += 14000;
    m.on_ui_frame(kUi);
    CHECK(c.calls == 0);  // loading
    g.player = true;      // the local player is in the world
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK(c.last.ok);
    CHECK(c.last.world == 1u);
    CHECK(c.last.character == 1u);
    CHECK(c.last.world_name == "My World 1");
    CHECK(c.last.character_name == "Character 2");
    CHECK(c.last.ms == 15000u);
    CHECK_FALSE(m.armed());  // the guard is off again
    CHECK_FALSE(m.busy());
    CHECK(m.session_world_name() == std::optional<std::string>("My World 1"));
}

TEST_CASE_TEMPLATE("load_world: requests from the bridge thread are only handed over; the UI frame does every post", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    Capture c;
    bool accepted = false;
    std::thread bridge([&] {
        LoadWorldResult no;
        accepted = m.request(req(), c.done(), &no);
    });
    bridge.join();
    REQUIRE(accepted);
    CHECK(g.posts.empty());
    m.on_ui_frame(kUi);
    REQUIRE(g.posts.size() == 2);
    for (const auto& p : g.posts) CHECK(p.thread == std::this_thread::get_id());  // the UI frame's thread
}

TEST_CASE_TEMPLATE("load_world: a slot that does not exist, a wrong name and bad numbers are refused", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    g.world(0, 1, 1, "");  // LOCAL world slot 2 is empty (state 1)
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    LoadWorldResult no;
    Capture c;
    // numbers out of range: refused at once
    CHECK_FALSE(m.request(req(0, 1), c.done(), &no));
    CHECK(no.code == "bad_request");
    CHECK_FALSE(m.request(req(11, 1), c.done(), &no));
    CHECK_FALSE(m.request(req(1, 17), c.done(), &no));
    CHECK(c.calls == 0);
    // an empty slot: refused at the first UI frame, nothing posted
    REQUIRE(m.request(req(2, 1), c.done(), &no));
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK_FALSE(c.last.ok);
    CHECK(c.last.code == "refused");
    CHECK(c.last.error.find("LOCAL world slot 2") != std::string::npos);
    CHECK(g.posts.empty());
    // a character slot that does not exist
    REQUIRE(m.request(req(1, 5), c.done(), &no));
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 2);
    CHECK(c.last.error.find("LOCAL character slot 5") != std::string::npos);
    // the name the request expects is not that character's
    REQUIRE(m.request(req(1, 1, "Character 1"), c.done(), &no));
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 3);
    CHECK(c.last.code == "refused");
    CHECK(c.last.error.find("'Character 2'") != std::string::npos);
    CHECK(g.posts.empty());
    CHECK_FALSE(m.armed());
}

TEST_CASE_TEMPLATE("load_world: one at a time; not while a world runs", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    LoadWorldResult no;
    Capture c, d;
    REQUIRE(m.request(req(), c.done(), &no));
    CHECK_FALSE(m.request(req(), d.done(), &no));
    CHECK(no.code == "busy");
    FakeGame g2(Build::app);
    standard_saves(g2);
    g2.any_running = true;  // in a game (or someone else's)
    MenuStart m2(g2.deps());
    m2.on_menu_state(g2.pg(), 0);
    Capture e;
    REQUIRE(m2.request(req(), e.done(), &no));
    m2.on_ui_frame(kUi);
    REQUIRE(e.calls == 1);
    CHECK(e.last.code == "unavailable");
    CHECK(e.last.error.find("not at the main menu") != std::string::npos);
    CHECK(g2.posts.empty());
}

TEST_CASE_TEMPLATE("load_world: waits for the main menu and the save lists, then times out naming what it waited for", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    g.save2[L::kWorldListState] = 1;  // still loading
    MenuStart m(g.deps());
    LoadWorldResult no;
    Capture c;
    REQUIRE(m.request(req(), c.done(), &no));
    m.on_ui_frame(kUi);  // no main menu yet (the game is still starting): waits
    CHECK(c.calls == 0);
    m.on_menu_state(g.pg(), 0);
    m.on_ui_frame(kUi);  // the menu is up, the lists are not: waits
    CHECK(c.calls == 0);
    CHECK(g.posts.empty());
    g.now += 60001;
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK(c.last.code == "timeout");
    CHECK(c.last.error.find("save lists") != std::string::npos);
    CHECK(g.posts.empty());
}

TEST_CASE_TEMPLATE("load_world: poll ends a request whose time is up even when no UI frame runs", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    LoadWorldResult no;
    Capture c;
    REQUIRE(m.request(req(), c.done(), &no));
    m.poll();
    CHECK(c.calls == 0);
    g.now += 60001;
    m.poll();
    REQUIRE(c.calls == 1);
    CHECK(c.last.code == "timeout");
    CHECK(c.last.error.find("main menu") != std::string::npos);
    CHECK_FALSE(m.busy());
}

TEST_CASE_TEMPLATE("load_world: the selection that never shows (the game ignored it) ends the request", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    LoadWorldResult no;
    Capture c;
    REQUIRE(m.request(req(), c.done(), &no));
    m.on_ui_frame(kUi);
    REQUIRE(g.posts.size() == 2);
    g.now += MenuStart::kSelectMs + 1;  // no dispatch: the selection never shows
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK(c.last.code == "unavailable");
    CHECK(c.last.error.find("did not take the selection") != std::string::npos);
    CHECK(g.posts.size() == 2);  // never started
    CHECK_FALSE(m.armed());
}

TEST_CASE_TEMPLATE("load_world: frame B never starts when the selection shows a Steam character (the user clicked one)", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    LoadWorldResult no;
    Capture c;
    REQUIRE(m.request(req(), c.done(), &no));
    m.on_ui_frame(kUi);
    g.dispatch();
    g.set16(g.save2, L::kSelectedCharacter, 0x0101);  // Steam character 1 selected meanwhile
    g.now += MenuStart::kSelectMs + 1;
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK_FALSE(c.last.ok);
    CHECK(g.posts.size() == 2);  // no session area, no start
}

TEST_CASE_TEMPLATE("load_world guard: it does nothing outside an EMF load, even with a Steam character selected", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    g.set16(g.save2, L::kSelectedCharacter, 0x0101);  // the user plays their Steam character
    g.set16(g.app, g.lay.play_slot, 0x0101);
    uint8_t result[16] = {};
    CHECK_FALSE(m.guard(result, g.s2()));
    CHECK(result[0] == 0);  // untouched: the game decides
}

TEST_CASE_TEMPLATE("local_only: the guard refuses every start with a Steam character or world, or another name, without a load_world", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.set_local_only("Character 2");  // [debug] local_only=1 (a boot join through +connect never posts a load_world)
    CHECK(m.armed());
    m.on_menu_state(g.pg(), 0);
    g.set16(g.save2, L::kSelectedCharacter, 0x0001);
    uint8_t ok[16] = {};
    CHECK_FALSE(m.guard(ok, g.s2()));  // LOCAL slot 1, 'Character 2': left alone
    CHECK(ok[0] == 0);
    g.set16(g.save2, L::kSelectedCharacter, 0x0101);  // the Steam character
    uint8_t r1[16] = {};
    CHECK(m.guard(r1, g.s2()));
    CHECK(r1[0] == L::kNoCharacterSelected);
    g.set16(g.save2, L::kSelectedCharacter, 0x0001);
    g.set16(g.app, g.lay.play_slot, 0x0101);  // a Steam world
    uint8_t r2[16] = {};
    CHECK(m.guard(r2, g.s2()));
    g.set16(g.app, g.lay.play_slot, 0x0000);
    g.character(0, 0, L::kRecordExists, "Somebody Else");
    uint8_t r3[16] = {};
    CHECK(m.guard(r3, g.s2()));
    CHECK(r3[0] == L::kNoCharacterSelected);
    // it stays on: back in the main menu after a refused start
    m.on_menu_state(g.pg(), 2);
    m.on_menu_state(g.pg(), 0);
    m.on_ui_frame(kUi);
    CHECK(m.armed());
    uint8_t r4[16] = {};
    CHECK(m.guard(r4, g.s2()));
}

TEST_CASE_TEMPLATE("local_only without a name: LOCAL storage is enough", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.set_local_only("");
    m.on_other_state(g.pg(), 3, 0);  // Start Play (the app is noted before its character check)
    g.set16(g.save2, L::kSelectedCharacter, 0x0001);
    g.character(0, 0, L::kRecordExists, "Somebody Else");
    uint8_t ok[16] = {};
    CHECK_FALSE(m.guard(ok, g.s2()));
    g.set16(g.save2, L::kSelectedCharacter, 0x0101);
    uint8_t r[16] = {};
    CHECK(m.guard(r, g.s2()));
}

TEST_CASE_TEMPLATE("load_world guard: during an EMF load a Steam character or world (or another name) becomes 'No Character selected'", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    LoadWorldResult no;
    Capture c;
    REQUIRE(m.request(req(1, 1, "Character 2"), c.done(), &no));
    m.on_ui_frame(kUi);
    g.dispatch();
    m.on_ui_frame(kUi);  // started
    REQUIRE(m.armed());
    uint8_t ok[16] = {};
    CHECK_FALSE(m.guard(ok, g.s2()));  // the LOCAL pair: left alone
    CHECK(ok[0] == 0);
    g.set16(g.save2, L::kSelectedCharacter, 0x0101);  // a Steam character slipped in
    uint8_t r1[16] = {};
    CHECK(m.guard(r1, g.s2()));
    CHECK(r1[0] == L::kNoCharacterSelected);
    g.set16(g.save2, L::kSelectedCharacter, 0x0001);
    g.set16(g.app, g.lay.play_slot, 0x0101);  // the world to load is Steam storage
    uint8_t r2[16] = {};
    CHECK(m.guard(r2, g.s2()));
    CHECK(r2[0] == L::kNoCharacterSelected);
    g.set16(g.app, g.lay.play_slot, 0x0001);
    g.character(0, 0, L::kRecordExists, "Somebody Else");  // the name check
    uint8_t r3[16] = {};
    CHECK(m.guard(r3, g.s2()));
    CHECK(r3[0] == L::kNoCharacterSelected);
    // the game then goes back to the main menu: the request ends as refused, the guard turns off
    m.on_menu_state(g.pg(), 2);
    m.on_menu_state(g.pg(), 0);
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK(c.last.code == "refused");
    CHECK(c.last.error.find("guard") != std::string::npos);
    CHECK_FALSE(m.armed());
    uint8_t after[16] = {};
    g.set16(g.save2, L::kSelectedCharacter, 0x0101);
    CHECK_FALSE(m.guard(after, g.s2()));
    CHECK(after[0] == 0);
}

TEST_CASE_TEMPLATE("load_world: back at the main menu after start without the guard: the game refused it", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    LoadWorldResult no;
    Capture c;
    REQUIRE(m.request(req(), c.done(), &no));
    m.on_ui_frame(kUi);
    g.dispatch();
    m.on_ui_frame(kUi);
    m.on_menu_state(g.pg(), 2);
    m.on_menu_state(g.pg(), 0);
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK(c.last.code == "unavailable");
    CHECK(c.last.error.find("back to the main menu") != std::string::npos);
}

TEST_CASE_TEMPLATE("load_world: a full command queue is retried on the next frame", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    LoadWorldResult no;
    Capture c;
    REQUIRE(m.request(req(), c.done(), &no));
    g.queue_full = true;
    m.on_ui_frame(kUi);
    CHECK(g.posts.empty());
    CHECK(c.calls == 0);
    g.queue_full = false;
    m.on_ui_frame(kUi);
    CHECK(g.posts.size() == 2);
}

TEST_CASE_TEMPLATE("load_world: another pregame state (Start Play) means the main menu is not active", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    CHECK(m.menu_active());
    m.on_other_state(g.pg(), 3, 1);  // a command in another state: no change
    CHECK(m.menu_active());
    m.on_other_state(g.pg(), 3, 0);  // Start Play entered
    CHECK_FALSE(m.menu_active());
    m.on_menu_state(g.pg(), 1);      // a command for the main menu: it is up
    CHECK(m.menu_active());
    MenuStart late(g.deps());        // hooks that came after the menu was entered learn the pregame from any call
    late.on_other_state(g.pg(), 0, 1);
    CHECK(late.game_app() == reinterpret_cast<uintptr_t>(g.app.data()));
}

// ---- edge cases ----

namespace {
// the request started (frame A, the game ran the selection, frame B posted offline + start), with a short timeout
void start_with_timeout(FakeGame& g, MenuStart& m, Capture& c, uint32_t timeout_ms) {
    LoadWorldRequest r = req(1, 1, "Character 2");
    r.timeout_ms = timeout_ms;
    LoadWorldResult no;
    REQUIRE(m.request(r, c.done(), &no));
    m.on_ui_frame(kUi);
    g.dispatch();
    m.on_ui_frame(kUi);
    REQUIRE(g.posts.size() == 4);
    REQUIRE(g.posts[3].cmd == 0x28);
}
}  // namespace

TEST_CASE_TEMPLATE("load_world guard: a timeout after Start was posted leaves the guard armed until the game is back in the main menu", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    Capture c;
    start_with_timeout(g, m, c, 10000);
    g.now += 10001;
    m.poll();  // the request's own timeout (the start is still in the game's queue)
    REQUIRE(c.calls == 1);
    CHECK(c.last.code == "timeout");
    CHECK(m.armed());  // the start can still come: the guard stays
    m.on_menu_state(g.pg(), 1);  // the game dispatches our start command in the main menu: not a way back
    CHECK(m.armed());
    m.on_menu_state(g.pg(), 2);
    m.on_other_state(g.pg(), 3, 0);  // Start Play
    g.set16(g.save2, L::kSelectedCharacter, 0x0101);  // a Steam character slipped in
    uint8_t r[16] = {};
    CHECK(m.guard(r, g.s2()));  // still refused after the timeout
    CHECK(r[0] == L::kNoCharacterSelected);
    m.on_other_state(g.pg(), 3, 2);
    m.on_menu_state(g.pg(), 0);  // back in the main menu
    CHECK_FALSE(m.armed());
    uint8_t after[16] = {};
    CHECK_FALSE(m.guard(after, g.s2()));
}

TEST_CASE_TEMPLATE("load_world guard: after a timeout the guard also turns off when the world runs, and at the latest 60 s after the start", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    Capture c;
    start_with_timeout(g, m, c, 5000);
    g.now += 5001;
    m.poll();
    REQUIRE(c.calls == 1);
    REQUIRE(m.armed());
    g.server_running = g.player = true;  // the start went through after all
    m.poll();
    CHECK_FALSE(m.armed());
    FakeGame g2(Build::app);
    standard_saves(g2);
    MenuStart m2(g2.deps());
    m2.on_menu_state(g2.pg(), 0);
    Capture c2;
    start_with_timeout(g2, m2, c2, 5000);
    const uint64_t started = g2.now;
    g2.now += 5001;
    m2.on_ui_frame(kUi);  // the timeout through the UI frame
    REQUIRE(c2.calls == 1);
    CHECK(m2.armed());
    g2.now = started + MenuStart::kGuardLingerMs - 1;
    m2.on_ui_frame(kUi);
    CHECK(m2.armed());
    g2.now = started + MenuStart::kGuardLingerMs + 1;  // the hard cap
    m2.poll();
    CHECK_FALSE(m2.armed());
}

TEST_CASE_TEMPLATE("load_world guard: the fake game's Start Play runs the guard; the user's Steam select queued with our start is refused", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    g.menu = &m;
    m.on_menu_state(g.pg(), 0);
    Capture c;
    LoadWorldResult no;
    REQUIRE(m.request(req(1, 1, "Character 2"), c.done(), &no));
    m.on_ui_frame(kUi);
    g.dispatch();
    m.on_ui_frame(kUi);  // frame B: offline + start
    REQUIRE(g.posts.size() == 4);
    // the user clicked their Steam character in the same frame: its select lands in the queue before our start
    g.posts.insert(g.posts.begin() + 3, FakeGame::Post{0x11, 0x0101, std::this_thread::get_id(), kUi});
    g.dispatch();
    CHECK(g.refused_starts == 1);  // "No Character selected": the game stays in the menu
    CHECK_FALSE(g.started);
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK(c.last.code == "refused");
    CHECK(c.last.error.find("guard") != std::string::npos);
    CHECK_FALSE(m.armed());
}

TEST_CASE_TEMPLATE("load_world guard: test_refuse makes the guard refuse even the LOCAL pair (the live check of the refusal path)", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    g.menu = &m;
    m.on_menu_state(g.pg(), 0);
    Capture c;
    LoadWorldResult no;
    LoadWorldRequest r = req(1, 1, "Character 2");
    r.test_refuse = true;
    REQUIRE(m.request(r, c.done(), &no));
    m.on_ui_frame(kUi);
    g.dispatch();
    m.on_ui_frame(kUi);
    g.dispatch();
    CHECK(g.refused_starts == 1);
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK(c.last.code == "refused");
    CHECK_FALSE(m.armed());
    // a normal load right after works
    REQUIRE(m.request(req(1, 1, "Character 2"), c.done(), &no));
    m.on_ui_frame(kUi);
    g.dispatch();
    m.on_ui_frame(kUi);
    g.dispatch();
    CHECK(g.started);
    g.server_running = g.player = true;
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 2);
    CHECK(c.last.ok);
}

TEST_CASE_TEMPLATE("load_world: the game app is kept for the process; the pregame is forgotten when Start Play begins", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    const uintptr_t app = reinterpret_cast<uintptr_t>(g.app.data());
    CHECK(m.game_app() == app);
    m.on_other_state(g.pg(), 3, 0);  // Start Play: the pregame object is destroyed once it succeeds
    std::memset(g.pregame, 0xEE, sizeof g.pregame);  // freed memory
    CHECK(m.game_app() == app);
    g.put_name(g.app, g.lay.session_name, "My World 1");
    CHECK(m.session_world_name() == std::optional<std::string>("My World 1"));
    // a request now waits for the main menu (it does not read the freed pregame)
    Capture c;
    LoadWorldResult no;
    REQUIRE(m.request(req(), c.done(), &no));
    m.on_ui_frame(kUi);
    CHECK(c.calls == 0);
    CHECK(g.posts.empty());
}

TEST_CASE_TEMPLATE("load_world: frame B posts the start only in the main menu, in Lobby mode, with no world and an empty command queue", Build, On1273043, On1283531) {
    auto at_frame_b = [](FakeGame& g, MenuStart& m, Capture& c) {
        LoadWorldResult no;
        REQUIRE(m.request(req(), c.done(), &no));
        m.on_ui_frame(kUi);
        g.dispatch();
    };
    {
        FakeGame g(Build::app);
        standard_saves(g);
        MenuStart m(g.deps());
        m.on_menu_state(g.pg(), 0);
        Capture c;
        at_frame_b(g, m, c);
        g.foreign_queued = 1;  // the menu's own click waits in the queue
        m.on_ui_frame(kUi);
        CHECK(g.posts.size() == 2);  // waits
        g.foreign_queued = 0;
        m.on_ui_frame(kUi);
        CHECK(g.posts.size() == 4);
    }
    {
        FakeGame g(Build::app);
        standard_saves(g);
        MenuStart m(g.deps());
        m.on_menu_state(g.pg(), 0);
        Capture c;
        at_frame_b(g, m, c);
        m.on_menu_state(g.pg(), 2);  // the menu was left meanwhile
        m.on_ui_frame(kUi);
        REQUIRE(c.calls == 1);
        CHECK(c.last.code == "unavailable");
        CHECK(g.posts.size() == 2);
    }
    {
        FakeGame g(Build::app);
        standard_saves(g);
        MenuStart m(g.deps());
        m.on_menu_state(g.pg(), 0);
        Capture c;
        at_frame_b(g, m, c);
        const uint32_t mode = 2;  // Host_Online
        std::memcpy(g.app.data() + g.lay.mode, &mode, 4);
        m.on_ui_frame(kUi);
        REQUIRE(c.calls == 1);
        CHECK(c.last.error.find("mode") != std::string::npos);
        CHECK(g.posts.size() == 2);
    }
    {
        FakeGame g(Build::app);
        standard_saves(g);
        MenuStart m(g.deps());
        m.on_menu_state(g.pg(), 0);
        Capture c;
        at_frame_b(g, m, c);
        g.any_running = true;
        m.on_ui_frame(kUi);
        REQUIRE(c.calls == 1);
        CHECK(c.last.error.find("a world is running") != std::string::npos);
        CHECK(g.posts.size() == 2);
    }
}

TEST_CASE_TEMPLATE("frame B: a selection with another world, another slot to load or an adventure never starts", Build, On1273043, On1283531) {
    for (int which = 0; which < 3; ++which) {
        CAPTURE(which);
        FakeGame g(Build::app);
        standard_saves(g);
        MenuStart m(g.deps());
        m.on_menu_state(g.pg(), 0);
        Capture c;
        LoadWorldResult no;
        REQUIRE(m.request(req(), c.done(), &no));
        m.on_ui_frame(kUi);
        g.dispatch();
        if (which == 0) g.set16(g.save2, L::kSelectedWorld, 0x0002);
        if (which == 1) g.set16(g.app, g.lay.play_slot, 0x0002);
        if (which == 2) g.app[g.lay.adventure] = 1;
        m.on_ui_frame(kUi);
        CHECK(g.posts.size() == 2);
        g.now += MenuStart::kSelectMs + 1;
        m.on_ui_frame(kUi);
        REQUIRE(c.calls == 1);
        CHECK(c.last.error.find("did not take the selection") != std::string::npos);
        CHECK(g.posts.size() == 2);
        CHECK_FALSE(m.armed());
    }
}

TEST_CASE_TEMPLATE("frame A: a session mode other than Lobby waits, and the timeout names it", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    const uint32_t mode = 1;  // Host_Offline
    std::memcpy(g.app.data() + g.lay.mode, &mode, 4);
    MenuStart m(g.deps());
    m.on_menu_state(g.pg(), 0);
    Capture c;
    LoadWorldResult no;
    REQUIRE(m.request(req(), c.done(), &no));
    m.on_ui_frame(kUi);
    CHECK(g.posts.empty());
    g.now += 60001;
    m.on_ui_frame(kUi);
    REQUIRE(c.calls == 1);
    CHECK(c.last.code == "timeout");
    CHECK(c.last.error.find("session mode") != std::string::npos);
}

TEST_CASE_TEMPLATE("character: the selection the pregame state hook last saw; unknown before and when it cannot be read", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    CharacterSelection c = m.character();
    CHECK(c.storage == "unknown");  // nothing seen yet
    CHECK_FALSE(c.slot.has_value());
    CHECK_FALSE(c.name.has_value());
    g.set16(g.save2, L::kSelectedCharacter, 0x0001);  // LOCAL character slot 1
    g.save2[L::kSelectedCharacterValid] = 1;
    m.note_character(g.pg());
    c = m.character();
    CHECK(c.storage == "local");
    REQUIRE(c.slot.has_value());
    CHECK(*c.slot == 1);
    REQUIRE(c.name.has_value());
    CHECK(*c.name == "Character 2");
    m.note_character(nullptr);  // a world runs: no pregame, the snapshot stays
    CHECK(m.character().storage == "local");
    g.set16(g.save2, L::kSelectedCharacter, 0x0101);  // the user's Steam character, slot 1
    m.note_character(g.pg());
    c = m.character();
    CHECK(c.storage == "steam");
    REQUIRE(c.slot.has_value());
    CHECK(*c.slot == 1);
    REQUIRE(c.name.has_value());
    CHECK(*c.name == "Steam Hero");  // the name from the Steam table, not the LOCAL one
}

TEST_CASE_TEMPLATE("character: no valid selection, a storage or slot the layout does not know, or unreadable memory: unknown", Build, On1273043, On1283531) {
    FakeGame g(Build::app);
    standard_saves(g);
    MenuStart m(g.deps());
    auto seen = [&](uint16_t slot, uint8_t valid) {
        g.set16(g.save2, L::kSelectedCharacter, slot);
        g.save2[L::kSelectedCharacterValid] = valid;
        m.note_character(g.pg());
        return m.character();
    };
    CHECK(seen(0x0001, 1).storage == "local");
    CHECK(seen(0x0001, 0).storage == "unknown");  // not selected (the valid byte)
    CHECK(seen(0x0201, 1).storage == "unknown");  // storage 2: not a storage the game has
    CHECK(seen(0x0000, 1).storage == "unknown");  // no slot
    CHECK(seen(0x0011, 1).storage == "unknown");  // slot 17: there are 16
    CharacterSelection c = seen(0x0002, 1);       // LOCAL slot 2 exists in the selection but has no record name
    CHECK(c.storage == "local");
    REQUIRE(c.slot.has_value());
    CHECK(*c.slot == 2);
    CHECK_FALSE(c.name.has_value());
    // memory that cannot be read: unknown, never the last value
    seen(0x0001, 1);
    MenuStart::Deps d = g.deps();
    const uintptr_t sel = g.s2() + L::kSelectedCharacter;
    d.read = [sel](uintptr_t a, void* out, size_t n) {
        if (a == sel) return false;
        std::memcpy(out, reinterpret_cast<const void*>(a), n);
        return true;
    };
    MenuStart m2(d);
    m2.note_character(g.pg());
    CHECK(m2.character().storage == "unknown");
}
