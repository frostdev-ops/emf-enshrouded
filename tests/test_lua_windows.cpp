// Lua 1.13: mod windows (API v24) from Lua: emf.window, emf.window_update, emf.window_close; the rows reach the core
// in order with their kinds and flags, events reach fn, a reload closes the script's window.
#include <doctest.h>

#include <string>
#include <vector>

#include "lua_host/host.h"
#include "lua_test_fake.h"

using emf::lua::Host;

namespace {
EmfUiEvent ui_event(uint32_t kind, const char* window, const char* key, uint32_t reason = 0, uint32_t flags = 0) {
    EmfUiEvent e{};
    e.size = sizeof e;
    e.kind = kind;
    e.window = window;
    e.key = key;
    e.from_key = "";
    e.reason = reason;
    e.flags = flags;
    return e;
}
const char* kStable =
    "emf.command('go', '', function(w)\n"
    "  emf.reply(tostring(emf.window('stable', {title = 'Beasts', width = 30, place = 'right', close_key = 'F3',\n"
    "    world = 'client', rows = {\n"
    "      {heading = 'Active 1/3'},\n"
    "      {slot = 'active.1', text = 'Wolf 2', count = 3, tooltip = 'Level 12'},\n"
    "      {slot = 'active.2', text = 'Slot 2', empty = true, same_line = true},\n"
    "      {separator = true},\n"
    "      {row = 'beast.Boar', text = 'Boar (out)', selected = true},\n"
    "      {button = 'call', text = 'Call'},\n"
    "      {button = 'release', text = 'Release', same_line = true, disabled = true},\n"
    "      {bar = 'Wolf 2', value = 0.8},\n"
    "      {text = 'hello', color = '#112233'},\n"
    "    }}, function(w2, ev)\n"
    "      emf.log('ev ' .. ev.kind .. ' ' .. tostring(ev.key) .. ' ' .. tostring(ev.reason) .. ' ' .. tostring(w2 ~= nil))\n"
    "    end)))\n"
    "end)\n";
}  // namespace

TEST_CASE("lua 1.13: emf.window passes the rows in order with their kinds, flags and the mod's id; fn gets the events") {
    Rig r(L"luawin");
    with_windows(r.api);
    add_script(r.dir, "wn", kStable);
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("wn.go");
    REQUIRE(r.fake.replies.size() == 1);
    CHECK(r.fake.replies[0] == "true");
    REQUIRE(r.fake.windows.size() == 1u);
    const Fake::Win& w = r.fake.windows[0];
    CHECK(w.id == "wn.stable");  // the host puts the mod's id in front
    CHECK(w.title == "Beasts");
    CHECK(w.width == 30.0f);
    CHECK(w.place == EMF_UI_PLACE_RIGHT);
    CHECK(w.close_key == 0x72u);  // F3
    CHECK(w.kind == EMF_WORLD_CLIENT);
    REQUIRE(w.widgets.size() == 9u);
    const uint32_t types[] = {EMF_UI_HEADING, EMF_UI_SLOT,   EMF_UI_SLOT, EMF_UI_SEPARATOR, EMF_UI_ROW,
                              EMF_UI_BUTTON,  EMF_UI_BUTTON, EMF_UI_BAR,  EMF_UI_TEXT};
    for (size_t i = 0; i < 9; ++i) CHECK(w.widgets[i].type == types[i]);
    CHECK(w.texts[0] == "Active 1/3");
    CHECK(w.keys[1] == "active.1");
    CHECK(w.widgets[1].count == 3u);
    CHECK(w.tips[1] == "Level 12");
    CHECK(w.widgets[2].flags == (EMF_UI_SAME_LINE | EMF_UI_EMPTY));
    CHECK(w.widgets[4].flags == EMF_UI_SELECTED);
    CHECK(w.texts[4] == "Boar (out)");
    CHECK(w.keys[5] == "call");
    CHECK(w.widgets[6].flags == (EMF_UI_SAME_LINE | EMF_UI_DISABLED));
    CHECK(w.texts[7] == "Wolf 2");
    CHECK(w.widgets[7].value == 800u);
    CHECK(w.widgets[8].rgba == 0x112233FFu);
    // the core's events, one step later
    EmfUiEvent click = ui_event(EMF_UI_CLICK, "wn.stable", "call");
    w.cb(kClient, &click, w.user);
    CHECK(logged("ev click call nil true"));
    EmfUiEvent closed = ui_event(EMF_UI_CLOSED, "wn.stable", "", EMF_UI_BY_PLAYER);
    w.cb(kClient, &closed, w.user);
    CHECK(logged("ev closed  player true"));
}

