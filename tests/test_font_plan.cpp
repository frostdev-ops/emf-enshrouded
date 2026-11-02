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

void put16(std::vector<unsigned char>& v, uint32_t x) {
    v.push_back(static_cast<unsigned char>(x >> 8));
    v.push_back(static_cast<unsigned char>(x));
}
void put32(std::vector<unsigned char>& v, uint32_t x) {
    put16(v, x >> 16);
    put16(v, x & 0xFFFF);
}
void set32(std::vector<unsigned char>& v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[at + i] = static_cast<unsigned char>(x >> (24 - 8 * i));
}
uint32_t get32(const std::vector<unsigned char>& v, size_t at) {
    return (uint32_t(v[at]) << 24) | (uint32_t(v[at + 1]) << 16) | (uint32_t(v[at + 2]) << 8) | v[at + 3];
}

// A small but complete TrueType font: two empty glyphs, its cmap (format 4) maps only `codepoint` to glyph 1.
std::vector<unsigned char> tiny_font(uint16_t codepoint) {
    std::vector<std::pair<const char*, std::vector<unsigned char>>> tables;
    std::vector<unsigned char> cmap;
    for (uint32_t x : {0u, 1u, 3u, 1u}) put16(cmap, x);  // version, one record: Microsoft Unicode BMP
    put32(cmap, 12);
    for (uint32_t x : {4u, 32u, 0u, 4u, 4u, 1u, 0u}) put16(cmap, x);  // format 4, length, language, two segments
    for (uint32_t x : {uint32_t(codepoint), 0xFFFFu, 0u, uint32_t(codepoint), 0xFFFFu}) put16(cmap, x);  // ends, starts
    for (uint32_t x : {(1u - codepoint) & 0xFFFFu, 1u, 0u, 0u}) put16(cmap, x);  // deltas, range offsets
    tables.push_back({"cmap", cmap});
    std::vector<unsigned char> head(54, 0);
    set32(head, 0, 0x00010000);
    set32(head, 12, 0x5F0F3CF5);
    head[18] = 0x03;
    head[19] = 0xE8;  // 1000 units per em; short loca
    tables.push_back({"head", head});
    std::vector<unsigned char> hhea(36, 0);
    set32(hhea, 0, 0x00010000);
    hhea[4] = 0x03;
    hhea[5] = 0x20;  // ascender 800
    hhea[6] = 0xFF;
    hhea[7] = 0x38;  // descender -200
    hhea[35] = 2;    // two metrics
    tables.push_back({"hhea", hhea});
    std::vector<unsigned char> hmtx;
    for (uint32_t x : {500u, 0u, 500u, 0u}) put16(hmtx, x);
    tables.push_back({"hmtx", hmtx});
    std::vector<unsigned char> maxp;
    put32(maxp, 0x00005000);
    put16(maxp, 2);
    tables.push_back({"maxp", maxp});
    tables.push_back({"loca", std::vector<unsigned char>(6, 0)});
    tables.push_back({"glyf", std::vector<unsigned char>(1200, 0)});
    std::vector<unsigned char> font;
    put32(font, 0x00010000);
    for (uint32_t x : {uint32_t(tables.size()), 64u, 2u, 48u}) put16(font, x);
    size_t at = 12 + 16 * tables.size();
    std::vector<unsigned char> data;
    for (const auto& [tag, t] : tables) {
        for (int i = 0; i < 4; ++i) font.push_back(static_cast<unsigned char>(tag[i]));
        put32(font, 0);  // checksum (not checked)
        put32(font, static_cast<uint32_t>(at + data.size()));
        put32(font, static_cast<uint32_t>(t.size()));
        data.insert(data.end(), t.begin(), t.end());
        while (data.size() % 4) data.push_back(0);
    }
    font.insert(font.end(), data.begin(), data.end());
    return font;
}

// `font` inside a TrueType collection of `version` (its tables moved by the 16 bytes of the collection's header).
std::vector<unsigned char> collection_of(std::vector<unsigned char> font, uint32_t version) {
    const uint32_t n = (uint32_t(font[4]) << 8) | font[5];
    for (uint32_t i = 0; i < n; ++i) set32(font, 12 + 16 * i + 8, get32(font, 12 + 16 * i + 8) + 16);
    std::vector<unsigned char> ttc = {'t', 't', 'c', 'f'};
    put32(ttc, version);
    put32(ttc, 1);
    put32(ttc, 16);
    ttc.insert(ttc.end(), font.begin(), font.end());
    return ttc;
}
}  // namespace

