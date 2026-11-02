// The overlay's fonts: which file each face uses (the fallback chain), their pixel sizes, the check that refuses a file
// that is not a font, the rebuild on a new window size, and the ImGui atlas built from a plan (headless).
#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "core/overlay/font_plan.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "test_scratch.h"

using namespace emf::core;

namespace {
const FontPaths kPaths{L"G:\\game\\emf\\fonts", L"C:\\Windows\\Fonts\\consola.ttf"};
const std::wstring kSerif = L"G:\\game\\emf\\fonts\\Alegreya-Regular.ttf";
const std::wstring kBold = L"G:\\game\\emf\\fonts\\Alegreya-Bold.ttf";
const std::wstring kCascadia = L"G:\\game\\emf\\fonts\\CascadiaMono-Regular.ttf";
const std::wstring kConsolas = L"C:\\Windows\\Fonts\\consola.ttf";

// file_ok for a PC that has every file except those in `missing`.
struct Files {
    std::set<std::wstring> missing;
    bool operator()(const std::wstring& path) const { return !missing.count(path); }
};

std::wstring source_font(const wchar_t* rel) {
    const std::string src = EMF_SOURCE_DIR;
    return std::wstring(src.begin(), src.end()) + L"\\third_party\\fonts\\" + rel;
}

std::vector<unsigned char> bytes_of(const std::wstring& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void write_file(const std::wstring& path, const std::vector<unsigned char>& data) {
    std::ofstream f(std::filesystem::path(path), std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

// A folder for one test, removed when it ends.
struct Folder {
    std::wstring dir;
    explicit Folder(const wchar_t* name) : dir(test_scratch::temp_path(name)) {
        std::filesystem::create_directories(dir);
    }
    ~Folder() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::wstring operator/(const wchar_t* name) const { return dir + L"\\" + name; }
};

struct Headless {
    ImGuiContext* ctx;
    Headless() { ctx = ImGui::CreateContext(); }
    ~Headless() { ImGui::DestroyContext(ctx); }
};
}  // namespace

TEST_CASE("font plan: the base size is the screen height / 54 within 13-40 px; size % scales it to whole pixels") {
    CHECK(base_pixel_size(1080) == doctest::Approx(20.0f));
    CHECK(base_pixel_size(720) == doctest::Approx(13.333f).epsilon(0.001));
    CHECK(base_pixel_size(2160) == doctest::Approx(40.0f));
    CHECK(base_pixel_size(480) == doctest::Approx(13.0f));   // small windows: never under 13
    CHECK(base_pixel_size(4320) == doctest::Approx(40.0f));  // 8K: never over 40
    CHECK(base_pixel_size(0) == doctest::Approx(13.0f));
    CHECK(scaled_pixel_size(20.0f, 100) == 20.0f);
    CHECK(scaled_pixel_size(20.0f, 60) == 12.0f);
    CHECK(scaled_pixel_size(20.0f, 200) == 40.0f);
    CHECK(scaled_pixel_size(base_pixel_size(720), 100) == 13.0f);
    CHECK(scaled_pixel_size(base_pixel_size(720), 60) == 8.0f);
    CHECK(scaled_pixel_size(base_pixel_size(2160), 60) == 24.0f);
    CHECK(scaled_pixel_size(base_pixel_size(2160), 200) == 80.0f);
    CHECK(scaled_pixel_size(13.0f, 10) == 8.0f);  // never under 8 px
    CHECK(scaled_pixel_size(base_pixel_size(1080), 150) == 30.0f);
}

TEST_CASE("font plan: by default the console is Consolas, the tabs and the HUD the bundled serif, bold for titles") {
    const FontPlan p = plan_fonts(LookSettings{}, 1080, kPaths, Files{});
    REQUIRE(p.faces.size() == 4);
    CHECK(p.pixel_size == doctest::Approx(20.0f));
    CHECK(p.face(FontRole::Console).file == kConsolas);
    CHECK(p.face(FontRole::Tabs).file == kSerif);
    CHECK(p.face(FontRole::Hud).file == kSerif);
    CHECK(p.face(FontRole::HudBold).file == kBold);
    for (const auto& f : p.faces) CHECK(f.pixel_size == 20.0f);
    CHECK(p.problems.empty());
}

TEST_CASE("font plan: each font value picks its file; the sizes follow text_size and size") {
    LookSettings s;
    s.console_font = {FontChoice::Kind::Cascadia, {}};
    s.tabs_font = {FontChoice::Kind::Consolas, {}};
    s.hud_font = {FontChoice::Kind::Console, {}};
    s.text_size = 80;
    s.hud_size = 150;
    FontPlan p = plan_fonts(s, 1080, kPaths, Files{});
    CHECK(p.face(FontRole::Console).file == kCascadia);
    CHECK(p.face(FontRole::Console).pixel_size == 16.0f);
    CHECK(p.face(FontRole::Tabs).file == kConsolas);
    CHECK(p.face(FontRole::Tabs).pixel_size == 16.0f);
    CHECK(p.face(FontRole::Hud).file == kCascadia);  // "console": the console's font
    CHECK(p.face(FontRole::Hud).pixel_size == 30.0f);
    CHECK(p.face(FontRole::HudBold).file == kCascadia);  // no bold of it: the same face
    CHECK(p.face(FontRole::HudBold).pixel_size == 30.0f);

    s.console_font = {FontChoice::Kind::Serif, {}};
    s.hud_font = {FontChoice::Kind::File, "My Book.ttf"};
    s.tabs_font = {FontChoice::Kind::File, "Runes.otf"};
    p = plan_fonts(s, 1080, kPaths, Files{});
    CHECK(p.face(FontRole::Console).file == kSerif);
    CHECK(p.face(FontRole::Hud).file == L"G:\\game\\emf\\fonts\\My Book.ttf");
    CHECK(p.face(FontRole::HudBold).file == L"G:\\game\\emf\\fonts\\My Book.ttf");
    CHECK(p.face(FontRole::Tabs).file == L"G:\\game\\emf\\fonts\\Runes.otf");

    s.console_font = {FontChoice::Kind::File, "Mono.ttf"};
    s.hud_font = {FontChoice::Kind::Console, {}};
    p = plan_fonts(s, 1080, kPaths, Files{});
    CHECK(p.face(FontRole::Console).file == L"G:\\game\\emf\\fonts\\Mono.ttf");
    CHECK(p.face(FontRole::Hud).file == L"G:\\game\\emf\\fonts\\Mono.ttf");
}

TEST_CASE("font plan: a missing file falls back along the chain (bundled, then Consolas, then ImGui's own), said once") {
    LookSettings s;
    // no Consolas (Proton, Wine): the console takes the bundled Cascadia Mono
    FontPlan p = plan_fonts(s, 1080, kPaths, Files{{kConsolas}});
    CHECK(p.face(FontRole::Console).file == kCascadia);
    REQUIRE(p.problems.size() == 1);
    CHECK(p.problems[0].find("consola.ttf") != std::string::npos);
    // neither: ImGui's built-in font
    p = plan_fonts(s, 1080, kPaths, Files{{kConsolas, kCascadia}});
    CHECK(p.face(FontRole::Console).file.empty());
    CHECK(p.problems.size() == 2);
    // no bundled serif: Consolas for the HUD and the tabs; no bold: the regular serif
    p = plan_fonts(s, 1080, kPaths, Files{{kSerif}});
    CHECK(p.face(FontRole::Hud).file == kConsolas);
    CHECK(p.face(FontRole::Tabs).file == kConsolas);
    CHECK(p.face(FontRole::HudBold).file == kBold);
    CHECK(p.problems.size() == 1);  // the same missing file is said once, not once per face
    p = plan_fonts(s, 1080, kPaths, Files{{kBold}});
    CHECK(p.face(FontRole::HudBold).file == kSerif);
    // a player's own font that is not there: the HUD takes the bundled serif, the console Cascadia Mono
    s.hud_font = {FontChoice::Kind::File, "Gone.ttf"};
    s.console_font = {FontChoice::Kind::File, "AlsoGone.ttf"};
    p = plan_fonts(s, 1080, kPaths,
                   Files{{L"G:\\game\\emf\\fonts\\Gone.ttf", L"G:\\game\\emf\\fonts\\AlsoGone.ttf"}});
    CHECK(p.face(FontRole::Hud).file == kSerif);
    CHECK(p.face(FontRole::HudBold).file == kSerif);  // a file font has no bold, and its fallback keeps it regular
    CHECK(p.face(FontRole::Console).file == kCascadia);
    CHECK(p.problems.size() == 2);
    // nothing at all
    p = plan_fonts(LookSettings{}, 1080, kPaths, Files{{kConsolas, kCascadia, kSerif, kBold}});
    for (const auto& f : p.faces) CHECK(f.file.empty());
}

TEST_CASE("font check: the bundled fonts pass; garbage, empty, cut-off, too big or missing files are refused") {
    const std::wstring serif = source_font(L"alegreya\\Alegreya-Regular.ttf");
    CHECK(font_file_ok(serif));
    CHECK(font_file_ok(source_font(L"alegreya\\Alegreya-Bold.ttf")));
    CHECK(font_file_ok(source_font(L"cascadia\\CascadiaMono-Regular.ttf")));
    CHECK_FALSE(font_file_ok(serif, 1000));  // over the size limit
    Folder d(L"emf_test_fontcheck");
    CHECK_FALSE(font_file_ok(d / L"missing.ttf"));
    write_file(d / L"empty.ttf", {});
    CHECK_FALSE(font_file_ok(d / L"empty.ttf"));
    std::vector<unsigned char> garbage(4096);
    for (size_t i = 0; i < garbage.size(); ++i) garbage[i] = static_cast<unsigned char>((i * 7919u) ^ (i >> 3));
    write_file(d / L"garbage.ttf", garbage);
    CHECK_FALSE(font_file_ok(d / L"garbage.ttf"));
    write_file(d / L"text.ttf", std::vector<unsigned char>({'h', 'e', 'l', 'l', 'o'}));
    CHECK_FALSE(font_file_ok(d / L"text.ttf"));
    const std::vector<unsigned char> real = bytes_of(serif);
    REQUIRE(real.size() > 10000);
    write_file(d / L"cut.ttf", std::vector<unsigned char>(real.begin(), real.begin() + 2000));  // the tables are gone
    CHECK_FALSE(font_file_ok(d / L"cut.ttf"));
    std::vector<unsigned char> header_only(real.begin(), real.begin() + 12);  // a font's header and nothing else
    write_file(d / L"header.ttf", header_only);
    CHECK_FALSE(font_file_ok(d / L"header.ttf"));
    std::vector<unsigned char> bad_table = real;  // the first table claims to reach past the end of the file
    bad_table[12 + 12] = 0x7F;
    write_file(d / L"badtable.ttf", bad_table);
    CHECK_FALSE(font_file_ok(d / L"badtable.ttf"));
    std::vector<unsigned char> collection = {'t', 't', 'c', 'f', 0, 1, 0, 0, 0, 0, 0, 1, 0x7F, 0, 0, 0};  // points away
    write_file(d / L"collection.ttc", collection);
    CHECK_FALSE(font_file_ok(d / L"collection.ttc"));
}

TEST_CASE("font plan: a player's garbage font file is refused by the check and the HUD falls back to the serif") {
    Folder d(L"emf_test_fontplan");
    std::vector<unsigned char> garbage(2048, 0x5A);
    garbage[0] = 0;
    garbage[1] = 1;  // starts like a TrueType font, then nothing a font has
    write_file(d / L"Bad.ttf", garbage);
    write_file(d / L"Alegreya-Regular.ttf", bytes_of(source_font(L"alegreya\\Alegreya-Regular.ttf")));
    LookSettings s;
    s.hud_font = {FontChoice::Kind::File, "Bad.ttf"};
    const FontPaths paths{d.dir, d / L"no-consolas.ttf"};
    const FontPlan p = plan_fonts(s, 1080, paths, [](const std::wstring& f) { return font_file_ok(f); });
    CHECK(p.face(FontRole::Hud).file == d / L"Alegreya-Regular.ttf");
    bool said = false;
    for (const auto& line : p.problems) said |= line.find("Bad.ttf") != std::string::npos;
    CHECK(said);
}

TEST_CASE("font rebuild: due the first time and once per new size, never again for the same size") {
    FontRebuild r;
    CHECK(r.due(20.0f));
    CHECK_FALSE(r.due(20.0f));
    CHECK_FALSE(r.due(20.0f));
    CHECK(r.due(base_pixel_size(1440)));
    CHECK_FALSE(r.due(base_pixel_size(1440)));
    CHECK(r.due(20.0f));  // back to the first size: built again
    CHECK_FALSE(r.due(20.0f));
    // heights that clamp to the same size need no rebuild
    FontRebuild clamped;
    CHECK(clamped.due(base_pixel_size(600)));
    CHECK_FALSE(clamped.due(base_pixel_size(500)));
}

TEST_CASE("font atlas: the plan's faces with Latin, Latin Extended-A and Cyrillic; one font per file and size") {
    Headless h;
    ImFontAtlas atlas;
    FontPlan plan;
    plan.pixel_size = 20.0f;
    plan.faces = {{FontRole::Console, source_font(L"cascadia\\CascadiaMono-Regular.ttf"), 20.0f},
                  {FontRole::Tabs, source_font(L"alegreya\\Alegreya-Regular.ttf"), 20.0f},
                  {FontRole::Hud, source_font(L"alegreya\\Alegreya-Regular.ttf"), 20.0f},
                  {FontRole::HudBold, source_font(L"alegreya\\Alegreya-Bold.ttf"), 20.0f}};
    std::vector<std::string> problems;
    const OverlayFonts f = build_font_atlas(atlas, plan, &problems);
    CHECK(problems.empty());
    REQUIRE(f.console);
    REQUIRE(f.hud);
    REQUIRE(f.hud_bold);
    CHECK(f.tabs == f.hud);  // the same file at the same size: one font
    CHECK(f.console != f.hud);
    CHECK(f.hud_bold != f.hud);
    CHECK(atlas.Fonts.Size == 3);
    CHECK(atlas.IsBuilt());
    CHECK(f.hud->FontSize == 20.0f);
    for (ImWchar c : {ImWchar('A'), ImWchar(0xE9) /* e acute */, ImWchar(0x141) /* L stroke */,
                      ImWchar(0x416) /* Zhe */}) {
        CHECK(f.hud->FindGlyphNoFallback(c) != nullptr);
        CHECK(f.console->FindGlyphNoFallback(c) != nullptr);
    }
    CHECK(f.hud->FindGlyphNoFallback(0x4E00) == nullptr);  // no CJK in the ranges
    // the HUD at another size is its own font
    plan.faces[2].pixel_size = 30.0f;
    const OverlayFonts g = build_font_atlas(atlas, plan, &problems);  // a rebuild clears what was there
    CHECK(atlas.Fonts.Size == 4);
    CHECK(g.hud != g.tabs);
    CHECK(g.hud->FontSize == 30.0f);
}

TEST_CASE("font atlas: a file that cannot be loaded becomes ImGui's built-in font, with a line, never a crash") {
    Headless h;
    Folder d(L"emf_test_fontatlas");
    std::vector<unsigned char> garbage(5000, 0x33);
    garbage[0] = 0;
    garbage[1] = 1;
    garbage[2] = 0;
    garbage[3] = 0;
    write_file(d / L"Broken.ttf", garbage);
    ImFontAtlas atlas;
    FontPlan plan;
    plan.pixel_size = 20.0f;
    plan.faces = {{FontRole::Console, L"", 20.0f},  // planned as the built-in font: no line
                  {FontRole::Tabs, d / L"Broken.ttf", 20.0f},
                  {FontRole::Hud, d / L"Gone.ttf", 26.0f},
                  {FontRole::HudBold, source_font(L"alegreya\\Alegreya-Bold.ttf"), 26.0f}};
    std::vector<std::string> problems;
    const OverlayFonts f = build_font_atlas(atlas, plan, &problems);
    CHECK(atlas.IsBuilt());
    REQUIRE(f.console);
    REQUIRE(f.tabs);
    REQUIRE(f.hud);
    CHECK(f.tabs->FontSize == 20.0f);
    CHECK(f.hud->FontSize == 26.0f);
    CHECK(f.hud_bold->FindGlyphNoFallback(0x416) != nullptr);
    CHECK(problems.size() == 2);  // Broken.ttf and Gone.ttf
}