TEST_CASE("lua 1.13: window_update and window_close take the short id; bad rows are errors; false on a v23 core") {
    Rig r(L"luawin2");
    with_windows(r.api);
    add_script(r.dir, "wu",
               "local function j(...) local t = {} for i = 1, select('#', ...) do t[#t + 1] = tostring((select(i, ...))) end return table.concat(t, ' ') end\n"
               "emf.command('go', '', function(w)\n"
               "  emf.reply(j(emf.window('list', {rows = {{text = 'one'}}}, function() end)))\n"
               "  emf.reply(j(emf.window_update('list', {title = 'New', rows = {{text = 'two'}}})))\n"
               "  emf.reply(j(emf.window_close('list')))\n"
               "  emf.reply(j(pcall(emf.window, 'bad', {rows = {{nosuch = 1}}}, function() end)))\n"
               "  emf.reply(j(pcall(emf.window, 'bad', {rows = {{button = 'b', text = 't', place = 1}}}, function() end)))\n"
               "  emf.reply(j(pcall(emf.window, 'bad', {close_key = 'NoSuchKey', rows = {}}, function() end)))\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("wu.go");
    REQUIRE(r.fake.replies.size() == 6);
    CHECK(r.fake.replies[0] == "true");
    CHECK(r.fake.replies[1] == "true");
    CHECK(r.fake.replies[2] == "true");
    CHECK(r.fake.replies[3].find("false") == 0);  // an unknown row kind
    CHECK(r.fake.replies[3].find("nosuch") != std::string::npos);
    CHECK(r.fake.replies[4].find("false") == 0);  // an unknown field in a row
    CHECK(r.fake.replies[5].find("false") == 0);  // not a key
    REQUIRE(r.fake.windows.size() == 1u);
    CHECK(r.fake.windows[0].kind == EMF_WORLD_CLIENT);  // no hosted world in the fake: the client world
    REQUIRE(r.fake.window_updates.size() == 1u);
    CHECK(r.fake.window_updates[0].id == "wu.list");
    CHECK(r.fake.window_updates[0].title == "New");
    CHECK(r.fake.window_updates[0].texts[0] == "two");
    REQUIRE(r.fake.windows_closed.size() == 1u);
    CHECK(r.fake.windows_closed[0] == "wu.list");

    Rig old(L"luawin3");  // a v23 core
    add_script(old.dir, "wo", "emf.command('go', '', function(w) emf.reply(tostring(emf.window('x', {rows = {}}, function() end))) end)\n");
    Host h2(&old.api, old.dir);
    h2.load_all();
    old.run_command("wo.go");
    REQUIRE(old.fake.replies.size() == 1);
    CHECK(old.fake.replies[0] == "false");
}

TEST_CASE("lua 1.13: an empty close_key (a key setting left unset) is no close key, not an error") {
    Rig r(L"luawin5");
    with_windows(r.api);
    add_script(r.dir, "we", "emf.command('go', '', function(w) emf.reply(tostring(emf.window('x', {close_key = '', rows = {}}, function() end))) end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("we.go");
    REQUIRE(r.fake.replies.size() == 1);
    CHECK(r.fake.replies[0] == "true");
    REQUIRE(r.fake.windows.size() == 1u);
    CHECK(r.fake.windows[0].close_key == 0u);
    CHECK(host.status("we") == "running");
}

TEST_CASE("lua 1.13: a reload closes the script's open window; its late events reach nobody") {
    Rig r(L"luawin4");
    with_windows(r.api);
    add_script(r.dir, "wr",
               "emf.command('go', '', function(w)\n"
               "  emf.window('w', {rows = {{button = 'b', text = 'B'}}}, function(w2, ev) emf.log('old ' .. ev.kind) end)\n"
               "end)\n");
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("wr.go");
    REQUIRE(r.fake.windows.size() == 1u);
    Sleep(20);
    write(r.dir + L"\\wr\\main.lua", "emf.log('reloaded')\n");
    host.check_reload_now();
    REQUIRE(host.status("wr") == "running");
    REQUIRE(r.fake.windows_closed.size() == 1u);
    CHECK(r.fake.windows_closed[0] == "wr.w");
    const Fake::Win& w = r.fake.windows[0];
    EmfUiEvent click = ui_event(EMF_UI_CLICK, "wr.w", "b");
    w.cb(kClient, &click, w.user);
    EmfUiEvent closed = ui_event(EMF_UI_CLOSED, "wr.w", "", EMF_UI_BY_MOD);
    w.cb(kClient, &closed, w.user);  // the slot is freed here; the old script is not asked
    CHECK_FALSE(logged("old click"));
    CHECK_FALSE(logged("old closed"));
}

namespace {
const char* kPictures =
    "emf.command('img', '', function(w, args)\n"
    "  local ok, h = pcall(emf.image, args[1])\n"
    "  emf.reply(ok and tostring(h) or ('error ' .. tostring(h)))\n"
    "end)\n"
    "emf.command('show', '', function(w, args)\n"
    "  local h = emf.image('pics/wolf.png')\n"
    "  emf.reply(tostring(emf.window('pics', {rows = {\n"
    "    {image = h, height = 8, fps = 12, key = 'portrait', tooltip = 'Wolf'},\n"
    "    {slot = 'active.1', text = 'Wolf', icon = h, fps = 6},\n"
    "    {row = 'beast.Wolf', text = 'Wolf (ready)', icon = h},\n"
    "  }}, function() end)))\n"
    "end)\n";
}  // namespace

TEST_CASE("lua 1.15: emf.image reads the script's own folder or the portrait cache; other paths are refused") {
    Rig r(L"luaimg");
    with_windows(r.api);
    with_images(r.api);
    add_script(r.dir, "wi", kPictures);
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("wi.img pics/wolf.png");
    REQUIRE(r.fake.replies.size() == 1);
    CHECK(r.fake.replies[0] == "50");
    REQUIRE(r.fake.images_loaded.size() == 1u);
    const std::string loaded = r.fake.images_loaded[0];
    CHECK(loaded.size() > 2);  // <mods>\wi\pics\wolf.png: absolute, in the mod's folder
    CHECK(loaded[1] == ':');
    const std::string tail = "\\wi\\pics\\wolf.png";
    CHECK(loaded.size() > tail.size());
    CHECK(loaded.compare(loaded.size() - tail.size(), tail.size(), tail) == 0);
    r.run_command("wi.img portrait:wolf_t1");
    CHECK(r.fake.replies[1] == "51");
    CHECK(r.fake.images_loaded[1] == "portrait:wolf_t1");
    for (const char* bad : {"../other/x.png", "pics/../../x.png", "C:/x.png", "/x.png", "\\\\server\\x.png"}) {
        r.fake.replies.clear();
        r.run_command(std::string("wi.img ") + bad);
        REQUIRE(r.fake.replies.size() == 1);
        CHECK_MESSAGE(r.fake.replies[0].rfind("error", 0) == 0, bad);
    }
    CHECK(r.fake.images_loaded.size() == 2u);  // refused paths never reach the core
    r.fake.image_ok = false;
    r.fake.replies.clear();
    r.run_command("wi.img pics/missing.png");
    CHECK(r.fake.replies[0] == "nil");
}

TEST_CASE("lua 1.15: rows with image, icon, fps and height become the v26 widgets; a v25 core gives nil") {
    Rig r(L"luaimg2");
    with_windows(r.api);
    with_images(r.api);
    add_script(r.dir, "wp", kPictures);
    Host host(&r.api, r.dir);
    host.load_all();
    r.run_command("wp.show");
    REQUIRE(r.fake.windows.size() == 1u);
    const auto& w = r.fake.windows[0].widgets;
    REQUIRE(w.size() == 3u);
    CHECK(w[0].type == EMF_UI_IMAGE);
    CHECK(w[0].icon == 50u);
    CHECK(w[0].height == 8.0f);
    CHECK(w[0].fps == 12u);
    CHECK(r.fake.windows[0].keys[0] == "portrait");
    CHECK(w[1].type == EMF_UI_SLOT);
    CHECK(w[1].icon == 50u);
    CHECK(w[1].fps == 6u);
    CHECK(w[2].type == EMF_UI_ROW);
    CHECK(w[2].icon == 50u);

    Rig old(L"luaimg3");
    with_windows(old.api);  // a v25 core: no image_load
    add_script(old.dir, "wo", kPictures);
    Host h2(&old.api, old.dir);
    h2.load_all();
    old.run_command("wo.img pics/wolf.png");
    REQUIRE(old.fake.replies.size() == 1);
    CHECK(old.fake.replies[0] == "nil");
}
