// Mod windows through the C API (v24): the table, the checks, the hand-over to the overlay, and the events delivered
// through the dispatcher after the next step of the asked world (CLOSED at once with no world running).
#include <windows.h>

#include <doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "common/offsets.h"
#include "core/api.h"
#include "core/api_sizes.h"
#include "core/window_request.h"
#include "fake_world.h"

using namespace emf;
using namespace emf::core;
using emf::test::FakeWorld;

namespace {
uint64_t g_now = 500000;
std::vector<std::string> g_lines;
struct Seen {
    EmfWorld* world;
    uint32_t kind, reason, flags;
    std::string window, key;
    void* user;
};
std::vector<Seen> g_seen;
void record(EmfWorld* w, const EmfUiEvent* e, void* user) {
    g_seen.push_back({w, e->kind, e->reason, e->flags, e->window, e->key, user});
}
void faulting(EmfWorld*, const EmfUiEvent*, void*) {
    volatile int* p = nullptr;
    *p = 1;
}

struct Fixture {
    CoreState state;
    FakeWorld server;
    std::vector<WindowRequest> opened, updated;
    std::optional<WindowRequest> open_now;  // what the fake overlay shows
    bool overlay_says_yes = true;
    Fixture(bool world = true, bool overlay = true) {
        g_lines.clear();
        g_seen.clear();
        state.log_line = [](const std::string& s) { g_lines.push_back(s); };
        state.now_ms = [] { return g_now; };
        server.put<uint8_t>(off::kKind, EMF_WORLD_SERVER);
        if (world) state.tracker.on_tick(w(), EMF_WORLD_SERVER, g_now);
        if (overlay) {
            state.open_window = [this](WindowRequest r, std::optional<WindowRequest>* replaced, std::string* why) {
                if (!overlay_says_yes) {
                    *why = "another mod's window is open";
                    return false;
                }
                if (open_now && open_now->id == r.id) *replaced = open_now;
                opened.push_back(r);
                open_now = r;
                return true;
            };
            state.update_window = [this](const WindowRequest& r) {
                if (!open_now || open_now->id != r.id) return false;
                updated.push_back(r);
                return true;
            };
            state.close_window = [this](const std::string& id, std::optional<WindowRequest>* closed) {
                if (!open_now || open_now->id != id) return false;
                *closed = open_now;
                open_now.reset();
                return true;
            };
        }
        set_core_state(&state);
    }
    ~Fixture() { set_core_state(nullptr); }
    EmfWorld* w() { return reinterpret_cast<EmfWorld*>(server.p()); }
    void step() {
        state.dispatcher.run(w(), EMF_WORLD_SERVER, [](const std::string& s) { g_lines.push_back(s); });
    }
};
bool logged(const std::string& needle) {
    for (const auto& l : g_lines)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}

struct Def {
    EmfUiWidget widgets[2]{};
    EmfWindowDef def{};
    Def(const char* id = "test.stable", EmfUiCallback cb = record) {
        widgets[0].type = EMF_UI_BUTTON;
        widgets[0].key = "call";
        widgets[0].text = "Call";
        widgets[1].type = EMF_UI_TEXT;
        widgets[1].text = "Wolf 2 rests";
        def.size = sizeof def;
        def.id = id;
        def.title = "Beasts";
        def.widgets = widgets;
        def.count = 2;
        def.widget_size = sizeof(EmfUiWidget);
        def.world_kind = EMF_WORLD_SERVER;
        def.cb = cb;
        def.user = this;
    }
};
}  // namespace

TEST_CASE("api v24: the table has window_open, window_update and window_close after change_component") {
    const EmfApi* api = api_table();
    CHECK(api->version >= 24u);
    CHECK(offsetof(EmfApi, window_open) == kApiV23Size);
    CHECK(api->size >= kApiV24Size);
    CHECK(api->window_open != nullptr);
    CHECK(api->window_update != nullptr);
    CHECK(api->window_close != nullptr);
}

TEST_CASE("window_open: a good window reaches the overlay with the caller's module; refused and logged otherwise") {
    {
        Fixture f;
        Def d;
        CHECK(api_table()->window_open(&d.def));
        REQUIRE(f.opened.size() == 1u);
        CHECK(f.opened[0].id == "test.stable");
        CHECK(f.opened[0].widgets.size() == 2u);
        CHECK_FALSE(f.opened[0].mod.empty());  // the caller's module (this test exe)
        Def bad("test stable");
        CHECK_FALSE(api_table()->window_open(&bad.def));
        CHECK(logged("id"));
        f.overlay_says_yes = false;
        Def other("test.other");
        CHECK_FALSE(api_table()->window_open(&other.def));
        CHECK(logged("another mod's window is open"));
    }
    {
        Fixture f(true, false);  // no overlay (or a dedicated server)
        Def d;
        CHECK_FALSE(api_table()->window_open(&d.def));
        CHECK(logged("overlay"));
    }
    {
        Fixture f(false);  // the main menu: no world runs
        Def d;
        CHECK_FALSE(api_table()->window_open(&d.def));
        CHECK(logged("no server world"));
        CHECK(f.opened.empty());
    }
}

