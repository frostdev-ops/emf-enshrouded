// The console UI drawn headless (no renderer): an ImGui context, a built font atlas, frames run by hand.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/overlay/console_model.h"
#include "core/overlay/console_ui.h"
#include "imgui.h"
#include "imgui_internal.h"

using namespace emf::core;

namespace {
struct Headless {
    ImGuiContext* ctx;
    Headless() {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1920, 1080);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    }
    ~Headless() { ImGui::DestroyContext(ctx); }
};

struct Rig {
    ConsoleModel model;
    ConsoleUiState ui;
    CompletionSources src;
    std::vector<std::string> submitted;
    ImVec2 display{1920, 1080};
    Rig() {
        src.commands = {"give", "help", "items", "time"};
        src.items = {"Material_T1_Wood_Logs"};
        ui.open_fraction = 1.0f;
    }
    void frame() {
        ImGui::NewFrame();
        draw_console(model, ui, src, display, [this](const std::string& s) { submitted.push_back(s); });
        ImGui::Render();
    }
    void type(const char* text) {
        for (const char* p = text; *p; ++p) ImGui::GetIO().AddInputCharacter(static_cast<unsigned>(*p));
        frame();
    }
    void press(ImGuiKey k) {
        ImGui::GetIO().AddKeyEvent(k, true);
        frame();
        ImGui::GetIO().AddKeyEvent(k, false);
        frame();
    }
};
// The scrollback child window (ImGui names children "<parent>/<name>_<id>").
ImGuiWindow* scrollback() {
    for (ImGuiWindow* w : GImGui->Windows)
        if (std::string(w->Name).find("##scrollback") != std::string::npos) return w;
    return nullptr;
}
// A window drawn in the last frame.
bool drawn(const char* name) {
    for (ImGuiWindow* w : GImGui->Windows)
        if (std::string(w->Name).find(name) != std::string::npos && w->Active) return true;
    return false;
}
// Commands with help, as CommandCenter::list() gives them.
void with_help(Rig& r) {
    r.src.commands = {"give", "help", "loot.status", "loot.toggle", "lua.reload", "time"};
    r.src.command_help = {"<item name> [count]: drop items at your feet (host)", "[page]: list the commands",
                          ": is your Loot Magnet on, and its rules", "[on|off]: your Loot Magnet",
                          ": reload the Lua mods whose files changed", "<hours 0-24>: set the time of day (host)"};
    r.src.version = 1;
}
}  // namespace

TEST_CASE("console UI: after the window grows and shrinks again, the scrollback still shows the newest line") {
    Headless h;
    Rig r;
    for (int i = 0; i < 60; ++i) r.model.add_line("line " + std::to_string(i));
    for (int i = 0; i < 4; ++i) r.frame();
    ImGuiWindow* w = scrollback();
    REQUIRE(w != nullptr);
    REQUIRE(w->ScrollMax.y > 0.0f);
    CHECK(w->Scroll.y == doctest::Approx(w->ScrollMax.y));  // follows the newest line
    r.display = ImVec2(1920, 6000);  // maximized on a tall screen: every line fits
    for (int i = 0; i < 4; ++i) r.frame();
    CHECK(w->ScrollMax.y == 0.0f);
    r.display = ImVec2(1920, 1080);  // restored
    for (int i = 0; i < 4; ++i) r.frame();
    REQUIRE(w->ScrollMax.y > 0.0f);
    CHECK(w->Scroll.y == doctest::Approx(w->ScrollMax.y));
}

TEST_CASE("console UI: Enter submits the line, clears the input and remembers it for Up") {
    Headless h;
    Rig r;
    r.frame();  // first frame focuses the input
    r.frame();
    r.type("help");
    CHECK(r.ui.input == "help");
    r.press(ImGuiKey_Enter);
    REQUIRE(r.submitted.size() == 1);
    CHECK(r.submitted[0] == "help");
    CHECK(r.ui.input.empty());
    r.press(ImGuiKey_UpArrow);
    CHECK(r.ui.input == "help");
}

TEST_CASE("console UI: Tab completes a command and then an item name") {
    Headless h;
    Rig r;
    r.frame();
    r.frame();
    r.type("gi");
    r.press(ImGuiKey_Tab);
    CHECK(r.ui.input == "give ");
    r.type("mat");
    r.press(ImGuiKey_Tab);
    CHECK(r.ui.input == "give Material_T1_Wood_Logs ");
}