TEST_CASE("font check: a font with no glyph in EMF's ranges is refused (ImGui stops on a font with none)") {
    const std::vector<unsigned char> latin = tiny_font('A');
    CHECK(font_bytes_ok(latin.data(), latin.size()));  // the same small font with an 'A' is fine
    const std::vector<unsigned char> cjk = tiny_font(0x4E00);
    CHECK_FALSE(font_bytes_ok(cjk.data(), cjk.size()));
    Headless h;
    Folder d(L"emf_test_fontrange");
    write_file(d / L"Latin.ttf", latin);
    write_file(d / L"Cjk.ttf", cjk);
    ImFontAtlas atlas;
    FontPlan plan;
    plan.faces = {{FontRole::Console, d / L"Latin.ttf", 20.0f}, {FontRole::Tabs, d / L"Cjk.ttf", 20.0f},
                  {FontRole::Hud, d / L"Cjk.ttf", 26.0f}, {FontRole::HudBold, d / L"Latin.ttf", 26.0f}};
    std::vector<std::string> problems;
    const OverlayFonts f = build_font_atlas(atlas, plan, &problems);  // never stops at ImGui's "no glyph" assert
    CHECK(atlas.IsBuilt());
    REQUIRE(f.console);
    CHECK(f.console->FindGlyphNoFallback('A') != nullptr);
    CHECK(problems.size() == 1);  // Cjk.ttf, once for both faces that wanted it
}

TEST_CASE("font check: a font collection is read as ImGui reads it: versions 1 and 2, not others") {
    const std::vector<unsigned char> real = bytes_of(source_font(L"alegreya\\Alegreya-Regular.ttf"));
    const auto v1 = collection_of(real, 0x00010000);
    CHECK(font_bytes_ok(v1.data(), v1.size()));
    const auto v2 = collection_of(real, 0x00020000);
    CHECK(font_bytes_ok(v2.data(), v2.size()));
    const auto v3 = collection_of(real, 0x00030000);  // stb_truetype finds no font in it: ImGui would stop
    CHECK_FALSE(font_bytes_ok(v3.data(), v3.size()));
}

TEST_CASE("font atlas: a build that fails inside ImGui is caught, never a crash") {
    Headless h;
    ImFontAtlas atlas;
    static const ImWchar kBackwards[] = {0x0050, 0x0020, 0};  // ImGui asserts on a range that runs backwards
    ImFontConfig cfg;
    cfg.SizePixels = 13.0f;
    cfg.GlyphRanges = kBackwards;
    atlas.AddFontDefault(&cfg);
    std::string why;
    CHECK_FALSE(guarded_atlas_build(atlas, &why));
    CHECK(why.find("Invalid range") != std::string::npos);  // ImGui's own words, for the log
    // handled here: it is not left behind as the reason the next, unrelated overlay failure would give
    CHECK(emf_imgui_last_assert()[0] == '\0');
    atlas.Clear();
    atlas.AddFontDefault();
    CHECK(guarded_atlas_build(atlas));
    CHECK(atlas.IsBuilt());
}

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

