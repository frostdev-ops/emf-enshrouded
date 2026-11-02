// blessings (Flame Blessings): an EMF mod. God mode, endless health, stamina and mana, and no fall damage, per player.
//   - The host switches their own in the Mods tab or on a wheel (hold F11); they are settings, so they stay on.
//   - Admins (the console, emf\commands.txt, the host's own chat, [chat] admins) use /blessings on any player; those end
//     when the player leaves or the world closes. Not a command for every player.
// Each blessing shows on the host's screen and is told in chat; nothing reaches the save. Runs where the world runs (the
// host or a dedicated server); players who join need nothing. Installed switched off.
// The rules (tested without the game) are blessings_logic.h; the game side blessings_glue.h.
#define NOMINMAX
#include <windows.h>

#include <sstream>
#include <string>
#include <vector>

#include "blessings_glue.h"
#include "blessings_logic.h"
#include "emf/emf.hpp"
#include "emf/keys.hpp"

namespace {
using blessings::kMod;

blessings::Glue g_blessings;
emf::keys::Edge g_key;
std::string g_key_text;
uint32_t g_key_vk = 0;

// The mod's own switch, as the Mods tab saved it (emf\config\blessings.ini, [blessings] enabled; off by default).
bool switch_on() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring ini(exe);
    ini = ini.substr(0, ini.find_last_of(L"\\/")) + L"\\emf\\config\\blessings.ini";
    return GetPrivateProfileIntW(L"blessings", L"enabled", 0, ini.c_str()) != 0;
}

void reply(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) EMF_REPLY("%s", line.c_str());
}

void on_tick(emf::World w) {
    g_blessings.tick(w);  // also a client world's step: in someone else's game it takes the host's line away
    // the wheel is the host's (a dedicated server has no screen)
    if (!w.is_server() || g_blessings.switched_off() || !emf::local_player()) return;
    const std::string& key = g_blessings.settings().wheel_key;
    if (key != g_key_text) {
        g_key_text = key;
        g_key_vk = emf::keys::parse_key(std::wstring(key.begin(), key.end()));
    }
    if (!g_key_vk || !g_key.pressed(emf::keys::down(g_key_vk))) return;
    const bool open = emf::wheel("Flame Blessings", g_blessings.wheel_items(), g_key_vk, EMF_WORLD_SERVER,
                                 [](emf::World w2, int choice) { g_blessings.wheel_pick(w2, choice); });
    if (!open) EMF_NOTIFY("%s", "Flame Blessings: the wheel is busy (is the console open?); the blessings command works too");
}
}  // namespace

extern "C" __declspec(dllexport) bool emf_mod_load(const EmfApi* api) {
    if (!emf::init(api)) return false;
    if (!emf::has_v19()) {
        EMF_LOG(kMod, "needs EMF API v19 (attributes, chat lines and player names): update EMF");
        return false;
    }
    g_blessings.clock = [] { return GetTickCount64(); };
    g_blessings.load();
    emf::on_setting_changed(kMod, [](emf::World, const std::string& key) {
        if (key == "enabled") g_blessings.set_switched_off(!switch_on());
        g_blessings.settings_changed();
    });
    // Admins only: registered without EMF_COMMAND_PLAYERS.
    const bool added = emf::add_command(
        "blessings",
        "<god|refill|stamina|mana|nofall|heal|off|xp|level|chest|list|help> [on|off|n|undo] [#k|name]: Flame Blessings "
        "(admin)",
        [](emf::World w, const std::vector<std::string>& a) {
            blessings::Sender from;
            EmfCommandSender s{};
            if (emf::command_sender(&s) && s.from_chat) {
                from.from_chat = true;
                from.admin = s.admin != 0;
                from.entity = s.entity;
            }
            reply(g_blessings.command(w, {a.begin() + (a.empty() ? 0 : 1), a.end()}, from));
        });
    if (!added) EMF_LOG(kMod, "the blessings command could not be added (see the core log): another mod may use the name");
    emf::on_tick(on_tick);
    const auto& s = g_blessings.settings();
    EMF_LOG(kMod, "ready: the host's blessings: %s; wheel key %s; god mode and refills %s on this game build",
            s.host.any() ? blessings::joined(blessings::short_names(s.host)).c_str() : "none",
            s.wheel_key.empty() ? "(none)" : s.wheel_key.c_str(), blessings::attributes_on() ? "work" : "do not work");
    return true;
}