TEST_CASE("console UI: suggestions show as you type, with ghost text; Tab takes the top one, then the next argument's") {
    Headless h;
    Rig r;
    with_help(r);
    r.frame();
    r.frame();
    r.type("lo");
    CHECK(r.ui.suggester.showing());
    CHECK(r.ui.suggester.ghost() == "ot.status");
    CHECK(drawn("##emf_suggest"));
    CHECK(r.ui.wants_escape);  // the next Esc hides the list, it does not close the console
    r.press(ImGuiKey_Tab);
    CHECK(r.ui.input == "loot.status ");
    CHECK(r.ui.caret == 12);
    CHECK(drawn("##emf_suggest"));  // the hint line "loot.status: is your Loot Magnet on..."
    CHECK_FALSE(r.ui.suggester.showing());
    CHECK_FALSE(r.ui.wants_escape);
    r.press(ImGuiKey_Backspace);
    for (int i = 0; i < 6; ++i) r.press(ImGuiKey_Backspace);  // "loot."
    r.type("t");
    r.press(ImGuiKey_Tab);
    CHECK(r.ui.input == "loot.toggle ");
    REQUIRE(r.ui.suggester.showing());  // [on|off]
    r.press(ImGuiKey_DownArrow);
    CHECK(r.ui.suggester.selected() == 1);
    r.press(ImGuiKey_RightArrow);
    CHECK(r.ui.input == "loot.toggle off ");
    r.type("x");
    r.press(ImGuiKey_Enter);
    REQUIRE(r.submitted.size() == 1);
    CHECK(r.submitted[0] == "loot.toggle off x");
    CHECK(r.ui.suggester.current().rows.empty());
    CHECK(r.ui.suggester.current().hint.empty());
}

TEST_CASE("console UI: Right takes the suggestion only at the end of the line; elsewhere it moves the caret") {
    Headless h;
    Rig r;
    with_help(r);
    r.frame();
    r.frame();
    r.type("lo");
    r.press(ImGuiKey_LeftArrow);
    CHECK(r.ui.caret == 1);
    r.press(ImGuiKey_RightArrow);
    CHECK(r.ui.input == "lo");
    CHECK(r.ui.caret == 2);
    r.press(ImGuiKey_RightArrow);
    CHECK(r.ui.input == "loot.status ");
}

TEST_CASE("console UI: Up/Down move in the list and leave the history alone; Esc hides it, then Up is the history's") {
    Headless h;
    Rig r;
    with_help(r);
    r.frame();
    r.frame();
    r.type("help");
    r.press(ImGuiKey_Enter);
    r.frame();  // the input line takes the keyboard again
    r.type("lo");
    REQUIRE(r.ui.suggester.showing());
    r.press(ImGuiKey_DownArrow);
    CHECK(r.ui.input == "lo");
    CHECK(r.ui.suggester.selected() == 1);
    CHECK(r.ui.suggester.ghost() == "ot.toggle");
    r.press(ImGuiKey_UpArrow);
    r.press(ImGuiKey_UpArrow);
    CHECK(r.ui.suggester.selected() == 2);  // wrapped to lua.reload
    CHECK(r.ui.input == "lo");
    r.ui.escape = true;  // Esc reached the console (the router sends it while the list shows)
    r.frame();
    CHECK_FALSE(r.ui.suggester.showing());
    CHECK_FALSE(r.ui.wants_escape);  // the next Esc closes the console
    CHECK_FALSE(drawn("##emf_suggest"));
    CHECK(r.ui.input == "lo");
    r.press(ImGuiKey_UpArrow);
    CHECK(r.ui.input == "help");
}

TEST_CASE("console UI: a line from the history shows no list, so Up and Down keep going through the history") {
    Headless h;
    Rig r;
    with_help(r);
    r.model.push_history("loot.toggle on");
    r.model.push_history("lo");
    r.frame();
    r.frame();
    r.press(ImGuiKey_UpArrow);
    CHECK(r.ui.input == "lo");
    CHECK_FALSE(r.ui.suggester.showing());
    r.press(ImGuiKey_UpArrow);
    CHECK(r.ui.input == "loot.toggle on");
    r.press(ImGuiKey_DownArrow);
    CHECK(r.ui.input == "lo");
    r.type("o");  // typing shows it again
    CHECK(r.ui.suggester.showing());
}