TEST_CASE("window events: a click and CLOSED reach the callback after the next step of the asked world") {
    Fixture f;
    Def d;
    REQUIRE(api_table()->window_open(&d.def));
    post_window_event(*f.open_now, EMF_UI_CLICK, "call", 0);
    CHECK(g_seen.empty());  // not before the world's step
    f.step();
    REQUIRE(g_seen.size() == 1u);
    CHECK(g_seen[0].kind == EMF_UI_CLICK);
    CHECK(g_seen[0].window == "test.stable");
    CHECK(g_seen[0].key == "call");
    CHECK(g_seen[0].world == f.w());
    CHECK(g_seen[0].user == &d);
    CHECK(api_table()->window_close("test.stable"));
    CHECK_FALSE(api_table()->window_close("test.stable"));  // not open any more
    f.step();
    REQUIRE(g_seen.size() == 2u);
    CHECK(g_seen[1].kind == EMF_UI_CLOSED);
    CHECK(g_seen[1].reason == EMF_UI_BY_MOD);
    CHECK(g_seen[1].key.empty());
    CHECK(g_seen[1].flags == 0u);
}

TEST_CASE("window_open with the open window's id: the replaced window's callback gets CLOSED (by the mod)") {
    Fixture f;
    Def first;
    REQUIRE(api_table()->window_open(&first.def));
    Def second;
    REQUIRE(api_table()->window_open(&second.def));
    f.step();
    REQUIRE(g_seen.size() == 1u);
    CHECK(g_seen[0].kind == EMF_UI_CLOSED);
    CHECK(g_seen[0].reason == EMF_UI_BY_MOD);
    CHECK(g_seen[0].user == &first);
}

TEST_CASE("window events: CLOSED with no world running comes at once with EMF_UI_NO_WORLD; a click is dropped") {
    Fixture f(false);
    WindowRequest r;
    r.id = "test.stable";
    r.world_kind = EMF_WORLD_SERVER;
    r.cb = record;
    post_window_event(r, EMF_UI_CLICK, "call", 0);
    CHECK(g_seen.empty());
    post_window_event(r, EMF_UI_CLOSED, "", EMF_UI_BY_CORE);
    REQUIRE(g_seen.size() == 1u);
    CHECK(g_seen[0].kind == EMF_UI_CLOSED);
    CHECK(g_seen[0].world == nullptr);
    CHECK(g_seen[0].flags == EMF_UI_NO_WORLD);
    CHECK(g_seen[0].reason == EMF_UI_BY_CORE);
}

TEST_CASE("window events: a callback that faults is caught and logged; the next event still comes") {
    Fixture f;
    WindowRequest bad;
    bad.id = "test.bad";
    bad.cb = faulting;
    WindowRequest good = bad;
    good.cb = record;
    post_window_event(bad, EMF_UI_CLOSED, "", EMF_UI_BY_MOD);
    post_window_event(good, EMF_UI_CLOSED, "", EMF_UI_BY_MOD);
    f.step();
    CHECK(logged("faulted"));
    CHECK(g_seen.size() == 1u);
}

TEST_CASE("window_update: new contents for the open window; false when it is not open, without a log line") {
    Fixture f;
    Def d;
    CHECK_FALSE(api_table()->window_update(&d.def));
    REQUIRE(api_table()->window_open(&d.def));
    d.widgets[1].text = "Wolf 2 is ready";
    d.def.cb = nullptr;  // an update needs none
    CHECK(api_table()->window_update(&d.def));
    REQUIRE(f.updated.size() == 1u);
    CHECK(f.updated[0].widgets[1].text == "Wolf 2 is ready");
    const size_t lines = g_lines.size();
    Def other("test.other");
    CHECK_FALSE(api_table()->window_update(&other.def));
    CHECK(g_lines.size() == lines);
}

TEST_CASE("window events: a click for a window that closed is dropped, never delivered after its CLOSED") {
    Fixture f;
    WindowRequest r;
    r.id = "test.stable";
    r.world_kind = EMF_WORLD_SERVER;
    r.cb = record;
    r.open = std::make_shared<std::atomic<bool>>(true);
    post_window_event(r, EMF_UI_CLICK, "call", 0);  // queued for the next step
    r.open->store(false);                           // then the gate hands the window back...
    post_window_event(r, EMF_UI_CLOSED, "", EMF_UI_BY_MOD);  // ...and its CLOSED follows
    post_window_event(r, EMF_UI_CLICK, "call", 0);  // a click from a stale copy
    f.step();
    REQUIRE(g_seen.size() == 1u);
    CHECK(g_seen[0].kind == EMF_UI_CLOSED);
}

TEST_CASE("window_open: refused when no world of the asked kind runs (its events would never come)") {
    Fixture f;  // only a server world runs
    Def d;
    d.def.world_kind = EMF_WORLD_CLIENT;
    CHECK_FALSE(api_table()->window_open(&d.def));
    CHECK(logged("client"));
    CHECK(f.opened.empty());
    d.def.world_kind = EMF_WORLD_SERVER;
    CHECK(api_table()->window_open(&d.def));
}
