// teleports (Waypoint Teleports): an EMF mod. /unstuck and /home for every player, tp / bring / tp.pos / tp.back /
// tp.list for admins, through EMF's teleport (API v20), which moves a player the way the game's own fast travel does.
// With EMF API v34: the map teleport (EMF's map action "teleport": Shift + hold on the map) and /tp.mark.
// Runs where the world runs (the host or a dedicated server); players who join need nothing. Installed switched off.
// The decisions (tested without the game) are tp_logic.h; the game side is tp_glue.h.
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "emf/emf.hpp"
#include "emf/generated/components.hpp"
#include "tp_glue.h"
#include "tp_logic.h"

namespace {
constexpr const char* kMod = "teleports";

tp::ApiGame g_game(kMod);
tp::Teleports g_tp(g_game);
std::atomic<bool> g_switched_off{false};
// Everything but a settings change runs on the server world's thread (commands, ticks, events, teleport answers). A
// change comes after a step of any world (the client world's thread in a game you host): it waits here for the next
// server tick.
std::mutex g_settings_lock;
std::optional<tp::Settings> g_new_settings;
uint32_t g_session = 0;

// The mod's own switch, as the Mods tab saved it (emf\config\teleports.ini, [teleports] enabled; the API has no getter).
bool switch_on() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring ini(exe);
    ini = ini.substr(0, ini.find_last_of(L"\\/")) + L"\\emf\\config\\teleports.ini";
    return GetPrivateProfileIntW(L"teleports", L"enabled", 0, ini.c_str()) != 0;
}

tp::Settings read_settings() {
    tp::Settings s;
    s.unstuck = emf::setting_bool(kMod, "unstuck", true);
    s.unstuck_height = emf::setting_float(kMod, "unstuck_height", 4.0);
    s.unstuck_cooldown_s = static_cast<int>(emf::setting_int(kMod, "unstuck_cooldown_seconds", 60));
    s.combat_s = static_cast<int>(emf::setting_int(kMod, "combat_seconds", 10));
    s.home = emf::setting_bool(kMod, "home", true);
    s.home_cooldown_min = static_cast<int>(emf::setting_int(kMod, "home_cooldown_minutes", 10));
    s.home_combat_s = static_cast<int>(emf::setting_int(kMod, "home_combat_seconds", 10));
    s.home_cost_item = emf::setting_text(kMod, "home_cost_item", "");
    s.home_cost_count = static_cast<int>(emf::setting_int(kMod, "home_cost_count", 1));
    s.home_in_shroud = emf::setting_bool(kMod, "home_in_shroud", false);
    s.countdown_s = static_cast<int>(emf::setting_int(kMod, "countdown_seconds", 3));
    s.enabled = tp::parse_enabled(emf::setting_text(kMod, "enabled_commands", tp::kDefaultEnabled));
    s.map_teleport = tp::parse_map_who(emf::setting_text(kMod, "map_teleport", "admins"));
    s.map_cooldown_s = static_cast<int>(emf::setting_int(kMod, "map_cooldown_seconds", 60));
    return s;
}

// EMF's map action: a player's game does the gesture and sends the spot; it arrives here on the server world's thread.
void on_map_done(EmfWorld* w, const EmfMapDone* d, void*) {
    if (!d || g_switched_off) return;
    const emf::World world(w);
    if (!world.is_server()) return;
    g_game.set_world(world);
    g_tp.map_teleport(d->player, d->admin != 0, d->x, d->z);
}

// The action is offered while the map teleport is on (off: players' Shift + clicks stay the game's). Any thread.
void update_map_action(const tp::Settings& s) {
    if (!emf::has_v34() || !emf::api()->map_action) return;
    static std::atomic<int> offered{-1};
    const int want = !g_switched_off && s.map_teleport != tp::MapWho::Off ? 1 : 0;
    if (offered.load() == want) return;
    if (want) {
        if (emf::api()->map_action("teleport", "Teleport here", 0, on_map_done, nullptr))
            offered = 1;  // latched only once the core took it: a refusal is tried again on the next change
        else
            EMF_LOG(kMod, "the map teleport could not be offered (see the core log)");
    } else {
        emf::api()->map_action("teleport", nullptr, 0, nullptr, nullptr);
        offered = 0;
    }
}

bool sender_admin() {
    EmfCommandSender s{};
    if (emf::command_sender(&s)) return s.admin != 0;
    return true;  // an older EMF: only admins reach commands that are not for players
}

// Who "you" is for the running command: the chat sender, else (the console of a game you host) the local player.
uint32_t sender_player() {
    EmfCommandSender s{};
    if (emf::command_sender(&s) && s.from_chat) return s.entity;
    return emf::local_player();
}

void reply(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) EMF_REPLY("%s", line.c_str());
}

