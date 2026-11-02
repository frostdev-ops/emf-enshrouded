// The game-style look: where HUD lines and notes go at 720p, 1080p and 4K, the notes' fading band, the text's shadow,
// the navy and gold theme, and the drawing itself (headless ImGui: draw lists and windows are counted, not pixels).
#include <doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/overlay/console_model.h"
#include "core/overlay/console_ui.h"
#include "core/overlay/look_layout.h"
#include "core/overlay/mods_ui.h"
#include "core/overlay/wheel_model.h"
#include "core/overlay/wheel_ui.h"
#include "imgui.h"
#include "imgui_internal.h"

using namespace emf::core;

namespace {
const ImVec2 kScreens[] = {ImVec2(1280, 720), ImVec2(1920, 1080), ImVec2(3840, 2160)};
const HudPlace kPlaces[] = {HudPlace::TopRight, HudPlace::BottomRight, HudPlace::BottomLeft};
const NotesPlace kNotePlaces[] = {NotesPlace::TopCentre, NotesPlace::TopLeft, NotesPlace::Right};

float line_height(ImVec2 screen) { return screen.y / 54.0f; }

struct Headless {
    ImGuiContext* ctx;
    explicit Headless(ImVec2 display = ImVec2(1920, 1080)) {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = display;
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    }
    ~Headless() { ImGui::DestroyContext(ctx); }
};

// A window drawn in the last frame.
bool drawn(const char* name) {
    for (ImGuiWindow* w : GImGui->Windows)
        if (std::string(w->Name).find(name) != std::string::npos && w->Active) return true;
    return false;
}
ImGuiWindow* window(const char* name) {
    for (ImGuiWindow* w : GImGui->Windows)
        if (std::string(w->Name).find(name) != std::string::npos) return w;
    return nullptr;
}

TextLook look_of(HudStyle style, bool shadow = true) {
    TextLook t;
    t.style = style;
    t.shadow = shadow;
    return t;
}

// Draws the HUD lines in one frame; returns the background draw list's vertex count.
int hud_vertices(const std::vector<HudLine>& lines, const TextLook& look, ImVec2 display = ImVec2(1920, 1080)) {
    ImGui::NewFrame();
    draw_hud(lines, display, look);
    ImGui::Render();
    return ImGui::GetBackgroundDrawList()->VtxBuffer.Size;
}

bool has_colour(ImDrawList* dl, ImU32 c) {
    for (const ImDrawVert& v : dl->VtxBuffer)
        if (v.col == c) return true;
    return false;
}

ImU32 im_colour(uint32_t rgba) {
    return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}
}  // namespace

TEST_CASE("look layout: HUD lines sit in their corner, inside the screen, at 720p, 1080p and 4K") {
    for (const ImVec2 s : kScreens) {
        const float lh = line_height(s);
        const ImVec2 text(s.x * 0.15f, lh);
        for (const HudPlace place : kPlaces) {
            for (size_t i = 0; i < 3; ++i) {
                const ImVec2 p = hud_line_position(place, i, 3, text, lh, s);
                CHECK(p.x >= 0.0f);
                CHECK(p.y >= 0.0f);
                CHECK(p.x + text.x <= s.x);
                CHECK(p.y + lh <= s.y);
            }
        }
        // the corners: right places end a line height from the right edge, the left place starts there
        CHECK(hud_line_position(HudPlace::TopRight, 0, 1, text, lh, s).x == doctest::Approx(s.x - lh - text.x));
        CHECK(hud_line_position(HudPlace::BottomLeft, 0, 1, text, lh, s).x == doctest::Approx(lh));
        // the top right starts under the game's quest tracker; the bottom lines end a line height above the bottom
        CHECK(hud_line_position(HudPlace::TopRight, 0, 1, text, lh, s).y == doctest::Approx(s.y * 0.24f));
        CHECK(hud_line_position(HudPlace::BottomRight, 0, 1, text, lh, s).y + lh == doctest::Approx(s.y - lh));
        CHECK(hud_line_position(HudPlace::BottomLeft, 2, 3, text, lh, s).y + lh == doctest::Approx(s.y - lh));
    }
}

TEST_CASE("look layout: three stacked HUD lines keep their order top to bottom and never overlap") {
    for (const ImVec2 s : kScreens) {
        const float lh = line_height(s);
        for (const HudPlace place : kPlaces) {
            float last_bottom = -1.0f;
            for (size_t i = 0; i < 3; ++i) {
                const ImVec2 p = hud_line_position(place, i, 3, ImVec2(100.0f + 50.0f * i, lh), lh, s);
                CHECK(p.y >= last_bottom);  // below the line before it
                last_bottom = p.y + lh;
            }
        }
    }
}