TEST_CASE("font plan: on-screen text has its own, bigger base: the screen height / 42 within 16-52 px") {
    CHECK(hud_base_pixel_size(1080) == doctest::Approx(25.714f).epsilon(0.001));
    CHECK(hud_base_pixel_size(720) == doctest::Approx(17.143f).epsilon(0.001));
    CHECK(hud_base_pixel_size(1481) == doctest::Approx(35.262f).epsilon(0.001));
    CHECK(hud_base_pixel_size(2160) == doctest::Approx(51.429f).epsilon(0.001));
    CHECK(hud_base_pixel_size(480) == doctest::Approx(16.0f));   // never under 16
    CHECK(hud_base_pixel_size(4320) == doctest::Approx(52.0f));  // never over 52
    // the sizes the faces get: on-screen 17 / 26 / 35 / 51 px, the console 13 / 20 / 27 / 40 px
    const int heights[] = {720, 1080, 1481, 2160};
    const float hud[] = {17.0f, 26.0f, 35.0f, 51.0f};
    const float text[] = {13.0f, 20.0f, 27.0f, 40.0f};
    for (int i = 0; i < 4; ++i) {
        const FontSizes sz = font_sizes(LookSettings{}, heights[i]);
        CHECK(sz.hud == hud[i]);
        CHECK(sz.text == text[i]);
    }
    LookSettings big;
    big.hud_size = 200;
    big.text_size = 60;
    CHECK(font_sizes(big, 2160).hud == 103.0f);
    CHECK(font_sizes(big, 720).text == 8.0f);
    // style=boxed is EMF's earlier look: its on-screen text is the console's size
    LookSettings boxed;
    boxed.style = HudStyle::Boxed;
    CHECK(font_sizes(boxed, 1080).hud == 20.0f);
}

TEST_CASE("font plan: by default the console is Consolas, the tabs the bundled serif, on-screen text the serif and bold") {
    const FontPlan p = plan_fonts(LookSettings{}, 1080, kPaths, Files{});
    REQUIRE(p.faces.size() == 4);
    CHECK(p.pixel_size == doctest::Approx(20.0f));
    CHECK(p.face(FontRole::Console).file == kConsolas);
    CHECK(p.face(FontRole::Tabs).file == kSerif);
    CHECK(p.face(FontRole::Hud).file == kSerif);
    CHECK(p.face(FontRole::HudBold).file == kBold);
    CHECK(p.face(FontRole::Console).pixel_size == 20.0f);
    CHECK(p.face(FontRole::Tabs).pixel_size == 20.0f);
    CHECK(p.face(FontRole::Hud).pixel_size == 26.0f);
    CHECK(p.face(FontRole::HudBold).pixel_size == 26.0f);
    CHECK(p.problems.empty());
    // boxed with the console's font: one face for the console and the screen, as before
    LookSettings boxed;
    boxed.style = HudStyle::Boxed;
    boxed.hud_font = {FontChoice::Kind::Console, {}};
    const FontPlan b = plan_fonts(boxed, 1080, kPaths, Files{});
    CHECK(b.face(FontRole::Hud) == FontFace{FontRole::Hud, kConsolas, 20.0f});
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
    CHECK(p.face(FontRole::Hud).pixel_size == 39.0f);  // 150 % of the on-screen base (25.7 px)
    CHECK(p.face(FontRole::HudBold).file == kCascadia);  // no bold of it: the same face
    CHECK(p.face(FontRole::HudBold).pixel_size == 39.0f);

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

TEST_CASE("font rebuild: at once the first time; a new size once it held for 250 ms; never for the same sizes") {
    const LookSettings look;
    const FontSizes a = font_sizes(look, 1080), b = font_sizes(look, 1481), c = font_sizes(look, 1440);
    FontRebuild r;
    CHECK(r.due(a, 0));  // the first atlas: at once
    r.built(a);
    CHECK_FALSE(r.due(a, 10));
    CHECK(font_sizes(look, 1081) == a);  // a pixel more or less: the same faces, nothing to build
    CHECK_FALSE(r.due(font_sizes(look, 1081), 20));
    // a new size waits until the window stopped changing
    CHECK_FALSE(r.due(b, 1000));
    CHECK_FALSE(r.due(b, 1200));
    CHECK(r.due(b, 1250));
    CHECK(r.due(b, 1260));  // still due until it is built
    r.built(b);
    CHECK_FALSE(r.due(b, 1300));
    // dragging the window's edge: each new size starts the wait again; back to the built size: nothing to do
    CHECK_FALSE(r.due(c, 2000));
    CHECK_FALSE(r.due(a, 2200));
    CHECK_FALSE(r.due(a, 2400));
    CHECK(r.due(a, 2450));
    CHECK_FALSE(r.due(b, 2460));  // back to b (built) before a was built: nothing
    CHECK_FALSE(r.due(b, 3000));
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
    CHECK(atlas.TexWidth == 4096);  // a wide atlas: big text on 4K stays a few rows high
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
