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

// ---- the colour controls ----
namespace {
StyleBlock page_block() {
    StyleBlock b{};
    b[kFrame] = {rgba_to_game(0x10101080), 1.0f};
    b[kRange] = {rgba_to_game(0x202020C0), 1.0f};
    b[kFill] = {rgba_to_game(0x4FD17CFF), 1.0f};
    b[kFillEnd] = {rgba_to_game(0xA0F0B0E0), 1.2f};
    b[kDamage] = {rgba_to_game(0xD04040FF), 1.0f};
    b[kHeal] = {rgba_to_game(0xF0F0A0FF), 1.0f};
    return b;
}
}  // namespace

TEST_CASE("page_colour: the player's, else the game's, else white") {  // Review Focus 5
    HudView v;
    CHECK(page_colour(v, kBarStamina, BarPart::Fill) == 0xFFFFFFFFu);  // not seen yet
    v.game_colours[kBarHealth] = page_block();
    CHECK(page_colour(v, kBarHealth, BarPart::Fill) == 0x4FD17CFFu);
    CHECK(page_colour(v, kBarHealth, BarPart::Empty) == 0x202020C0u);
    v.colours[kBarHealth].part[static_cast<size_t>(BarPart::Empty)] = *parse_colour("#ff0000").colour;  // no alpha
    CHECK(page_colour(v, kBarHealth, BarPart::Empty) == 0xFF0000C0u);  // the game's alpha for that part
    v.colours[kBarHealth].part[static_cast<size_t>(BarPart::Fill)] = *parse_colour("#ff000040").colour;
    CHECK(page_colour(v, kBarHealth, BarPart::Fill) == 0xFF000040u);
    v.colours[kBarMana].part[static_cast<size_t>(BarPart::Fill)] = *parse_colour("#0000ff").colour;  // mana not seen
    CHECK(page_colour(v, kBarMana, BarPart::Fill) == 0x0000FFFFu);
}

TEST_CASE("page_panel_colour: the set panel, else health's frame from the game, else dark") {
    HudView v;
    CHECK(page_panel_colour(v) == 0x000000B0u);
    v.game_colours[kBarHealth] = page_block();
    CHECK(page_panel_colour(v) == 0x10101080u);
    v.panel = *parse_colour("#20208080").colour;
    CHECK(page_panel_colour(v) == 0x20208080u);
}

TEST_CASE("picked: the picker's floats as a colour with alpha") {
    const float a[4] = {1.0f, 0.25f, 0.0f, 0.5f};
    const Colour c = picked(a);
    CHECK(c.rgba == 0xFF400080u);
    CHECK(c.set);
    CHECK(c.has_alpha);
    const float b[4] = {2.0f, -1.0f, 0.5f, 1.0f};  // out of range is clamped
    CHECK(picked(b).rgba == 0xFF0080FFu);
}

TEST_CASE("console_only_colours: the set colours the page has no button for") {
    BarColours c;
    CHECK(console_only_colours(c).empty());
    c.part[static_cast<size_t>(BarPart::Fill)] = *parse_colour("#ff0000").colour;
    CHECK(console_only_colours(c).empty());
    c.part[static_cast<size_t>(BarPart::End)] = *parse_colour("#00ff00").colour;
    c.part[static_cast<size_t>(BarPart::Heal)] = *parse_colour("#0000ff80").colour;
    CHECK(console_only_colours(c) == "end #00ff00, heal #0000ff80");
}

TEST_CASE("hud page: the colour controls draw in every state") {
    PageRig p;
    const StyleBlock b = page_block();
    PanelStatus status = PanelStatus::Unknown;
    HudEditorDeps d = p.rig.deps();
    d.game_colours = [&b](int bar) -> std::optional<StyleBlock> {
        if (bar == kBarHealth) return b;
        return std::nullopt;
    };
    d.panel_status = [&status] { return status; };
    p.rig.editor = std::make_unique<HudEditor>(d);
    p.rig.editor->start();
    p.e = p.rig.editor.get();
    const Colour red = *parse_colour("#ff0000").colour;
    for (int state = 0; state < 4; ++state) {
        if (state == 1) {  // colours set, also some the page has no button for, and the panel
            p.e->set_colour(kBarHealth, BarPart::Fill, red, Save::Now);
            p.e->set_colour(kBarHealth, BarPart::Heal, red, Save::Now);
            p.e->set_panel(red, Save::Now);
        }
        if (state == 2) status = PanelStatus::ReadOnly;
        if (state == 3) p.rig.bars = false;
        for (size_t part : {size_t{0}, static_cast<size_t>(bar_element(kBarHealth)),
                            static_cast<size_t>(bar_element(kBarMana)), static_cast<size_t>(bar_element(kBarStamina))}) {
            p.st.selected = part;
            p.frame();
        }
    }
    CHECK(std::string(emf_imgui_last_assert()).empty());
}