TEST_CASE("look layout: the notes' band at each place: top centre under the compass, fading at both ends") {
    for (const ImVec2 s : kScreens) {
        const float lh = line_height(s);
        const ImVec2 text(s.x * 0.25f, lh * 2.2f);
        for (const NotesPlace place : kNotePlaces) {
            const NoteBand b = note_band(place, text, lh, s);
            CHECK(b.min.x >= 0.0f);
            CHECK(b.max.x <= s.x);
            CHECK(b.min.y >= 0.0f);
            CHECK(b.max.y <= s.y);
            // the text sits inside the band's solid middle, with room around it
            CHECK(b.text_min.x >= b.min.x + b.fade_left);
            CHECK(b.text_max.x <= b.max.x - b.fade_right);
            CHECK(b.text_max.x - b.text_min.x == doctest::Approx(text.x));
            CHECK(b.text_max.y - b.text_min.y == doctest::Approx(text.y));
            CHECK(b.text_min.y > b.min.y);
            CHECK(b.text_max.y < b.max.y);
            CHECK(note_wrap_width(place, s) <= s.x * 0.6f);
        }
        const NoteBand c = note_band(NotesPlace::TopCentre, text, lh, s);
        CHECK((c.min.x + c.max.x) * 0.5f == doctest::Approx(s.x * 0.5f));  // centred
        CHECK(c.min.y >= s.y * 0.06f);                                     // under the compass
        CHECK(c.min.y <= s.y * 0.10f);
        CHECK(c.fade_left > 0.0f);
        CHECK(c.fade_right == doctest::Approx(c.fade_left));
        const NoteBand l = note_band(NotesPlace::TopLeft, text, lh, s);  // from the left edge, fading to the right
        CHECK(l.min.x == 0.0f);
        CHECK(l.fade_left == 0.0f);
        CHECK(l.fade_right > 0.0f);
        CHECK(l.text_min.x == doctest::Approx(lh));
        CHECK(l.min.y <= lh);
        const NoteBand r = note_band(NotesPlace::Right, text, lh, s);  // from the right edge, fading to the left
        CHECK(r.max.x == s.x);
        CHECK(r.fade_right == 0.0f);
        CHECK(r.fade_left > 0.0f);
        CHECK(r.text_max.x == doctest::Approx(s.x - lh));
        // lines inside the block: centred at the top centre, left at the top left, right at the right
        CHECK(note_line_x(NotesPlace::TopCentre, c, 50.0f) == doctest::Approx(s.x * 0.5f - 25.0f));
        CHECK(note_line_x(NotesPlace::TopLeft, l, 50.0f) == doctest::Approx(l.text_min.x));
        CHECK(note_line_x(NotesPlace::Right, r, 50.0f) == doctest::Approx(r.text_max.x - 50.0f));
    }
}

TEST_CASE("look layout: the band is solid in the middle and fades to nothing at both ends") {
    Headless h;
    ImGui::NewFrame();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const NoteBand b = note_band(NotesPlace::TopCentre, ImVec2(400, 40), 20.0f, ImVec2(1920, 1080));
    draw_note_band(dl, b);
    ImGui::Render();
    REQUIRE(dl->VtxBuffer.Size > 0);
    const unsigned band_alpha = kBandColour & 0xFF;
    CHECK(band_alpha >= 160);  // about 70 %
    CHECK(band_alpha <= 200);
    bool edge_left = false, edge_right = false, middle = false;
    for (const ImDrawVert& v : dl->VtxBuffer) {
        const unsigned a = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
        if (v.pos.x == b.min.x) {
            CHECK(a == 0);  // nothing at the very ends
            edge_left = true;
        }
        if (v.pos.x == b.max.x) {
            CHECK(a == 0);
            edge_right = true;
        }
        if (v.pos.x == b.min.x + b.fade_left || v.pos.x == b.max.x - b.fade_right) {
            CHECK(a == band_alpha);  // full where the fades end
            middle = true;
        }
    }
    CHECK(edge_left);
    CHECK(edge_right);
    CHECK(middle);
}