TEST_CASE("console UI: with suggestions off nothing shows as you type and Tab cycles through the matches as before") {
    Headless h;
    Rig r;
    with_help(r);
    r.ui.suggest = false;
    r.src.commands = {"time", "tpl", "tplset"};
    r.src.command_help.clear();
    r.frame();
    r.frame();
    r.type("t");
    CHECK_FALSE(r.ui.suggester.showing());
    CHECK_FALSE(drawn("##emf_suggest"));
    CHECK_FALSE(r.ui.wants_escape);
    r.press(ImGuiKey_Tab);
    CHECK(r.ui.input == "time");
    r.press(ImGuiKey_Tab);
    CHECK(r.ui.input == "tpl");
    r.press(ImGuiKey_Tab);
    CHECK(r.ui.input == "tplset");
}

TEST_CASE("console UI: while closed it takes no typing, and many lines draw without trouble") {
    Headless h;
    Rig r;
    r.ui.open_fraction = 0.0f;
    r.frame();
    r.type("w");
    CHECK(r.ui.input.empty());
    for (int i = 0; i < 2500; ++i) r.model.add_line(i % 7 ? "player 1 at (1.0, 2.0, 3.0) m" : "usage: give <item name> [count]");
    r.ui.open_fraction = 1.0f;
    for (int i = 0; i < 5; ++i) r.frame();
    CHECK(r.model.size() == 2000);
}

// ---- overlay glue: settings and key mapping ----
#include <windows.h>

#include "core/overlay/overlay.h"

TEST_CASE("read_overlay_config: on by default with the key under Esc; enabled=0 and key= are read") {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    const std::wstring ini = std::wstring(dir) + L"emf_test_overlay_" + std::to_wstring(GetCurrentProcessId()) + L".ini";
    DeleteFileW(ini.c_str());
    OverlayConfig c = read_overlay_config(ini);
    CHECK(c.enabled);
    CHECK(c.toggle_scan == 0x29);
    CHECK(c.chat_commands);
    WritePrivateProfileStringW(L"overlay", L"enabled", L"0", ini.c_str());
    WritePrivateProfileStringW(L"overlay", L"key", L"0x44", ini.c_str());
    WritePrivateProfileStringW(L"overlay", L"chat", L"0", ini.c_str());
    c = read_overlay_config(ini);
    CHECK_FALSE(c.enabled);
    CHECK(c.toggle_scan == 0x44);
    CHECK_FALSE(c.chat_commands);
    WritePrivateProfileStringW(L"overlay", L"key", L"999", ini.c_str());  // out of range: default
    CHECK(read_overlay_config(ini).toggle_scan == 0x29);
    DeleteFileW(ini.c_str());
}

TEST_CASE("read_overlay_config: suggestions as you type are on with 8 rows; suggest=0 turns them off; 3-12 rows") {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    const std::wstring ini = std::wstring(dir) + L"emf_test_suggest_" + std::to_wstring(GetCurrentProcessId()) + L".ini";
    DeleteFileW(ini.c_str());
    OverlayConfig c = read_overlay_config(ini);
    CHECK(c.suggest);
    CHECK(c.suggest_rows == 8);
    WritePrivateProfileStringW(L"overlay", L"suggest", L"0", ini.c_str());
    WritePrivateProfileStringW(L"overlay", L"suggest_rows", L"5", ini.c_str());
    c = read_overlay_config(ini);
    CHECK_FALSE(c.suggest);
    CHECK(c.suggest_rows == 5);
    WritePrivateProfileStringW(L"overlay", L"suggest", L"1", ini.c_str());
    WritePrivateProfileStringW(L"overlay", L"suggest_rows", L"20", ini.c_str());  // more than fits: 12
    c = read_overlay_config(ini);
    CHECK(c.suggest);
    CHECK(c.suggest_rows == 12);
    WritePrivateProfileStringW(L"overlay", L"suggest_rows", L"1", ini.c_str());
    CHECK(read_overlay_config(ini).suggest_rows == 3);
    WritePrivateProfileStringW(L"overlay", L"suggest_rows", L"lots", ini.c_str());  // not a number: the default
    CHECK(read_overlay_config(ini).suggest_rows == 8);
    DeleteFileW(ini.c_str());
}

