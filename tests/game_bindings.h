#pragma once
// The keys Enshrouded 1.0's default keyboard preset ("Preset A") binds, for tests that a mod's default key leaves the
// game alone (keybind audit, 2026-11-01): every letter except K, every digit, Tab, Space, Enter, Esc, F1 (feedback),
// F5 (reload in menus), F12 (Steam's screenshot), Delete, Page Up / Page Down, the arrows and Shift / Ctrl / Alt.
// Players can rebind the game; this is its default only. Quick Stack's L and Villager Jobs' K were chosen with this
// list in view (L only toggles voice-over playback; K is the one letter the preset leaves free).
// Also: a setting's default as the source tree's mod.json declares it, and the keys the shipped mods take.
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/manifest.h"
#include "emf/keys.hpp"

namespace game_bindings {

inline bool is_game_key(uint32_t vk) {
    if (vk >= 'A' && vk <= 'Z') return vk != 'K';
    if (vk >= '0' && vk <= '9') return true;
    switch (vk) {
        case VK_TAB: case VK_SPACE: case VK_RETURN: case VK_ESCAPE:
        case VK_F1: case VK_F5: case VK_F12:
        case VK_DELETE: case VK_PRIOR: case VK_NEXT:
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_SHIFT: case VK_CONTROL: case VK_MENU:
            return true;
        default:
            return false;
    }
}

// A key setting's text ("F6", "G", "0x71") is a key the game binds by default. Text that is no key is not.
inline bool is_game_key(const std::string& text) {
    return is_game_key(emf::keys::parse_key(std::wstring(text.begin(), text.end())));
}

// The default of setting `key` in <source>/<rel> (a mod.json); "<no setting>" when it has none, "<unreadable>" when
// the file does not parse.
inline std::string manifest_default(const std::string& rel, const std::string& key) {
    std::ifstream f(std::string(EMF_SOURCE_DIR) + "/" + rel, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const auto m = emf::core::parse_manifest(ss.str());
    if (!m.manifest) return "<unreadable>";
    for (const auto& s : m.manifest->settings)
        if (s.key == key) return s.default_value;
    return "<no setting>";
}

// Every key a shipped mod (mods/*/mod.json) binds by default: virtual key -> "mod.setting" for each setting on it.
// Settings whose default is no key are left out.
inline std::map<uint32_t, std::vector<std::string>> shipped_keys() {
    std::map<uint32_t, std::vector<std::string>> out;
    for (const auto& e : std::filesystem::directory_iterator(std::string(EMF_SOURCE_DIR) + "/mods")) {
        std::ifstream f(e.path() / "mod.json", std::ios::binary);
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        const auto m = emf::core::parse_manifest(ss.str());
        if (!m.manifest) continue;
        for (const auto& s : m.manifest->settings) {
            if (s.type != emf::core::SettingType::Key || s.default_value.empty()) continue;
            const uint32_t vk = emf::keys::parse_key(std::wstring(s.default_value.begin(), s.default_value.end()));
            out[vk].push_back(m.manifest->id + "." + s.key);
        }
    }
    return out;
}

}  // namespace game_bindings