TEST_CASE("look layout: the shadow is 1-3 px by size; plain and boxed text have none, game style has it") {
    CHECK(shadow_offset(13.0f) == 1.0f);
    CHECK(shadow_offset(20.0f) == 1.0f);
    CHECK(shadow_offset(40.0f) == 2.0f);
    CHECK(shadow_offset(80.0f) == 3.0f);
    const auto game = text_passes(HudStyle::Game, true, 20.0f);
    REQUIRE(game.size() >= 2);
    CHECK_FALSE(game.back().shadow);  // the text itself last, on top
    CHECK(game.back().offset.x == 0.0f);
    int shadows = 0;
    for (const auto& p : game) shadows += p.shadow;
    CHECK(shadows == static_cast<int>(game.size()) - 1);
    bool drop = false;
    for (const auto& p : game)
        if (p.shadow && p.offset.x == shadow_offset(20.0f) && p.offset.y == shadow_offset(20.0f)) {
            CHECK(p.alpha == doctest::Approx(kShadowAlpha));
            drop = true;
        }
    CHECK(drop);
    for (const auto& p : {text_passes(HudStyle::Plain, true, 20.0f), text_passes(HudStyle::Boxed, true, 20.0f),
                          text_passes(HudStyle::Game, false, 20.0f)}) {
        REQUIRE(p.size() == 1);
        CHECK_FALSE(p[0].shadow);
    }
}

TEST_CASE("look drawing: game-style HUD lines are text and shadow with no box; plain is the text alone") {
    Headless h;
    const std::vector<HudLine> one{{"clock.time", "Hi", HudPlace::BottomRight, 0}};
    const int plain = hud_vertices(one, look_of(HudStyle::Plain));
    CHECK(plain == 8);  // two glyphs, four corners each
    CHECK_FALSE(drawn("##emf_hud"));
    const int game = hud_vertices(one, look_of(HudStyle::Game));
    CHECK(game == plain * static_cast<int>(text_passes(HudStyle::Game, true, 13.0f).size()));
    CHECK_FALSE(drawn("##emf_hud"));
    CHECK(hud_vertices(one, look_of(HudStyle::Game, false)) == plain);  // shadow=0
    // the line's colour: the look's by default, the mod's own when it chose one
    hud_vertices(one, look_of(HudStyle::Plain));
    CHECK(has_colour(ImGui::GetBackgroundDrawList(), im_colour(0xFFFFF4FFu)));
    hud_vertices({{"x", "Hi", HudPlace::TopRight, 0xFF8000FFu}}, look_of(HudStyle::Game));
    CHECK(has_colour(ImGui::GetBackgroundDrawList(), im_colour(0xFF8000FFu)));
    CHECK_FALSE(has_colour(ImGui::GetBackgroundDrawList(), im_colour(0xFFFFF4FFu)));
}

TEST_CASE("look drawing: style=boxed keeps today's boxes, one per place") {
    Headless h;
    const std::vector<HudLine> lines{{"a", "Hi", HudPlace::TopRight, 0}, {"b", "Ho", HudPlace::BottomLeft, 0}};
    CHECK(hud_vertices(lines, look_of(HudStyle::Boxed)) == 0);  // nothing on the background: in windows
    CHECK(drawn("##emf_hud_top_right"));
    CHECK(drawn("##emf_hud_bottom_left"));
    CHECK_FALSE(drawn("##emf_hud_bottom_right"));
}

TEST_CASE("look drawing: notes show top centre on the band in game style; plain has no band; boxed is today's box") {
    Headless h;
    ConsoleModel m;
    const ToastState t{m.version(), 1000};
    m.add_line("Sleeping");
    const ConsoleLines lines = toast_lines(m, t, 0);
    const ImVec2 display(1920, 1080);
    ImGui::NewFrame();
    draw_notes(lines, display, look_of(HudStyle::Game));
    ImGui::Render();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const int game = dl->VtxBuffer.Size;
    CHECK(has_colour(dl, im_colour(kBandColour)));  // the band's solid middle
    float lo = display.x, hi = 0.0f, top = display.y;
    for (const ImDrawVert& v : dl->VtxBuffer) {
        lo = (std::min)(lo, v.pos.x);
        hi = (std::max)(hi, v.pos.x);
        top = (std::min)(top, v.pos.y);
    }
    CHECK((lo + hi) * 0.5f == doctest::Approx(display.x * 0.5f).epsilon(0.01));  // centred
    CHECK(top >= display.y * 0.06f);  // under the compass
    CHECK_FALSE(drawn("##emf_toast"));
    ImGui::NewFrame();
    draw_notes(lines, display, look_of(HudStyle::Plain));
    ImGui::Render();
    CHECK_FALSE(has_colour(ImGui::GetBackgroundDrawList(), im_colour(kBandColour)));
    CHECK(ImGui::GetBackgroundDrawList()->VtxBuffer.Size < game);
    TextLook boxed = look_of(HudStyle::Boxed);
    boxed.notes_place = NotesPlace::TopLeft;
    ImGui::NewFrame();
    draw_notes(lines, display, boxed);
    ImGui::Render();
    CHECK(drawn("##emf_toast"));
    CHECK(ImGui::GetBackgroundDrawList()->VtxBuffer.Size == 0);
    REQUIRE(window("##emf_toast"));
    CHECK(window("##emf_toast")->Pos.x < 100.0f);  // today's place, top left
}

