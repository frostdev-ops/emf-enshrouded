// EMF's own settings (in-game look phase 2): the Settings tab's defs under the reserved id "emf", their pages, live or
// next start, defaults equal to what the overlay uses with an empty emf_core.ini, the font list, the reserved id.
#include <doctest.h>

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "core/emf_settings.h"
#include "core/manifest.h"
#include "core/overlay/look_settings.h"
#include "test_scratch.h"

using namespace emf::core;

namespace {
const SettingDef* find_def(const std::vector<SettingDef>& defs, const std::string& key) {
    for (const auto& d : defs)
        if (d.key == key) return &d;
    return nullptr;
}
}  // namespace

TEST_CASE("emf settings: every key of the spec, on its page, live or next start") {
    const auto defs = emf_setting_defs({});
    std::vector<std::string> keys;
    for (const auto& d : defs) keys.push_back(d.key);
    CHECK(keys == std::vector<std::string>{
                      "hud.style", "hud.font", "hud.size", "hud.colour", "hud.shadow", "hud.notes_place",
                      "hud.notes_seconds", "overlay.key", "overlay.mouse_speed", "overlay.chat", "overlay.font",
                      "overlay.tabs_font", "overlay.text_size", "overlay.height", "overlay.opacity",
                      "overlay.background", "overlay.accent", "overlay.colour_command", "overlay.colour_reply",
                      "overlay.colour_error", "overlay.colour_success", "overlay.suggest", "overlay.suggest_rows",
                      "console.window", "backup.on_start", "backup.keep", "backup.min_minutes"});
    const std::vector<std::string> next_start = {"overlay.key",    "overlay.chat", "console.window",
                                                 "backup.on_start", "backup.keep", "backup.min_minutes"};
    for (const auto& d : defs) {
        CAPTURE(d.key);
        const bool next = std::find(next_start.begin(), next_start.end(), d.key) != next_start.end();
        CHECK(d.live == !next);
        const std::string page = d.key.rfind("hud.", 0) == 0       ? kPageText
                                 : d.key.rfind("overlay.", 0) == 0 ? kPageConsole
                                                                   : kPageStart;
        CHECK(d.group == page);
        CHECK_FALSE(d.label.empty());
        SettingDef copy = d;  // each one is a valid definition with a valid default
        CHECK(normalise_setting(copy).empty());
    }
    CHECK(find_def(defs, "overlay.enabled") == nullptr);  // lockout: stays in the ini and EMF.exe's Server tab
    CHECK(find_def(defs, "chat.admins") == nullptr);
    CHECK(find_def(defs, "overlay.key")->type == SettingType::ScanCode);
}

TEST_CASE("emf settings: the defaults are the ones the overlay uses when emf_core.ini is empty") {
    std::map<std::string, std::string> dflt;
    for (const auto& d : emf_setting_defs({})) dflt[d.key] = d.default_value;
    std::vector<std::string> problems;
    const LookSettings from_defaults = read_look_settings(
        [&](const char* section, const char* key) {
            auto it = dflt.find(std::string(section) + "." + key);
            return it == dflt.end() ? std::string() : it->second;
        },
        &problems);
    CHECK(problems.empty());
    CHECK(from_defaults == LookSettings{});
    CHECK(dflt["overlay.key"] == "0x29");
    CHECK(dflt["overlay.mouse_speed"] == "1");
    CHECK(dflt["overlay.suggest_rows"] == "8");
    CHECK(dflt["backup.keep"] == "10");
    CHECK(dflt["backup.min_minutes"] == "60");
}

TEST_CASE("emf settings: dropped-in fonts become choices; the three EMF ships are not listed twice") {
    const std::wstring dir = test_scratch::temp_path(L"emf_fonts_test");
    CreateDirectoryW(dir.c_str(), nullptr);
    test_scratch::remove_at_exit(dir);
    for (const wchar_t* f : {L"Alegreya-Regular.ttf", L"MyFont.ttf", L"Other.OTF", L"notes.txt"})
        std::ofstream(dir + L"\\" + f) << "x";
    CHECK(emf_font_files(dir) == std::vector<std::string>{"MyFont.ttf", "Other.OTF"});
    const auto defs = emf_setting_defs(emf_font_files(dir));
    const SettingDef* hud_font = find_def(defs, "hud.font");
    REQUIRE(hud_font);
    REQUIRE(hud_font->options.size() == 4);
    CHECK(hud_font->options[2].value == "file:MyFont.ttf");
    CHECK(find_def(defs, "overlay.font")->options.back().value == "file:Other.OTF");
    CHECK(emf_font_files(dir + L"\\missing").empty());
}

TEST_CASE("emf settings: the console key is a scan code from 0x01 to 0x7f, kept as 0x29") {
    const SettingDef* key = find_def(emf_setting_defs({}), "overlay.key");
    REQUIRE(key);
    CHECK(setting_value(*key, "41", false, nullptr).value_or("") == "0x29");
    CHECK(setting_value(*key, "0x3B", false, nullptr).value_or("") == "0x3b");
    std::string why;
    CHECK_FALSE(setting_value(*key, "0x80", false, &why));
    CHECK(why.find("scan code") != std::string::npos);
    CHECK_FALSE(setting_value(*key, "F1", false, nullptr));
    CHECK_FALSE(setting_value(*key, "", false, nullptr));
    CHECK(setting_shown_value(*key, "0x29") == "the key under Esc (0x29)");
}

TEST_CASE("emf settings: a mod cannot take the id emf") {
    const auto r = parse_manifest(R"({"id": "emf", "version": "1.0.0"})");
    CHECK_FALSE(r.manifest);
    CHECK(r.error.find("EMF's own") != std::string::npos);
    CHECK(parse_manifest(R"({"id": "emf_tools", "version": "1.0.0"})").manifest);
}

namespace {
struct MapIni : IniBackend {
    std::map<std::string, std::string> values;
    std::optional<std::string> read(const std::string& mod, const std::string& key) override {
        auto it = values.find(mod + "." + key);
        if (it == values.end()) return std::nullopt;
        return it->second;
    }
    bool write(const std::string& mod, const std::string& key, const std::string& value) override {
        values[mod + "." + key] = value;
        return true;
    }
};
}  // namespace

TEST_CASE("look_from_store: the store's emf values make the look; unset keys are the defaults") {
    MapIni ini;
    ini.values["emf.hud.size"] = "140";
    SettingsStore s(&ini);
    for (SettingDef d : emf_setting_defs({}))
        REQUIRE(s.declare(kEmfSettingsId, std::move(d), nullptr, SettingsStore::Declare::Keep));
    REQUIRE(s.set(kEmfSettingsId, "overlay.accent", "#112233") == SettingsStore::SetResult::Ok);
    REQUIRE(s.set(kEmfSettingsId, "overlay.colour_error", "#010203") == SettingsStore::SetResult::Ok);
    std::vector<std::string> problems;
    const LookSettings look = look_from_store(s, &problems);
    CHECK(problems.empty());
    CHECK(look.hud_size == 140);
    CHECK(look.accent == 0x112233FFu);
    CHECK(look.colour_error == 0x010203FFu);
    CHECK(look.style == HudStyle::Game);
    CHECK(look.height == 50);
    SettingsStore empty(&ini);  // nothing declared (a core without the defs): the defaults
    CHECK(look_from_store(empty, nullptr) == LookSettings{});
}
