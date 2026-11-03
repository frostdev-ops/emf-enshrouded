// Mod windows (API v24): drawing a window with ImGui (headless): clicks come back with the widget's key, disabled and
// keyless widgets give none, a window without input gives none, the X closes it, and every widget kind draws.
#include <doctest.h>

#include <windows.h>

#include <optional>
#include <string>

#include "core/overlay/overlay.h"
#include "core/overlay/window_ui.h"
#include "imgui.h"
#include "imgui_internal.h"

using namespace emf::core;

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

WindowWidget widget(uint32_t type, const char* key, const char* text, uint32_t flags = 0) {
    WindowWidget w;
    w.type = type;
    w.key = key;
    w.text = text;
    w.flags = flags;
    return w;
}

struct Rig {
    WindowRequest req;
    WindowUiState st;
    bool input = true;
    std::optional<WindowClick> last;
    Rig() {
        req.id = "test.window";
        req.title = "Beasts";
        req.width = 30;
        req.widgets = {widget(EMF_UI_HEADING, "", "Active 1/3"),
                       widget(EMF_UI_SLOT, "active.1", "Wolf 2"),
                       widget(EMF_UI_SLOT, "active.2", "Slot 2", EMF_UI_SAME_LINE | EMF_UI_EMPTY),
                       widget(EMF_UI_SLOT, "active.3", "Level 15", EMF_UI_SAME_LINE | EMF_UI_LOCKED),
                       widget(EMF_UI_SEPARATOR, "", ""),
                       widget(EMF_UI_ROW, "beast.Boar", "Boar (out)", EMF_UI_SELECTED),
                       widget(EMF_UI_ROW, "beast.Bear", "Bear (ready)"),
                       widget(EMF_UI_BUTTON, "call", "Call"),
                       widget(EMF_UI_BUTTON, "release", "Release", EMF_UI_SAME_LINE | EMF_UI_DISABLED),
                       widget(EMF_UI_BUTTON, "", "No key"),
                       widget(EMF_UI_BAR, "", "Wolf 2"),
                       widget(EMF_UI_TEXT, "", "A long line of text that has to wrap at the window's width, again and "
                                               "again, because it is much longer than thirty text heights.")};
        req.widgets[1].count = 3;
        req.widgets[1].tooltip = "Level 12, ready";
        req.widgets[10].value = 800;
        req.widgets.back().rgba = 0xFF8040FFu;
    }
    void frame() {
        struct Ctx {
            Rig* r;
        } c{this};
        const int crashed = guarded([](void* p) {
            Rig* r = static_cast<Ctx*>(p)->r;
            ImGui::NewFrame();
            auto click = draw_window(r->req, r->st, r->input, ImVec2(1920, 1080), TextLook{});
            if (click) r->last = click;
            ImGui::Render();
        }, &c);
        REQUIRE_MESSAGE(crashed == 0, std::string(emf_imgui_last_assert()));
    }
    std::optional<ImRect> rect_of(const std::string& key) const {
        for (const auto& h : st.hit)
            if (h.first == key) return h.second;
        return std::nullopt;
    }
    void click_at(ImVec2 at, int button) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(at.x, at.y);
        frame();
        io.AddMouseButtonEvent(button, true);
        frame();
        io.AddMouseButtonEvent(button, false);
        frame();
        frame();
    }
    std::optional<WindowClick> click(const std::string& key, int button = 0) {
        last.reset();
        auto r = rect_of(key);
        REQUIRE(r);
        click_at(r->GetCenter(), button);
        return last;
    }
};
}  // namespace

TEST_CASE("window ui: a click returns the widget's key; a right click says right") {
    Headless h;
    Rig r;
    for (int i = 0; i < 3; ++i) r.frame();
    auto c = r.click("call");
    REQUIRE(c);
    CHECK(c->key == "call");
    CHECK_FALSE(c->right);
    c = r.click("beast.Bear", 1);
    REQUIRE(c);
    CHECK(c->key == "beast.Bear");
    CHECK(c->right);
    c = r.click("active.1");
    REQUIRE(c);
    CHECK(c->key == "active.1");
    CHECK(emf_imgui_last_assert()[0] == '\0');
}

TEST_CASE("window ui: disabled and keyless widgets give no click; a window without input gives none") {
    Headless h;
    Rig r;
    for (int i = 0; i < 3; ++i) r.frame();
    CHECK_FALSE(r.rect_of("release"));  // disabled: not clickable
    for (const auto& hit : r.st.hit) CHECK_FALSE(hit.first.empty());
    const auto call = r.rect_of("call");
    REQUIRE(call);
    r.input = false;  // the console is open over it
    r.last.reset();
    r.click_at(call->GetCenter(), 0);
    CHECK_FALSE(r.last);
}

TEST_CASE("window ui: the X closes it; a new window id starts fresh") {
    Headless h;
    Rig r;
    for (int i = 0; i < 3; ++i) r.frame();
    ImGuiWindow* win = ImGui::FindWindowByName(window_imgui_name(r.req).c_str());
    REQUIRE(win != nullptr);
    const ImRect bar = win->TitleBarRect();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float size = ImGui::GetFontSize();
    r.click_at(ImVec2(bar.Max.x - style.FramePadding.x - size * 0.5f, bar.Min.y + style.FramePadding.y + size * 0.5f),
               0);
    CHECK(r.st.close_clicked);
    r.req.id = "test.other";
    r.frame();
    CHECK_FALSE(r.st.close_clicked);
    CHECK(r.st.id == "test.other");
}

TEST_CASE("window ui: 256 widgets of every kind and flag, in every place, draw without ImGui errors") {
    Headless h;
    Rig r;
    r.req.widgets.clear();
    for (uint32_t i = 0; i < 256; ++i) {
        const uint32_t type = i % 7;
        const uint32_t flags = (i / 7) % 64;
        WindowWidget w = widget(type, i % 3 ? "" : "k", "text");
        w.key = i % 3 ? "" : ("k" + std::to_string(i));
        w.flags = flags;
        w.count = i;
        w.value = (i * 37) % 1001;
        r.req.widgets.push_back(w);
    }
    for (uint32_t place : {EMF_UI_PLACE_CENTRE, EMF_UI_PLACE_LEFT, EMF_UI_PLACE_RIGHT}) {
        r.req.place = place;
        for (int i = 0; i < 3; ++i) r.frame();
    }
    r.req.widgets.clear();  // an empty window
    r.frame();
    CHECK(emf_imgui_last_assert()[0] == '\0');
}