TEST_CASE("look theme: the accent replaces ImGui's blue in tabs, the selection and the scrollbar; no blue is left") {
    Headless h;
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    const auto blue = [](const ImVec4& c) { return c.z - (std::max)(c.x, c.y) > 0.10f; };
    int before = 0;
    for (int i = 0; i < ImGuiCol_COUNT; ++i) before += blue(style.Colors[i]);
    CHECK(before > 5);  // ImGui's dark theme is blue
    LookSettings look;
    apply_look_theme(style, look);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        INFO("colour " << ImGui::GetStyleColorName(i));
        CHECK_FALSE(blue(style.Colors[i]));
    }
    // gold: red over green over blue, like #c0b074
    for (int i : {ImGuiCol_TabSelected, ImGuiCol_TabHovered, ImGuiCol_Header, ImGuiCol_HeaderHovered,
                  ImGuiCol_ScrollbarGrab, ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_TextSelectedBg}) {
        INFO("colour " << ImGui::GetStyleColorName(i));
        CHECK(style.Colors[i].x > style.Colors[i].y);
        CHECK(style.Colors[i].y > style.Colors[i].z);
    }
    CHECK(style.Colors[ImGuiCol_TabSelectedOverline].x == doctest::Approx(0xC0 / 255.0f));
    CHECK(style.Colors[ImGuiCol_WindowBg].x == doctest::Approx(0x08 / 255.0f));
    CHECK(style.Colors[ImGuiCol_WindowBg].z == doctest::Approx(0x0E / 255.0f));
    // the selection stays dark, so the Mods tab's status colours stay readable (and dark on HDR)
    CHECK(style.Colors[ImGuiCol_Header].x < 0.40f);
    // another accent follows
    look.accent = 0x80C080FFu;  // green
    apply_look_theme(style, look);
    CHECK(style.Colors[ImGuiCol_ScrollbarGrab].y > style.Colors[ImGuiCol_ScrollbarGrab].x);
}

TEST_CASE("look theme: the Mods tab's selected row takes the theme's selection colour, not a slate of its own") {
    Headless h;
    const ImVec4 header(0.50f, 0.25f, 0.125f, 1.0f);
    ImGui::GetStyle().Colors[ImGuiCol_Header] = header;
    std::vector<ModRecord> mods(1);
    mods[0].id = mods[0].name = "clock";
    mods[0].source = "clock\\";
    ModsUiState ui;
    ui.selected = "clock\\";
    for (int i = 0; i < 2; ++i) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(1200, 600));
        ImGui::Begin("##mods_theme_test");
        draw_mods_tab(mods, nullptr, ui);
        ImGui::End();
        ImGui::Render();
    }
    bool found = false;
    for (const ImDrawList* dl : ImGui::GetDrawData()->CmdLists)
        for (const ImDrawVert& v : dl->VtxBuffer) found |= v.col == ImGui::ColorConvertFloat4ToU32(header);
    CHECK(found);
}

TEST_CASE("look drawing: the console's height and background opacity follow [overlay] height and opacity") {
    Headless h;
    ConsoleModel model;
    CompletionSources src;
    ConsoleUiState ui;
    ui.open_fraction = 1.0f;
    ui.height = 0.70f;
    ui.opacity = 0.95f;
    ImGui::NewFrame();
    draw_console(model, ui, src, ImVec2(1920, 1080), [](const std::string&) {});
    ImGui::Render();
    ImGuiWindow* w = window("##emf_console");
    REQUIRE(w);
    CHECK(w->Size.y == doctest::Approx(1080.0f * 0.70f));
    ConsoleUiState d;  // the defaults: half the screen, as before
    d.open_fraction = 1.0f;
    CHECK(d.height == doctest::Approx(0.50f));
    CHECK(d.opacity == doctest::Approx(0.80f));
}

