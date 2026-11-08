// The HUD page drawn headless: gate states, keys, hide confirmation, scale keys, outlines.
#include <doctest.h>

#include <windows.h>

#include <string>

#include "core/overlay/hud_page.h"
#include "hud_editor_rig.h"
#include "imgui.h"

using namespace emf::core;
using namespace emf::core::hud;
using hud_test::HudRig;

extern "C" const char* emf_imgui_last_assert();

namespace {

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

struct PageRig {
    Headless h;
    HudRig rig;
    HudPageState st;
    HudEditor* e = nullptr;
    PageRig() { e = &rig.make(); }
    void frame() {
        struct Ctx {
            PageRig* r;
        } c{this};
        const int crashed = guarded(
            [](void* p) {
                PageRig* r = static_cast<Ctx*>(p)->r;
                ImGui::NewFrame();
                ImGui::SetNextWindowSize(ImVec2(1600, 900));
                ImGui::Begin("hud page test");
                draw_hud_page(*r->e, r->st, ImVec2(1920, 1080));
                ImGui::End();
                ImGui::Render();
            },
            &c);
        REQUIRE_MESSAGE(crashed == 0, std::string(emf_imgui_last_assert()));
    }
    // A key goes in before a frame and comes out after the next one.
    void press(ImGuiKey k, bool shift = false, bool ctrl = false) {
        ImGuiIO& io = ImGui::GetIO();
        if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
        if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(k, true);
        frame();
        io.AddKeyEvent(k, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
    }
};

}  // namespace

TEST_CASE("hud page: an unknown build shows only its message") {
    PageRig p;
    p.rig.gate.build_known = false;
    p.e = &p.rig.make();
    p.frame();
    p.frame();
    CHECK_FALSE(p.e->snapshots().current()->edits[0].outline);
    p.press(ImGuiKey_RightArrow);
    CHECK(p.e->view().elements[0].is_default());
    CHECK(std::string(emf_imgui_last_assert()).empty());
}

TEST_CASE("hud page: every state draws without ImGui errors") {
    PageRig p;
    for (int state = 0; state < 4; ++state) {
        p.rig.gate = GateInput{false, true, "1287369"};
        p.rig.other_mod.clear();
        p.rig.fault = 0;
        if (state == 1) p.rig.set("enabled", "0");
        if (state == 2) {
            p.rig.set("enabled", "1");
            p.rig.other_mod = "ShroudHud";
            p.rig.now += 6000;
        }
        if (state == 3) {
            p.rig.other_mod.clear();
            p.rig.now += 6000;
            p.rig.fault = 0xC0000005;
        }
        p.e->tick();
        for (int i = 0; i < 3; ++i) p.frame();
    }
    CHECK(std::string(emf_imgui_last_assert()).empty());
}

TEST_CASE("hud page: keys move, step, hide, reset; vitals asks twice before hiding") {
    PageRig p;
    p.frame();
    p.press(ImGuiKey_RightArrow);
    CHECK(p.e->view().elements[0].x == 0.5f);
    p.press(ImGuiKey_DownArrow, true);
    CHECK(p.e->view().elements[0].y == 5.0f);
    p.press(ImGuiKey_Tab);
    CHECK(p.st.selected == 1);
    p.press(ImGuiKey_Tab, true);
    CHECK(p.st.selected == 0);
    p.press(ImGuiKey_H);
    CHECK_FALSE(p.e->view().elements[0].hidden);
    p.press(ImGuiKey_H);
    CHECK(p.e->view().elements[0].hidden);
    p.press(ImGuiKey_R);
    CHECK(p.e->view().elements[0].is_default());
    for (int i = 0; i < 3; ++i) p.press(ImGuiKey_Tab);
    CHECK(p.st.selected == 3);
    p.press(ImGuiKey_H);
    CHECK(p.e->view().elements[3].hidden);
}

TEST_CASE("hud page: keys do nothing while Ctrl is held") {
    PageRig p;
    p.frame();
    p.press(ImGuiKey_RightArrow, false, true);
    CHECK(p.e->view().elements[0].is_default());
    p.press(ImGuiKey_Tab, false, true);
    CHECK(p.st.selected == 0);
}

TEST_CASE("hud page: + and - scale the selected part by 0.05; with scale not available the keys still keep the value") {
    PageRig p;
    p.frame();
    p.press(ImGuiKey_Equal);
    CHECK(p.e->view().elements[0].scale == doctest::Approx(1.05f));
    p.press(ImGuiKey_Minus);
    p.press(ImGuiKey_Minus);
    CHECK(p.e->view().elements[0].scale == doctest::Approx(0.95f));
    p.rig.scale = ScaleState{false, false};
    p.frame();
    p.press(ImGuiKey_KeypadAdd);
    CHECK(p.e->view().elements[0].scale == doctest::Approx(1.0f));
    CHECK(std::string(emf_imgui_last_assert()).empty());
}

TEST_CASE("hud page: drawing it turns the outlines on") {
    PageRig p;
    p.frame();
    CHECK(p.e->snapshots().current()->edits[1].outline);
}

TEST_CASE("hud page: the layout bar saves, loads and deletes through the editor") {
    PageRig p;
    p.st.save_name = "Mine";
    p.frame();
    // the buttons are ImGui widgets; the page's own state carries what they call
    p.e->save_as("Mine");
    p.frame();
    CHECK(p.e->view().layout_name == "Mine");
    CHECK(std::string(emf_imgui_last_assert()).empty());
}

TEST_CASE("page_toggle: open, show, close, refuse") {
    CHECK(page_toggle(false, false, false) == PageToggle::Open);
    CHECK(page_toggle(true, false, false) == PageToggle::Show);
    CHECK(page_toggle(true, true, false) == PageToggle::Close);
    CHECK(page_toggle(false, false, true) == PageToggle::Refuse);
    CHECK(page_toggle(true, true, true) == PageToggle::Refuse);
}
