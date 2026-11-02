// EMF.exe's EML content card through the page bridge (app/bridge_content.cpp): "content" and its actions, Build again
// at the next start, Remove the copy, When to apply ([content] mode), Let EMF load EML content (EML's dbghelp.dll set
// aside, E5) and its give-back. Every action is refused while the game runs; none writes a game file; EMF's uninstall
// puts EML's dbghelp.dll back; and EMF.exe never loads emf_content.dll.
#include <doctest.h>

#include <windows.h>

#include <psapi.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "app/bridge.h"
#include "app/text.h"
#include "app_content_files.h"
#include "core/json.h"
#include "loader/content_shadow.h"

using namespace emf::app;
using namespace app_content_test;
namespace json = emf::core::json;

namespace {

struct ContentHost : BridgeHost {
    std::wstring dir;
    std::string running;
    std::wstring game() const override { return dir; }
    void choose_game(const std::wstring& d) override { dir = d; }
    std::optional<std::wstring> pick_folder() override { return std::nullopt; }
    bool open(const std::wstring&, Open) override { return true; }
    std::string running_reason() override { return running; }
    void rendered(int, const std::vector<std::string>&) override {}
    std::wstring steam_game() override { return {}; }
    bool copy_text(const std::string&) override { return true; }
    std::optional<std::wstring> pick_archive() override { return std::nullopt; }
    // work runs at once and its messages are kept in `async_out`; with `defer` it waits in `queued` until run_queued()
    bool defer = false;
    std::vector<std::function<Done()>> queued;
    std::vector<std::string> async_out;
    void run_async(std::function<Done()> work) override {
        if (defer) {
            queued.push_back(std::move(work));
            return;
        }
        if (Done d = work())
            for (auto& m : d()) async_out.push_back(m);
    }
    void run_queued() {
        auto q = std::move(queued);
        queued.clear();
        for (auto& w : q)
            if (Done d = w())
                for (auto& m : d()) async_out.push_back(m);
    }
};

json::Value parse(const std::string& text) {
    auto r = json::parse(text);
    REQUIRE_MESSAGE(r.value, text);
    return *r.value;
}

// The "content" reply among a command's replies.
json::Value content_reply(const std::vector<std::string>& replies) {
    for (const auto& r : replies) {
        json::Value v = parse(r);
        if (v.get("type") && v.get("type")->s == "content") return v;
    }
    FAIL("no content reply");
    return {};
}

const json::Value& at(const json::Value& v, const char* key) {
    const json::Value* x = v.get(key);
    REQUIRE_MESSAGE(x, key);
    return *x;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string file_bytes(const std::wstring& path) { return content_fixtures::file_text(path); }

DWORD link_count(const std::wstring& path) { return emf::loader::shadow_detail::file_id(path).links; }

// Runs a command line, waits for it and returns its exit code.
DWORD run(std::wstring cmd) {
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    REQUIRE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

void junction(const std::wstring& at, const std::wstring& to) {
    REQUIRE(run(L"cmd.exe /c mklink /J \"" + at + L"\" \"" + to + L"\"") == 0);
    REQUIRE((GetFileAttributesW(at.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
}

// `s` inside a JSON string.
std::string json_escape(const std::string& s) {
    std::string out;
    for (const char c : s) out += c == '\\' ? std::string("\\\\") : c == '"' ? std::string("\\\"") : std::string(1, c);
    return out;
}

const char* kEmlProxy = "MZ EML dbghelp proxy EML_LOG_FILE_PATH \x01\x02 bytes";

}  // namespace

TEST_CASE("app content bridge: content answers with the panel") {
    ContentGame g(L"cb_panel");
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    const auto v = content_reply(bridge.handle(R"({"cmd":"content"})"));
    CHECK(at(v, "cmd").s == "content");
    CHECK(at(v, "ok").b);
    CHECK(at(at(v, "panel"), "status").s == "builds_next");
    REQUIRE(at(at(v, "panel"), "mods").arr.size() == 1);
}

TEST_CASE("app content bridge: a dedicated server's folder gets the one line, and no action") {
    ContentGame g(L"cb_server");
    std::filesystem::remove(g.at(L"enshrouded.exe"));
    g.write(L"enshrouded_server.exe", "fake server");
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    CHECK(at(at(content_reply(bridge.handle(R"({"cmd":"content"})")), "panel"), "status").s == "server_later");
    const auto v = content_reply(bridge.handle(R"({"cmd":"content_mode","mode":"off"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK_FALSE(g.has(L"emf\\emf_core.ini"));
}

TEST_CASE("app content bridge: every action is refused while the game runs, and nothing changes") {
    ContentGame g(L"cb_running");
    g.built();
    g.write(L"dbghelp.dll", kEmlProxy);
    g.write(L"emf\\emf_core.ini", "[content]\r\nmode=auto\r\n");
    ContentHost host;
    host.dir = g.dir;
    host.running = "Enshrouded is running from this folder (process 42)";
    Bridge bridge(host);
    const std::string state = file_bytes(g.shadow() + L"\\emf_content_state.json");
    for (const char* msg : {R"({"cmd":"content_rebuild"})", R"({"cmd":"content_remove"})", R"({"cmd":"content_mode","mode":"off"})",
                            R"({"cmd":"content_take_over"})", R"({"cmd":"content_give_back"})"}) {
        const auto v = content_reply(bridge.handle(msg));
        CHECK_FALSE(at(v, "ok").b);
        CHECK(has(at(v, "why").s, "process 42"));
        CHECK(at(at(v, "panel"), "status").s == "other_tool");
    }
    CHECK(file_bytes(g.shadow() + L"\\emf_content_state.json") == state);
    CHECK(g.has(L"emf\\content\\client\\enshrouded.kfc"));
    CHECK(file_bytes(g.at(L"emf\\emf_core.ini")) == "[content]\r\nmode=auto\r\n");
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == kEmlProxy);
    CHECK_FALSE(g.has(L"emf\\moved.txt"));
}

TEST_CASE("app content bridge: Build again at the next start deletes only the state") {
    ContentGame g(L"cb_rebuild");
    g.built();
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    CHECK(at(at(content_reply(bridge.handle(R"({"cmd":"content"})")), "panel"), "status").s == "ready");
    const auto v = content_reply(bridge.handle(R"({"cmd":"content_rebuild"})"));
    CHECK(at(v, "ok").b);
    CHECK(has(at(v, "text").s, "next start"));
    CHECK_FALSE(g.has(L"emf\\content\\client\\emf_content_state.json"));
    CHECK(g.has(L"emf\\content\\client\\shadow.txt"));
    CHECK(g.has(L"emf\\content\\client\\enshrouded.kfc"));
    CHECK(g.has(L"emf\\content\\client\\mods\\beasts\\mod.json"));
    CHECK(at(at(v, "panel"), "status").s == "builds_next");
}

TEST_CASE("app content bridge: Remove the copy deletes the shadow and leaves the game's linked files whole") {
    ContentGame g(L"cb_remove");
    g.built();
    REQUIRE(link_count(g.at(L"enshrouded_000.dat")) == 2);  // the shadow's hard link
    REQUIRE(link_count(g.at(L"enshrouded.exe")) == 2);
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    const auto v = content_reply(bridge.handle(R"({"cmd":"content_remove"})"));
    CHECK(at(v, "ok").b);
    CHECK_FALSE(g.has(L"emf\\content\\client"));
    CHECK(file_bytes(g.at(L"enshrouded_000.dat")) == "dat 0");
    CHECK(file_bytes(g.at(L"enshrouded_001.dat")) == "dat 1");
    CHECK(file_bytes(g.at(L"enshrouded.exe")) == "fake exe");
    CHECK(file_bytes(g.at(L"enshrouded.kfc")) == content_fixtures::kfc_bytes("7|r|t", 2));
    CHECK(link_count(g.at(L"enshrouded_000.dat")) == 1);
    CHECK(link_count(g.at(L"enshrouded_001.dat")) == 1);
    CHECK(link_count(g.at(L"enshrouded.exe")) == 1);
    CHECK(g.has(L"mods\\beasts\\mod.json"));  // the player's mods stay
    CHECK(at(at(v, "panel"), "status").s == "builds_next");
    CHECK_FALSE(at(at(v, "panel"), "copy_there").b);
    // again: nothing to remove
    CHECK(at(content_reply(bridge.handle(R"({"cmd":"content_remove"})")), "ok").b);
}

TEST_CASE("app content bridge: Remove the copy refuses a junction on the way, and what it points to stays") {
    ContentGame g(L"cb_junction");
    const std::wstring elsewhere = g.at(L"elsewhere");
    g.write(L"elsewhere\\client\\precious.txt", "not EMF's");
    junction(g.at(L"emf\\content"), elsewhere);
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    const auto v = content_reply(bridge.handle(R"({"cmd":"content_remove"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK(has(at(v, "why").s, "junction"));
    CHECK(file_bytes(elsewhere + L"\\client\\precious.txt") == "not EMF's");
    CHECK_FALSE(at(content_reply(bridge.handle(R"({"cmd":"content_rebuild"})")), "ok").b);
    RemoveDirectoryW(g.at(L"emf\\content").c_str());  // the junction, not what it points to
}

TEST_CASE("app content bridge: When to apply writes [content] mode, keeps the rest, and takes only auto, on and off") {
    ContentGame g(L"cb_mode");
    g.write(L"emf\\emf_core.ini", "[backup]\r\non_start=1\r\n\r\n[content]\r\nlog_opens=1\r\n");
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    auto v = content_reply(bridge.handle(R"({"cmd":"content_mode","mode":"off"})"));
    CHECK(at(v, "ok").b);
    CHECK(at(at(v, "panel"), "mode").s == "off");
    CHECK(at(at(v, "panel"), "status").s == "off");
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"backup", L"on_start", L"", buf, 64, g.at(L"emf\\emf_core.ini").c_str());
    CHECK(std::wstring(buf) == L"1");
    GetPrivateProfileStringW(L"content", L"log_opens", L"", buf, 64, g.at(L"emf\\emf_core.ini").c_str());
    CHECK(std::wstring(buf) == L"1");
    v = content_reply(bridge.handle(R"({"cmd":"content_mode","mode":"on"})"));
    CHECK(at(at(v, "panel"), "mode").s == "on");
    const std::string before = file_bytes(g.at(L"emf\\emf_core.ini"));
    for (const char* msg : {R"({"cmd":"content_mode","mode":"maybe"})", R"({"cmd":"content_mode","mode":"on\r\nx=1"})",
                            R"({"cmd":"content_mode"})", R"({"cmd":"content_mode","mode":1})"}) {
        v = content_reply(bridge.handle(msg));
        CHECK_FALSE(at(v, "ok").b);
        CHECK(has(at(v, "why").s, "auto, on or off"));
    }
    CHECK(file_bytes(g.at(L"emf\\emf_core.ini")) == before);
}

TEST_CASE("app content bridge: Let EMF load EML content sets EML's dbghelp.dll aside, records it, and gives it back") {
    ContentGame g(L"cb_takeover");
    g.write(L"dbghelp.dll", kEmlProxy);
    g.write(L"emf\\moved.txt", "moved\tcreative.dll\r\n");
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    auto v = content_reply(bridge.handle(R"({"cmd":"content"})"));
    CHECK(at(at(v, "panel"), "take_over").s == "offer");
    v = content_reply(bridge.handle(R"({"cmd":"content_take_over"})"));
    CHECK_MESSAGE(at(v, "ok").b, at(v, "why").s);
    CHECK_FALSE(g.has(L"dbghelp.dll"));
    CHECK(file_bytes(g.at(L"eml-dbghelp.dll")) == kEmlProxy);
    CHECK(file_bytes(g.at(L"emf\\moved.txt")) == "moved\tcreative.dll\r\nset-aside\teml-dbghelp.dll\tdbghelp.dll\r\n");
    CHECK(file_bytes(g.at(L"dinput8-chain.ini")).empty());  // never listed: EMF does not start EML's proxy
    CHECK(at(at(v, "panel"), "take_over").s == "given");
    CHECK(at(at(v, "panel"), "status").s == "builds_next");
    // a second take-over is refused
    v = content_reply(bridge.handle(R"({"cmd":"content_take_over"})"));
    CHECK_FALSE(at(v, "ok").b);
    // the give-back reverses it
    v = content_reply(bridge.handle(R"({"cmd":"content_give_back"})"));
    CHECK_MESSAGE(at(v, "ok").b, at(v, "why").s);
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == kEmlProxy);
    CHECK_FALSE(g.has(L"eml-dbghelp.dll"));
    CHECK(file_bytes(g.at(L"emf\\moved.txt")) == "moved\tcreative.dll\r\n");
    CHECK(at(at(v, "panel"), "status").s == "other_tool");
    CHECK(at(at(v, "panel"), "take_over").s == "offer");
    // a give-back without a take-over is refused
    v = content_reply(bridge.handle(R"({"cmd":"content_give_back"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == kEmlProxy);
}

TEST_CASE("app content bridge: a give-back never replaces a dbghelp.dll that is there again; a take-over needs a free name") {
    ContentGame g(L"cb_giveback_busy");
    g.write(L"dbghelp.dll", kEmlProxy);
    g.write(L"eml-dbghelp.dll", "an older EML proxy EML_LOG_FILE_PATH");  // not EMF's: no record of it
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    auto v = content_reply(bridge.handle(R"({"cmd":"content_take_over"})"));
    REQUIRE(at(v, "ok").b);
    CHECK(file_bytes(g.at(L"eml-dbghelp.dll")) == "an older EML proxy EML_LOG_FILE_PATH");
    CHECK(file_bytes(g.at(L"eml-dbghelp-2.dll")) == kEmlProxy);
    CHECK(has(file_bytes(g.at(L"emf\\moved.txt")), "set-aside\teml-dbghelp-2.dll\tdbghelp.dll"));
    g.write(L"dbghelp.dll", "EML installed again EML_LOG_FILE_PATH");
    v = content_reply(bridge.handle(R"({"cmd":"content_give_back"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK(has(at(v, "why").s, "dbghelp.dll"));
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == "EML installed again EML_LOG_FILE_PATH");
    CHECK(file_bytes(g.at(L"eml-dbghelp-2.dll")) == kEmlProxy);
    v = content_reply(bridge.handle(R"({"cmd":"content_take_over"})"));  // EMF set one aside already
    CHECK_FALSE(at(v, "ok").b);
}

TEST_CASE("app content bridge: ShroudForge's dbghelp.dll, an unknown one, or none is never set aside") {
    ContentGame g(L"cb_sf");
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    CHECK_FALSE(at(content_reply(bridge.handle(R"({"cmd":"content_take_over"})")), "ok").b);  // none there
    g.write(L"dbghelp.dll", "MZ shroudforge runtime EML_LOG_FILE_PATH");
    auto v = content_reply(bridge.handle(R"({"cmd":"content_take_over"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK(has(at(v, "why").s, "ShroudForge"));
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == "MZ shroudforge runtime EML_LOG_FILE_PATH");
    g.write(L"dbghelp.dll", "MZ some other proxy");
    CHECK_FALSE(at(content_reply(bridge.handle(R"({"cmd":"content_take_over"})")), "ok").b);
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == "MZ some other proxy");
    CHECK_FALSE(g.has(L"emf\\moved.txt"));
}

TEST_CASE("app content bridge: without emf_content.dll EMF does not take over from EML") {
    ContentGame g(L"cb_takeover_nodll", false);
    g.write(L"dbghelp.dll", kEmlProxy);
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    const auto v = content_reply(bridge.handle(R"({"cmd":"content_take_over"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK(has(at(v, "why").s, "emf_content.dll"));
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == kEmlProxy);
}

TEST_CASE("app content bridge: EMF's uninstall puts EML's dbghelp.dll back, byte for byte") {
    ContentGame g(L"cb_uninstall");
    g.write(L"dbghelp.dll", kEmlProxy);
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    REQUIRE(at(content_reply(bridge.handle(R"({"cmd":"content_take_over"})")), "ok").b);
    REQUIRE_FALSE(g.has(L"dbghelp.dll"));
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring ps = std::wstring(sys, n) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring script = std::filesystem::path(EMF_SOURCE_DIR "/package/uninstall.ps1").make_preferred().wstring();
    const DWORD code = run(L"\"" + ps + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script +
                           L"\" -Game \"" + g.dir + L"\"");
    CHECK(code == 0);
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == kEmlProxy);
    CHECK_FALSE(g.has(L"eml-dbghelp.dll"));
    CHECK_FALSE(g.has(L"emf\\moved.txt"));
    CHECK_FALSE(g.has(L"dinput8.dll"));  // EMF's loader went
}

TEST_CASE("app content bridge: EMF's uninstall leaves EML's dbghelp.dll set aside while another dbghelp.dll is there") {
    ContentGame g(L"cb_uninstall_busy");
    g.write(L"dbghelp.dll", kEmlProxy);
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    REQUIRE(at(content_reply(bridge.handle(R"({"cmd":"content_take_over"})")), "ok").b);
    g.write(L"dbghelp.dll", "a newer EML EML_LOG_FILE_PATH");
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring ps = std::wstring(sys, n) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring script = std::filesystem::path(EMF_SOURCE_DIR "/package/uninstall.ps1").make_preferred().wstring();
    CHECK(run(L"\"" + ps + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script + L"\" -Game \"" +
              g.dir + L"\" -All -Force") == 0);
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == "a newer EML EML_LOG_FILE_PATH");
    CHECK(file_bytes(g.at(L"eml-dbghelp.dll")) == kEmlProxy);
    CHECK(file_bytes(g.at(L"emf\\moved.txt")) == "set-aside\teml-dbghelp.dll\tdbghelp.dll\r\n");  // kept, also with -All
}

TEST_CASE("app content bridge: EMF.exe never loads emf_content.dll") {
    // no app source loads a library at all, and none includes the loader's build or start code (which do)
    const std::regex load(R"(LoadLibrary\w*\s*\(|LoadPackagedLibrary\s*\()");
    const std::regex loader_code(R"(#\s*include\s*["<][^">]*content_(build|start)\.h)");
    int files = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(EMF_SOURCE_DIR "/app")) {
        const auto ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".js") continue;
        std::ifstream f(e.path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        ++files;
        CHECK_MESSAGE(!std::regex_search(text, load), e.path().string());
        CHECK_MESSAGE(!std::regex_search(text, loader_code), e.path().string());
    }
    CHECK(files > 20);
    // and at run time: a real DLL with emf_content's exports in the folder, every request and action, and the
    // process never had it
    ContentGame g(L"cb_noload", false);
    REQUIRE(CopyFileW(std::filesystem::path(EMF_BINARY_DIR "/standin_content/emf_content.dll").make_preferred().c_str(),
                      g.at(L"emf\\emf_content.dll").c_str(), FALSE));
    g.write(L"dbghelp.dll", kEmlProxy);
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    for (const char* msg : {R"({"cmd":"content"})", R"({"cmd":"content_take_over"})", R"({"cmd":"content_rebuild"})",
                            R"({"cmd":"content_mode","mode":"on"})", R"({"cmd":"content_remove"})",
                            R"({"cmd":"content_give_back"})", R"({"cmd":"state"})"})
        (void)bridge.handle(msg);
    HMODULE mods[1024];
    DWORD bytes = 0;
    REQUIRE(EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &bytes));
    for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(mods[i], path, MAX_PATH);
        CHECK_MESSAGE(_wcsnicmp(path, g.dir.c_str(), g.dir.size()) != 0, "loaded from the game folder");
    }
}

TEST_CASE("app content bridge: the logs carry emf\\content.log as text") {
    ContentGame g(L"cb_log");
    g.write(L"emf\\emf_core.log", "core");
    g.write(L"emf\\content.log", "[emf] building EML content <script>window.__x=1</script>\r\n[info] built 1 mod\r\n");
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    (void)bridge.handle(R"({"cmd":"state"})");
    for (const auto& r : bridge.handle(R"({"cmd":"logs"})")) {
        const json::Value v = parse(r);
        if (!v.get("type") || v.get("type")->s != "logs") continue;
        CHECK(at(v, "core").s == "core");
        CHECK(at(v, "content").s == "[emf] building EML content <script>window.__x=1</script>\r\n[info] built 1 mod\r\n");
        return;
    }
    FAIL("no logs reply");
}

// ---- the review's fixes ----

TEST_CASE("app content bridge: a take-over that could not work is refused: [content] mode=off, EML's dinput8.dll chained") {
    ContentGame g(L"cb_takeover_cannot");
    g.write(L"dbghelp.dll", kEmlProxy);
    g.write(L"emf\\emf_core.ini", "[content]\r\nmode=off\r\n");
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    auto v = content_reply(bridge.handle(R"({"cmd":"content_take_over"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK(has(at(v, "why").s, "Off"));
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == kEmlProxy);
    g.write(L"emf\\emf_core.ini", "[content]\r\nmode=auto\r\n");
    g.write(L"eml.dll", "MZ EML proxy EML_LOG_FILE_PATH");
    g.write(L"dinput8-chain.ini", "[Chain]\r\neml.dll\r\n");
    v = content_reply(bridge.handle(R"({"cmd":"content_take_over"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK(has(at(v, "why").s, "dinput8-chain.ini"));
    CHECK(file_bytes(g.at(L"dbghelp.dll")) == kEmlProxy);
    CHECK_FALSE(g.has(L"emf\\moved.txt"));
}

TEST_CASE("app content bridge: a hand-edited set-aside record that names another file moves nothing, nor does uninstall") {
    ContentGame g(L"cb_badrecord");
    g.write(L"creative.dll", "creative mode");
    g.write(L"x.dll", "x");
    const std::string record =
        "set-aside\tcreative.dll\tdbghelp.dll\r\nset-aside\t..\\x.dll\tdbghelp.dll\r\nset-aside\tdinput8.dll\tdbghelp.dll\r\n";
    g.write(L"emf\\moved.txt", record);
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    CHECK_FALSE(at(content_reply(bridge.handle(R"({"cmd":"content_give_back"})")), "ok").b);
    CHECK(file_bytes(g.at(L"creative.dll")) == "creative mode");
    CHECK_FALSE(g.has(L"dbghelp.dll"));
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring ps = std::wstring(sys, n) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    const std::wstring script = std::filesystem::path(EMF_SOURCE_DIR "/package/uninstall.ps1").make_preferred().wstring();
    CHECK(run(L"\"" + ps + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script + L"\" -Game \"" +
              g.dir + L"\"") == 0);
    CHECK(file_bytes(g.at(L"creative.dll")) == "creative mode");
    CHECK(file_bytes(g.at(L"x.dll")) == "x");
    CHECK_FALSE(g.has(L"dbghelp.dll"));
}

TEST_CASE("app content bridge: actions wait while EMF is busy switching a profile or installing") {
    ContentGame g(L"cb_busy");
    g.built();
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    (void)bridge.handle(R"({"cmd":"profile_save","name":"Smoke"})");
    REQUIRE(g.has(L"emf\\profiles\\smoke.json"));
    host.defer = true;
    (void)bridge.handle(R"({"cmd":"profile_switch","slug":"smoke"})");
    REQUIRE(host.queued.size() == 1);
    const auto v = content_reply(bridge.handle(R"({"cmd":"content_rebuild"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK(has(at(v, "why").s, "busy"));
    CHECK(g.has(L"emf\\content\\client\\emf_content_state.json"));
    host.run_queued();
    CHECK(at(content_reply(bridge.handle(R"({"cmd":"content_rebuild"})")), "ok").b);
}

TEST_CASE("app content bridge: the reply names its folder, and an action meant for another folder is refused") {
    ContentGame g(L"cb_folder");
    g.built();
    ContentHost host;
    host.dir = g.dir;
    Bridge bridge(host);
    auto v = content_reply(bridge.handle(R"({"cmd":"content"})"));
    CHECK(at(v, "game").s == emf::app::utf8(g.dir));
    v = content_reply(bridge.handle(R"({"cmd":"content_remove","game":"C:\\Some\\Other\\Enshrouded"})"));
    CHECK_FALSE(at(v, "ok").b);
    CHECK(g.has(L"emf\\content\\client\\enshrouded.kfc"));
    v = content_reply(bridge.handle(R"({"cmd":"content_rebuild","game":")" + json_escape(emf::app::utf8(g.dir)) + R"("})"));
    CHECK(at(v, "ok").b);
}