TEST_CASE("look drawing: the wheel's lit slice has the gold accent ring and none of the old blue") {
    Headless h;
    WheelModel wheel;
    wheel.open("Blessings", {"God mode", "Endless health", "Endless stamina"}, false, 0, 0);
    wheel.mouse(0.0f, -150.0f);  // up: the first slice
    REQUIRE(wheel.highlighted() == 0);
    TextLook look = look_of(HudStyle::Game);
    look.accent = 0xC0B074FFu;
    ImGui::NewFrame();
    draw_wheel(wheel, ImVec2(1920, 1080), look);
    ImGui::Render();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    CHECK(has_colour(dl, IM_COL32(0xC0, 0xB0, 0x74, 255)));
    CHECK_FALSE(has_colour(dl, IM_COL32(62, 80, 108, 235)));
    CHECK_FALSE(has_colour(dl, IM_COL32(118, 140, 172, 255)));
}

// ---- the fix pass: bold on-screen text, wide and multi-line text, UTF-8 cuts, the wheel at the bigger size ----
#include <cfloat>
#include <functional>

#include "core/overlay/font_plan.h"

namespace {
// The vertical extent of the background draw list's vertices.
float drawn_height() {
    float lo = FLT_MAX, hi = -FLT_MAX;
    for (const ImDrawVert& v : ImGui::GetBackgroundDrawList()->VtxBuffer) {
        lo = (std::min)(lo, v.pos.y);
        hi = (std::max)(hi, v.pos.y);
    }
    return hi > lo ? hi - lo : 0.0f;
}
// One measure per UTF-8 character, 10 px each (continuation bytes are free).
float ten_per_char(const std::string& s) {
    float w = 0.0f;
    for (unsigned char c : s) w += (c & 0xC0) == 0x80 ? 0.0f : 10.0f;
    return w;
}
// 5 px per byte: a cut inside a character is possible (and must not happen).
float five_per_byte(const std::string& s) { return 5.0f * static_cast<float>(s.size()); }
bool whole_utf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}
const char* const kRussian = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB7\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C\xD1\x81\xD0\xBA\xD0\xB5"
                             "\xD0\xBB\xD0\xB5\xD1\x82\xD0\xB0";  // "Призвать скелета" without the space
}  // namespace

TEST_CASE("look drawing: HUD lines are in the bold on-screen font, like the game's own lines") {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    ImFontConfig small_cfg, big_cfg;
    small_cfg.SizePixels = 13.0f;
    big_cfg.SizePixels = 26.0f;
    ImFont* regular = io.Fonts->AddFontDefault(&small_cfg);
    ImFont* bold = io.Fonts->AddFontDefault(&big_cfg);  // a bigger "bold", to tell them apart
    io.Fonts->Build();
    TextLook look = look_of(HudStyle::Plain);
    look.font = regular;
    look.bold = bold;
    hud_vertices({{"x", "Hi", HudPlace::BottomLeft, 0}}, look);
    const float with_bold = drawn_height();
    look.bold = nullptr;  // no bold: the regular font
    hud_vertices({{"x", "Hi", HudPlace::BottomLeft, 0}}, look);
    CHECK(with_bold > drawn_height() * 1.5f);
    ImGui::DestroyContext(ctx);
}

TEST_CASE("look layout: a HUD line wider than the screen starts at the margin, never off the left edge") {
    for (const ImVec2 s : kScreens) {
        const float lh = line_height(s);
        for (const HudPlace place : kPlaces)
            CHECK(hud_line_position(place, 0, 1, ImVec2(s.x * 1.5f, lh), lh, s).x == doctest::Approx(lh));
    }
}

TEST_CASE("look drawing: a HUD line with a line break stays one line high") {
    Headless h;
    hud_vertices({{"x", "Hi Ho", HudPlace::BottomRight, 0}}, look_of(HudStyle::Plain));
    const float one = drawn_height();
    hud_vertices({{"x", "Hi\nHo", HudPlace::BottomRight, 0}}, look_of(HudStyle::Plain));
    CHECK(drawn_height() == doctest::Approx(one));
}

