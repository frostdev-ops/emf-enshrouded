// The console's Settings tab (in-game look phase 2): EMF's own settings on three pages with the Mods tab's widgets, the
// console key taken with two presses, and a refused change said in the tab. Drawn headless.
#include <doctest.h>

#include <windows.h>

#include <map>
#include <string>
#include <vector>

#include "core/emf_settings.h"
#include "core/overlay/mods_ui.h"
#include "core/overlay/overlay.h"
#include "core/overlay/settings_ui.h"
#include "imgui.h"
#include "imgui_internal.h"

using namespace emf::core;

namespace {
struct MapIni : IniBackend {
    std::map<std::string, std::string> values;
    bool fail = false;
    std::optional<std::string> read(const std::string& mod, const std::string& key) override {
        auto it = values.find(mod + "." + key);
        if (it == values.end()) return std::nullopt;
        return it->second;
    }
    bool write(const std::string& mod, const std::string& key, const std::string& value) override {
        if (fail) return false;
        values[mod + "." + key] = value;
        return true;
    }
};

int guarded(void (*fn)(void*), void* ctx) {
    __try {
        fn(ctx);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

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
    MapIni ini;
    SettingsStore store{&ini};
    ModsUiState ui;
    SettingsUiState st;
    int previews = 0;
    Rig() {
        for (SettingDef d : emf_setting_defs({"Mine.ttf"}))
            REQUIRE(store.declare(kEmfSettingsId, std::move(d), nullptr, SettingsStore::Declare::Keep));
    }
    void frame() {
        struct Ctx {
            Rig* r;
        } c{this};
        const int crashed = guarded([](void* p) {
            Rig* r = static_cast<Ctx*>(p)->r;
            ImGui::NewFrame();
            ImGui::SetNextWindowSize(ImVec2(1600, 700));
            ImGui::Begin("settings test");
            draw_settings_tab(&r->store, r->ui, r->st, [r] { ++r->previews; });
            ImGui::End();
            ImGui::Render();
        }, &c);
        REQUIRE_MESSAGE(crashed == 0, std::string(emf_imgui_last_assert()));
    }
};
}  // namespace

TEST_CASE("settings tab: every page draws every kind of setting without ImGui errors; the preview on the text page") {
    Headless h;
    Rig r;
    for (const char* page : {kPageText, kPageConsole, kPageStart}) {
        r.st.page = page;
        for (int i = 0; i < 3; ++i) r.frame();
    }
    CHECK(r.previews > 0);
    const int seen = r.previews;
    r.st.page = kPageConsole;
    r.frame();
    CHECK(r.previews == seen);  // only the text page shows it
    r.st.page = "no such page";  // falls back to the first page
    r.frame();
    CHECK(r.st.page == kPageText);
    CHECK(emf_imgui_last_assert()[0] == '\0');
}

TEST_CASE("settings tab: the console key, once pressed twice, is saved; a refused write is said and the old value stays") {
    Headless h;
    Rig r;
    r.st.page = kPageConsole;
    r.frame();
    r.ui.scan.value = "0x3b";  // what the key path (capture_key_event) leaves after two presses of F1
    r.frame();
    CHECK(r.store.get(kEmfSettingsId, "overlay.key") == "0x3b");
    CHECK(r.ini.values["emf.overlay.key"] == "0x3b");
    CHECK(r.ui.scan.value.empty());
    CHECK_FALSE(r.ui.message_error);
    r.ini.fail = true;
    r.ui.scan.value = "0x29";
    r.frame();
    CHECK(r.store.get(kEmfSettingsId, "overlay.key") == "0x3b");
    CHECK(r.ui.message_error);
    CHECK_FALSE(r.ui.message.empty());
}

TEST_CASE("settings tab: the console key's capture runs through the Mods tab's key path and is dropped with its widget") {
    ModsUiState ui;
    ui.scan.step = ScanCapture::Step::First;
    ui.scan.frame = 10;
    CHECK(capture_key_event(ui, KeyEvent{0x29, 0, true, false}, 11));  // taken: the console never sees it
    CHECK(ui.scan.step == ScanCapture::Step::Confirm);
    CHECK(capture_key_event(ui, KeyEvent{0x29, 0, true, false}, 11));
    CHECK(ui.scan.value == "0x29");
    ui.scan.step = ScanCapture::Step::First;
    ui.scan.frame = 10;
    CHECK_FALSE(capture_key_event(ui, KeyEvent{0x29, 0, true, false}, 30));  // the tab closed meanwhile: dropped
    CHECK(ui.scan.step == ScanCapture::Step::Idle);
}