// Every command runs on the server world after its step: name that world, then ask the logic.
template <class F>
void run(emf::World w, F f) {
    if (g_switched_off) {
        EMF_REPLY("Waypoint Teleports is switched off");
        return;
    }
    if (!w.is_server()) {
        EMF_REPLY("Waypoint Teleports runs where the world runs (the host or a dedicated server)");
        return;
    }
    g_game.set_world(w);
    reply(f());
}

std::vector<std::string> rest(const std::vector<std::string>& a) { return {a.begin() + (a.empty() ? 0 : 1), a.end()}; }

void on_tick(emf::World w) {
    if (!w.is_server()) return;
    g_game.set_world(w);
    {
        std::lock_guard<std::mutex> l(g_settings_lock);
        if (g_new_settings) {
            g_tp.set(*g_new_settings);
            g_new_settings.reset();
        }
    }
    const uint32_t session = emf::server_session();
    if (session != g_session) {  // another world, or a reload: entity ids and positions mean nothing any more
        g_session = session;
        g_tp.new_session();
    }
    if (!g_switched_off) g_tp.tick();
}
}  // namespace

extern "C" __declspec(dllexport) bool emf_mod_load(const EmfApi* api) {
    if (!emf::init(api)) return false;
    if (!emf::has_v20()) {
        EMF_LOG(kMod, "needs EMF API v20 (teleport)");
        return false;
    }
    g_game.clock = [] { return GetTickCount64(); };
    g_tp.set(read_settings());
    emf::on_setting_changed(kMod, [](emf::World, const std::string& key) {
        if (key == "enabled") g_switched_off = !switch_on();
        const tp::Settings s = read_settings();
        update_map_action(s);
        std::lock_guard<std::mutex> l(g_settings_lock);
        g_new_settings = s;
    });
    update_map_action(g_tp.settings());
    bool ok = true;
    ok &= emf::add_command("unstuck", ": a hop to free ground, else a /home",
                           [](emf::World w, const std::vector<std::string>&) { run(w, [] { return g_tp.unstuck(sender_player()); }); },
                           EMF_COMMAND_PLAYERS);
    ok &= emf::add_command("home", ": to your spawn point",
                           [](emf::World w, const std::vector<std::string>&) { run(w, [] { return g_tp.home(sender_player()); }); },
                           EMF_COMMAND_PLAYERS);
    ok &= emf::add_command("tp", "<#k|name> [<#m|name>]: to k, or k to m (admin)",
                           [](emf::World w, const std::vector<std::string>& a) {
                               run(w, [&] { return g_tp.tp(sender_player(), rest(a)); });
                           }, 0);
    ok &= emf::add_command("bring", "<#k|name>: to you (admin)",
                           [](emf::World w, const std::vector<std::string>& a) {
                               run(w, [&] { return g_tp.bring(sender_player(), rest(a)); });
                           }, 0);
    ok &= emf::add_command("tp.pos", "[#k] <x> <y> <z> [ground] (admin)",
                           [](emf::World w, const std::vector<std::string>& a) {
                               run(w, [&] { return g_tp.tp_pos(sender_player(), rest(a)); });
                           }, 0);
    ok &= emf::add_command("tp.back", "[#k]: undo the last teleport (admin)",
                           [](emf::World w, const std::vector<std::string>& a) {
                               run(w, [&] { return g_tp.tp_back(sender_player(), rest(a)); });
                           }, 0);
    ok &= emf::add_command("tp.list", ": the players' #k (admin)",
                           [](emf::World w, const std::vector<std::string>&) { run(w, [] { return g_tp.tp_list(); }); }, 0);
    ok &= emf::add_command("tp.mark", "[#k]: to your map waypoint (#k: player k to theirs, admin)",
                           [](emf::World w, const std::vector<std::string>& a) {
                               run(w, [&] { return g_tp.tp_mark(sender_player(), sender_admin(), rest(a)); });
                           },
                           EMF_COMMAND_PLAYERS);
    if (!ok) EMF_LOG(kMod, "a command could not be added (see the core log): another mod may use the name");
    emf::on_event<tp::gen::HitEvent>(EMF_WORLD_SERVER, [](emf::World w, const tp::gen::HitEvent& e) {
        g_game.set_world(w);
        g_tp.on_hit(e.targetId.id);
        if (e.rootTargetId.id != e.targetId.id) g_tp.on_hit(e.rootTargetId.id);
    });
    emf::on_tick(on_tick);
    const auto s = g_tp.settings();
    EMF_LOG(kMod, "ready: /unstuck %s, /home %s, countdown %d s, map teleport %s", s.unstuck ? "on" : "off",
            s.home ? "on" : "off", s.countdown_s,
            !emf::has_v34()                       ? "off (needs EMF API v34)"
            : s.map_teleport == tp::MapWho::Off    ? "off"
            : s.map_teleport == tp::MapWho::Admins ? "for admins"
                                                   : "for everyone");
    return true;
}
