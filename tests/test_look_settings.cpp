// The overlay's look ([hud] and the look keys of [overlay] in emf_core.ini), read from any key-value source.
#include <doctest.h>

#include <windows.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/overlay/look_settings.h"
#include "core/overlay/overlay.h"
#include "test_scratch.h"

using namespace emf::core;

namespace {
// An ini as a map: {"hud.size", "120"}; a key that is not there reads "".
struct FakeIni {
    std::map<std::string, std::string> values;
    std::string operator()(const char* section, const char* key) const {
        const auto it = values.find(std::string(section) + "." + key);
        return it == values.end() ? std::string() : it->second;
    }
};

LookSettings read(const std::map<std::string, std::string>& values, std::vector<std::string>* problems = nullptr) {
    const FakeIni ini{values};
    return read_look_settings(ini, problems);
}

bool mentions(const std::vector<std::string>& problems, const std::string& what) {
    for (const auto& p : problems)
        if (p.find(what) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST_CASE("look settings: the defaults when nothing is set (game style, serif, warm white, notes top centre)") {
    std::vector<std::string> problems;
    const LookSettings s = read({}, &problems);
    CHECK(problems.empty());
    CHECK(s.style == HudStyle::Game);
    CHECK(s.hud_font.kind == FontChoice::Kind::Serif);
    CHECK(s.hud_size == 100);
    CHECK(s.hud_colour == 0xFFFFF4FFu);
    CHECK(s.shadow);
    CHECK(s.notes_place == NotesPlace::TopCentre);
    CHECK(s.notes_seconds == 6);
    CHECK(s.console_font.kind == FontChoice::Kind::Consolas);
    CHECK(s.tabs_font.kind == FontChoice::Kind::Serif);
    CHECK(s.text_size == 100);
    CHECK(s.height == 50);
    CHECK(s.opacity == 80);
    CHECK(s.background == 0x080A0EFFu);
    CHECK(s.accent == 0xC0B074FFu);
    CHECK(s == LookSettings{});  // the struct's own defaults are the same
}

TEST_CASE("look settings: every key is read") {
    std::vector<std::string> problems;
    const LookSettings s = read({{"hud.style", "boxed"},
                                 {"hud.font", "console"},
                                 {"hud.size", "120"},
                                 {"hud.colour", "#ffcc00"},
                                 {"hud.shadow", "0"},
                                 {"hud.notes_place", "right"},
                                 {"hud.notes_seconds", "10"},
                                 {"overlay.font", "cascadia"},
                                 {"overlay.tabs_font", "consolas"},
                                 {"overlay.text_size", "80"},
                                 {"overlay.height", "70"},
                                 {"overlay.opacity", "95"},
                                 {"overlay.background", "#101820"},
                                 {"overlay.accent", "#d4af37"}},
                                &problems);
    CHECK(problems.empty());
    CHECK(s.style == HudStyle::Boxed);
    CHECK(s.hud_font.kind == FontChoice::Kind::Console);
    CHECK(s.hud_size == 120);
    CHECK(s.hud_colour == 0xFFCC00FFu);
    CHECK_FALSE(s.shadow);
    CHECK(s.notes_place == NotesPlace::Right);
    CHECK(s.notes_seconds == 10);
    CHECK(s.console_font.kind == FontChoice::Kind::Cascadia);
    CHECK(s.tabs_font.kind == FontChoice::Kind::Consolas);
    CHECK(s.text_size == 80);
    CHECK(s.height == 70);
    CHECK(s.opacity == 95);
    CHECK(s.background == 0x101820FFu);
    CHECK(s.accent == 0xD4AF37FFu);

    const LookSettings t = read({{"hud.style", "Plain"},
                                 {"hud.font", "file:My Book.ttf"},
                                 {"hud.notes_place", "TOP_LEFT"},
                                 {"hud.shadow", "on"},
                                 {"overlay.font", "serif"},
                                 {"overlay.tabs_font", "file:Runes.otf"}},
                                &problems);
    CHECK(problems.empty());
    CHECK(t.style == HudStyle::Plain);
    CHECK(t.hud_font.kind == FontChoice::Kind::File);
    CHECK(t.hud_font.file == "My Book.ttf");
    CHECK(t.notes_place == NotesPlace::TopLeft);
    CHECK(t.shadow);
    CHECK(t.console_font.kind == FontChoice::Kind::Serif);
    CHECK(t.tabs_font.kind == FontChoice::Kind::File);
    CHECK(t.tabs_font.file == "Runes.otf");
    CHECK(read({{"hud.notes_place", "top_center"}}).notes_place == NotesPlace::TopCentre);  // either spelling
    CHECK(read({{"hud.font", "file:a.ttf"}, {"overlay.font", "file:b.TTF"}}).console_font.file == "b.TTF");
}

TEST_CASE("look settings: a bad value falls back to the default and says so in one line") {
    std::vector<std::string> problems;
    const LookSettings s = read({{"hud.style", "fancy"},
                                 {"hud.font", "comic"},
                                 {"hud.size", "big"},
                                 {"hud.colour", "#12345"},
                                 {"hud.shadow", "maybe"},
                                 {"hud.notes_place", "bottom"},
                                 {"hud.notes_seconds", "6.5"},
                                 {"overlay.font", "console"},  // the console's own font cannot be "the console's font"
                                 {"overlay.tabs_font", "file:..\\x.ttf"},
                                 {"overlay.background", "navy"},
                                 {"overlay.accent", "#gggggg"}},
                                &problems);
    CHECK(s.style == HudStyle::Game);
    CHECK(s.hud_font.kind == FontChoice::Kind::Serif);
    CHECK(s.hud_size == 100);
    CHECK(s.hud_colour == 0xFFFFF4FFu);
    CHECK(s.shadow);
    CHECK(s.notes_place == NotesPlace::TopCentre);
    CHECK(s.notes_seconds == 6);
    CHECK(s.console_font.kind == FontChoice::Kind::Consolas);
    CHECK(s.tabs_font.kind == FontChoice::Kind::Serif);
    CHECK(s.background == 0x080A0EFFu);
    CHECK(s.accent == 0xC0B074FFu);
    CHECK(problems.size() == 11);  // one line per bad value
    CHECK(mentions(problems, "[hud] style=fancy"));
    CHECK(mentions(problems, "[hud] size=big"));
    CHECK(mentions(problems, "[overlay] accent=#gggggg"));
    for (const auto& p : problems) CHECK(p.find("default") != std::string::npos);  // says what is used instead
    // a font file must be a plain name in emf\fonts, a .ttf or .otf
    for (const char* bad : {"file:", "file:sub\\a.ttf", "file:a/b.ttf", "file:C:a.ttf", "file:notes.txt", "file:.ttf"}) {
        std::vector<std::string> p;
        CHECK(read({{"hud.font", bad}}, &p).hud_font.kind == FontChoice::Kind::Serif);
        CHECK(p.size() == 1);
    }
    // nothing set is not a problem; neither is a key set to nothing
    problems.clear();
    read({{"hud.size", ""}, {"overlay.accent", "   "}}, &problems);
    CHECK(problems.empty());
}

TEST_CASE("look settings: colours are #rgb or #rrggbb, with or without the #, any case") {
    CHECK(read({{"hud.colour", "#fff"}}).hud_colour == 0xFFFFFFFFu);
    CHECK(read({{"hud.colour", "#c0b"}}).hud_colour == 0xCC00BBFFu);
    CHECK(read({{"hud.colour", "#C0B074"}}).hud_colour == 0xC0B074FFu);
    CHECK(read({{"hud.colour", "1b1c22"}}).hud_colour == 0x1B1C22FFu);
    CHECK(read({{"overlay.background", "#000"}}).background == 0x000000FFu);
    std::vector<std::string> problems;
    for (const char* bad : {"#", "#ff", "#ffff", "#fffffff", "#ff00ff00", "red", "#12 456"}) {
        problems.clear();
        CHECK(read({{"hud.colour", bad}}, &problems).hud_colour == 0xFFFFF4FFu);
        CHECK(problems.size() == 1);
    }
}

TEST_CASE("look settings: a '; comment' after a value and the spaces around it are not part of it") {
    std::vector<std::string> problems;
    const LookSettings s = read({{"hud.size", "120   ; bigger"},
                                 {"hud.colour", "#ffcc00 ; gold"},
                                 {"hud.style", "\tplain\t; no shadow"},
                                 {"hud.font", "file:My Book.ttf ; my own"},
                                 {"overlay.height", " 60 "},
                                 {"overlay.accent", "; only a comment"}},
                                &problems);
    CHECK(problems.empty());
    CHECK(s.hud_size == 120);
    CHECK(s.hud_colour == 0xFFCC00FFu);
    CHECK(s.style == HudStyle::Plain);
    CHECK(s.hud_font.file == "My Book.ttf");
    CHECK(s.height == 60);
    CHECK(s.accent == 0xC0B074FFu);  // nothing before the comment: the default, quietly
}

TEST_CASE("look settings: numbers out of range are kept inside it, and that is said; a % sign is fine") {
    std::vector<std::string> problems;
    const LookSettings s = read({{"hud.size", "300"},
                                 {"hud.notes_seconds", "1"},
                                 {"overlay.text_size", "40"},
                                 {"overlay.height", "95"},
                                 {"overlay.opacity", "10"}},
                                &problems);
    CHECK(s.hud_size == 200);
    CHECK(s.notes_seconds == 2);
    CHECK(s.text_size == 60);
    CHECK(s.height == 90);
    CHECK(s.opacity == 30);
    CHECK(problems.size() == 5);
    CHECK(mentions(problems, "[hud] size=300"));
    CHECK(mentions(problems, "200"));
    problems.clear();
    const LookSettings t = read({{"hud.size", "150%"}, {"overlay.text_size", "60"}, {"hud.notes_seconds", "20"},
                                 {"overlay.height", "-5"}},
                                &problems);
    CHECK(t.hud_size == 150);
    CHECK(t.text_size == 60);
    CHECK(t.notes_seconds == 20);
    CHECK(t.height == 25);
    CHECK(problems.size() == 1);  // only the -5
}

TEST_CASE("read_overlay_config: the look comes from emf_core.ini's [hud] and [overlay], with its problems") {
    const std::wstring ini = test_scratch::temp_path(L"emf_test_look", L".ini");
    DeleteFileW(ini.c_str());
    OverlayConfig c = read_overlay_config(ini);
    CHECK(c.look == LookSettings{});
    CHECK(c.look_problems.empty());
    WritePrivateProfileStringW(L"hud", L"size", L"130 ; bigger", ini.c_str());
    WritePrivateProfileStringW(L"hud", L"font", L"file:Bücher.ttf", ini.c_str());  // a name beyond ASCII
    WritePrivateProfileStringW(L"overlay", L"accent", L"#abc", ini.c_str());
    WritePrivateProfileStringW(L"overlay", L"height", L"lots", ini.c_str());
    c = read_overlay_config(ini);
    CHECK(c.look.hud_size == 130);
    CHECK(c.look.hud_font.kind == FontChoice::Kind::File);
    CHECK(c.look.hud_font.file == "B\xC3\xBC" "cher.ttf");  // UTF-8
    CHECK(c.look.accent == 0xAABBCCFFu);
    CHECK(c.look.height == 50);
    REQUIRE(c.look_problems.size() == 1);
    CHECK(c.look_problems[0].find("[overlay] height=lots") != std::string::npos);
    DeleteFileW(ini.c_str());
}