TEST_CASE("look layout: note text wraps at spaces, at most three rows, the last ending in ... when text is left") {
    CHECK(wrap_rows("Sleeping", 200.0f, ten_per_char) == std::vector<std::string>{"Sleeping"});
    CHECK(wrap_rows("aaa bbb ccc", 70.0f, ten_per_char) == std::vector<std::string>{"aaa bbb", "ccc"});
    CHECK(wrap_rows("one\ntwo", 200.0f, ten_per_char) == std::vector<std::string>{"one", "two"});  // a break is a row
    const std::string long_text = "the clock runs sixty times faster while you sleep through the day and night";
    const auto rows = wrap_rows(long_text, 120.0f, ten_per_char);
    REQUIRE(rows.size() == 3);
    for (const auto& r : rows) CHECK(ten_per_char(r) <= 120.0f);
    CHECK(rows.back().size() >= 3);
    CHECK(rows.back().compare(rows.back().size() - 3, 3, "...") == 0);
    // a word wider than a row is cut between characters, never inside one
    const auto cut = wrap_rows(kRussian, 72.0f, five_per_byte, 5);
    REQUIRE(cut.size() >= 2);
    for (const auto& r : cut) {
        CHECK(whole_utf8(r));
        CHECK(five_per_byte(r) <= 72.0f);
    }
    CHECK(wrap_rows("", 100.0f, ten_per_char).empty());
}

TEST_CASE("look drawing: a very long note stays three rows high on its band") {
    Headless h;
    ConsoleModel m;
    const ToastState t{m.version(), 1000};
    std::string text;
    for (int i = 0; i < 60; ++i) text += "word" + std::to_string(i) + " ";
    m.add_line(text);
    ImGui::NewFrame();
    draw_notes(toast_lines(m, t, 0), ImVec2(1920, 1080), look_of(HudStyle::Game));
    ImGui::Render();
    const float row = ImGui::GetFontSize();
    CHECK(drawn_height() <= row * 3.0f * 1.2f + row * 1.0f);  // three rows and the band's padding
    CHECK(drawn_height() >= row * 3.0f);
}

TEST_CASE("look drawing: the wheel cuts a label between UTF-8 characters, with ...") {
    const std::string cut = wheel_fit(kRussian, 72.0f, five_per_byte);
    CHECK(whole_utf8(cut));
    CHECK(five_per_byte(cut) <= 72.0f);
    CHECK(cut.compare(cut.size() - 3, 3, "...") == 0);
    CHECK(wheel_fit("Stay here", 200.0f, ten_per_char) == "Stay here");
}

TEST_CASE("look drawing: the mods' wheel labels still fit in the serif at the bigger on-screen size") {
    ImGuiContext* ctx = ImGui::CreateContext();
    const std::string serif = std::string(EMF_SOURCE_DIR) + "/third_party/fonts/alegreya/Alegreya-Regular.ttf";
    const char* const labels[] = {"God mode (on)", "Endless health (off)", "Stamina (off)", "No fall damage (on)",
                                  "Fill me up", "All off", "Follow me", "Stay here", "Attack (what you look at)",
                                  "Raise Skeleton Warrior (2/3)", "Dismiss all", "Mace (6 Wood Logs, 1 Stone)",
                                  "Summons: Raise Skeleton Archer (1/3)", "Wood Logs (x120)", "Hand over to"};
    for (const int h : {720, 1080, 1481, 2160}) {
        ImFontAtlas atlas;
        ImFont* f = atlas.AddFontFromFileTTF(serif.c_str(), scaled_pixel_size(hud_base_pixel_size(h), 100), nullptr,
                                             kFontRanges);
        REQUIRE(f);
        REQUIRE(atlas.Build());
        const auto measure = [f](const std::string& s) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, s.c_str()).x; };
        const ImVec2 display(h * 16.0f / 9.0f, static_cast<float>(h));
        const float ring = (std::min)(display.x, display.y) * 0.30f * (1.0f - 0.34f);  // the ring's depth
        for (const size_t slices : {size_t(8), size_t(5)}) {
            const float width = wheel_label_width(display, slices);
            int cut = 0;
            for (const char* label : labels) {
                const auto lines = wheel_label_layout(label, width, measure);
                CHECK(lines.size() <= 4);
                CHECK(lines.size() * f->FontSize * 1.1f <= ring);  // the label's lines stay inside the ring
                for (const auto& l : lines) {
                    cut += measure(l.text) > width;
                    CHECK(measure(wheel_fit(l.text, width, measure)) <= width);
                }
            }
            INFO("height " << h << ", " << slices << " slices");
            CHECK(cut <= (slices == 8 ? 1 : 0));  // only "(6 Wood Logs, 1 Stone)" on a full page
        }
    }
    ImGui::DestroyContext(ctx);
}