TEST_CASE("toast: shows the lines added since it started, newest last, until it ends") {
    ConsoleModel m;
    m.add_line("old 1");
    m.add_line("old 2");
    const ToastState t{m.version(), 6000};
    CHECK(toast_lines(m, t, 100).empty());  // nothing new yet
    m.add_line("> give Material_T1_Wood_Logs 5");
    m.add_line("gave 5 x Material_T1_Wood_Logs to player 1");
    auto lines = toast_lines(m, t, 100);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].first == LineKind::Command);
    CHECK(lines[1].first == LineKind::Success);
    CHECK(lines[1].second == "gave 5 x Material_T1_Wood_Logs to player 1");
    CHECK(toast_lines(m, t, 6000).empty());  // over
    CHECK(toast_lines(m, ToastState{}, 100).empty());  // never started
}

TEST_CASE("toast: many new lines show only the newest few; a scrollback that dropped lines is fine") {
    ConsoleModel m(5);
    const ToastState t{m.version(), 1000};
    for (int i = 0; i < 20; ++i) m.add_line("line " + std::to_string(i));
    auto lines = toast_lines(m, t, 0, 4);
    REQUIRE(lines.size() == 4);
    CHECK(lines[0].second == "... 17 more lines in the console");
    CHECK(lines[1].second == "line 17");
    CHECK(lines[3].second == "line 19");
    CHECK(toast_lines(m, t, 0, 10).size() == 6);  // only 5 lines are kept: the note and those 5
}

TEST_CASE("toast: a long reply (like /help) says how many lines it does not show") {
    ConsoleModel m;
    const ToastState t{m.version(), 1000};
    m.add_line("> help");
    for (int i = 0; i < 12; ++i) m.add_line("command " + std::to_string(i));
    auto lines = toast_lines(m, t, 0, 6);
    REQUIRE(lines.size() == 6);
    CHECK(lines[0].second == "... 8 more lines in the console");
    CHECK(lines[1].second == "command 7");
    CHECK(lines[5].second == "command 11");
    CHECK(toast_lines(m, t, 0, 13).size() == 13);  // all fit: no note
}

TEST_CASE("toast: draws headless without trouble") {
    Headless h;
    ConsoleModel m;
    const ToastState t{m.version(), 1000};
    m.add_line("> time 12");
    m.add_line("time of day is now 12.00 h (was 3.00 h)");
    ImGui::NewFrame();
    draw_toast(toast_lines(m, t, 0), ImVec2(1920, 1080));
    ImGui::Render();
    bool found = false;
    for (ImGuiWindow* w : GImGui->Windows) found |= std::string(w->Name).find("##emf_toast") != std::string::npos;
    CHECK(found);
}

TEST_CASE("imgui_key: the keys the console uses") {
    CHECK(imgui_key(KeyEvent{0x0F, VK_TAB, true, false}) == ImGuiKey_Tab);
    CHECK(imgui_key(KeyEvent{0x1C, VK_RETURN, true, false}) == ImGuiKey_Enter);
    CHECK(imgui_key(KeyEvent{0x1C, VK_RETURN, true, true}) == ImGuiKey_KeypadEnter);
    CHECK(imgui_key(KeyEvent{0x48, VK_UP, true, true}) == ImGuiKey_UpArrow);
    CHECK(imgui_key(KeyEvent{0x1D, VK_CONTROL, true, false}) == ImGuiKey_LeftCtrl);
    CHECK(imgui_key(KeyEvent{0x2F, 'V', true, false}) == ImGuiKey_V);
    CHECK(imgui_key(KeyEvent{0x1E, 0, true, false}) == ImGuiKey_A);  // scan code only
    CHECK(imgui_key(KeyEvent{0x3B, VK_F1, true, false}) == ImGuiKey_None);
}

TEST_CASE("console UI: after a click on the scrollback, typing still goes to the input line") {
    Headless h;
    Rig r;
    for (int i = 0; i < 5; ++i) r.model.add_line("line " + std::to_string(i));
    r.frame();
    r.frame();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(400, 100);  // over the scrollback
    r.frame();
    io.AddMouseButtonEvent(0, true);
    r.frame();
    io.AddMouseButtonEvent(0, false);
    r.frame();
    r.frame();
    r.type("help");
    CHECK(r.ui.input == "help");
}
