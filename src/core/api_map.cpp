// API v34 the map (api.h): map_cursor and on_map_click on this game's map screen, map_action (the server end's actions
// over the core channel emf/mapact; each player's game does the Shift + hold gesture and sends the spot), ground_at on
// the server world (GroundService), and the console's map.where and ground. Private spec 2026-11-11-markers-design,
// plan 2026-11-11-markers-step2.
#include <windows.h>

#include <intrin.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "common/world_view.h"
#include "core/api.h"
#include "core/fault_report.h"
#include "core/ground_glue.h"
#include "core/map_input.h"
#include "core/marker_capture.h"
#include "core/net/net_service.h"
#include "core/overlay/input_hook.h"

namespace emf::core {

namespace {

constexpr size_t kClickListenersMax = 16;

uint64_t now_of(const CoreState* s) { return s && s->now_ms ? s->now_ms() : GetTickCount64(); }

void log_of(CoreState* s, const std::string& line) {
    if (s && s->log_line) s->log_line(line);
}

void announce_all(CoreState* s) {
    if (s && s->net) s->net->to_players("emf", kMapActionsChannel, s->map.actions.announce_message());
}

// The host's own game says no hello: it gets the list once its world is up, and again when the list changes.
void tell_host(CoreState* s) {
    uint32_t local = 0;
    if (!s->net || !s->net->hosted(&local) || !local) return;
    const uint64_t now = now_of(s);
    if (s->map.host_ms && now - s->map.host_ms < 1000) return;
    s->map.host_ms = now;
    const uint32_t told = s->map.host_told.load();
    if (!told && s->map.actions.empty()) return;  // nothing to tell: the gesture is off anyway
    const std::string msg = s->map.actions.announce_message();
    if (told == local && msg == s->map.host_msg) return;
    std::string w;
    if (s->net->to_player("emf", local, kMapActionsChannel, msg, &w)) {
        s->map.host_told = local;
        s->map.host_msg = msg;
    }
}

}  // namespace

bool api_map_cursor(double* x, double* z) {
    if (!x || !z) return false;
    const uint64_t now = now_of(core_state());
    MapView view;
    if (!marker_sight(now).map_open || !marker_map_view(&view)) return false;
    HWND hwnd = input_game_window();
    POINT pt;
    RECT rc;
    if (!hwnd || !GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt) || !GetClientRect(hwnd, &rc)) return false;
    const float cw = static_cast<float>(rc.right - rc.left), ch = static_cast<float>(rc.bottom - rc.top);
    if (cw <= 0 || ch <= 0) return false;
    return map_world_at(view, static_cast<float>(pt.x) * view.ui_w / cw, static_cast<float>(pt.y) * view.ui_h / ch, x, z);
}

bool api_on_map_click(EmfMapClickCallback cb, void* user) {
    CoreState* s = core_state();
    if (!s || !cb) return false;
    std::lock_guard<std::mutex> lock(s->map.click_lock);
    if (s->map.clicks.size() >= kClickListenersMax) return false;
    s->map.clicks.emplace_back(cb, user);
    return true;
}

bool api_map_action(const char* id, const char* label, uint32_t hold_ms, EmfMapActionCallback cb, void* user) {
    CoreState* s = core_state();
    if (!s) return false;
    const std::string owner = module_name_of(_ReturnAddress());
    if (!s->net) {
        log_of(s, "map_action refused (" + owner + "): mod messages are not available on this game build");
        return false;
    }
    if (!s->map.actions.add(owner, id, label, hold_ms, cb, user)) {
        if (cb) log_of(s, "map_action refused (" + owner + "): a bad id (1-48 of A-Z a-z 0-9 _ . - :) or 16 already");
        return false;
    }
    announce_all(s);
    return true;
}

bool api_ground_at(EmfWorld* world, double x, double z, EmfGroundCallback cb, void* user) {
    CoreState* s = core_state();
    if (!s || !cb || !world || WorldView(reinterpret_cast<uint8_t*>(world)).kind() != EMF_WORLD_SERVER) return false;
    std::lock_guard<std::recursive_mutex> lock(s->map.ground_lock);
    if (!s->map.ground) return false;
    std::string why;
    return s->map.ground->request(
        world, x, z,
        [cb, user](EmfWorld* w, bool ok, double gx, double gy, double gz, const std::string& msg) {
            cb(w, ok, gx, gy, gz, msg.c_str(), user);
        },
        &why);
}

void map_server_tick(EmfWorld* server) {
    CoreState* s = core_state();
    if (!s || !server) return;
    tell_host(s);
    std::lock_guard<std::recursive_mutex> lock(s->map.ground_lock);
    if (s->map.ground) s->map.ground->on_server_tick(server);
}

void map_world_setup(uint8_t kind) {
    CoreState* s = core_state();
    if (!s) return;
    if (kind == EMF_WORLD_CLIENT) {
        s->map.client.reset();  // the actions belong to the server that announced them
        s->map.host_told = 0;   // a host's own game hears them again
        map_input_set_announced(false);
        map_input().set_hold_ms(0);
    } else if (kind == EMF_WORLD_SERVER) {
        std::lock_guard<std::recursive_mutex> lock(s->map.ground_lock);
        if (s->map.ground) s->map.ground->world_reset();
    }
}

bool map_net_setup(net::NetService* net, std::string* why) {
    if (!net) {
        if (why) *why = "no mod messages";
        return false;
    }
    net::ChannelDef c;
    c.name = kMapActionsChannel;
    c.version = kMapActionsChannelVersion;
    c.flags = net::kToServer | net::kToPlayers;
    c.max_size = 4096;
    c.per_10s = 30;  // MapActions keeps its own 20 per 10 s per player
    c.on_server = [](EmfWorld* w, const net::NetFrom& f, const std::string& d) {
        CoreState* s = core_state();
        if (s && f.player) s->map.actions.on_done(w, f.player, d, now_of(s));
    };
    c.on_player = [](EmfWorld*, const net::NetFrom&, const std::string& d) {
        CoreState* s = core_state();
        if (!s || !s->map.client.on_announce(d)) return;
        map_input().set_hold_ms(s->map.client.hold_ms());
        map_input_set_announced(s->map.client.announced());
    };
    if (!net->add_channel("emf", c, why)) return false;
    map_input().set_done([](double x, double z) {
        CoreState* s = core_state();
        if (!s || !s->net) return;
        const std::string m = s->map.client.done(x, z);
        std::string w;
        if (!m.empty() && !s->net->to_server("emf", kMapActionsChannel, m, &w)) log_of(s, "map action: not sent (" + w + ")");
    });
    return true;
}

void map_input_setup() {
    map_input().set_click_listener([](const EmfMapClick& click) {
        CoreState* s = core_state();
        if (!s) return;
        std::vector<std::pair<EmfMapClickCallback, void*>> list;
        {
            std::lock_guard<std::mutex> lock(s->map.click_lock);
            list = s->map.clicks;
        }
        for (const auto& [cb, user] : list) cb(&click, user);
    });
}

void map_on_hello(uint32_t player) {
    CoreState* s = core_state();
    if (!s || !s->net || !player) return;
    std::string w;
    s->net->to_player("emf", player, kMapActionsChannel, s->map.actions.announce_message(), &w);
}

bool map_greets() {
    CoreState* s = core_state();
    return s && !s->map.actions.empty();
}

namespace {

void cmd_map_where(EmfWorld*, int, const char* const*, void*) {
    double x = 0, z = 0;
    char line[160];
    if (api_map_cursor(&x, &z))
        std::snprintf(line, sizeof line, "map.where: the mouse is over x %.1f, z %.1f", x, z);
    else
        std::snprintf(line, sizeof line, "map.where: the map is not open, or the mouse is not on it");
    command_reply_line(line);
    command_reply_line(map_input_announced() ? "map.where: this game's server has a map action (Shift + hold)"
                                             : "map.where: no map action from this game's server");
}

void cmd_ground(EmfWorld* world, int argc, const char* const* argv, void*) {
    if (argc < 3) {
        command_reply_line("ground <x> <z>: the ground under that spot of the world");
        return;
    }
    const double x = std::strtod(argv[1], nullptr), z = std::strtod(argv[2], nullptr);
    CoreState* s = core_state();
    std::string why;
    bool queued = false;
    if (s && world) {
        std::lock_guard<std::recursive_mutex> lock(s->map.ground_lock);
        if (!s->map.ground) {
            why = "not available on this game build";
        } else {
            const ReplyFn reply = current_reply();
            queued = s->map.ground->request(
                world, x, z,
                [reply](EmfWorld*, bool ok, double gx, double gy, double gz, const std::string& msg) {
                    char line[200];
                    if (ok)
                        std::snprintf(line, sizeof line, "ground: x %.1f, z %.1f: the ground is at y %.2f", gx, gz, gy);
                    else
                        std::snprintf(line, sizeof line, "ground: x %.1f, z %.1f: %s", gx, gz, msg.c_str());
                    if (reply) reply(line);
                },
                &why);
        }
    } else {
        why = "it needs a world you host";
    }
    if (!queued) {
        command_reply_line("ground: " + why);
        return;
    }
    command_reply_line("ground: looking (the answer follows)");
    const std::string d = ground_describe();
    if (!d.empty()) command_reply_line("ground: " + d);
}

}  // namespace

bool add_map_commands(CommandCenter& commands) {
    // map.where runs anywhere (a game the player joined has a map too); ground needs the world you host
    bool ok = commands.add("map.where", ": the world spot under the mouse on the open map, and whether Shift + hold does "
                                        "something here",
                           cmd_map_where, nullptr, nullptr, true);
    ok &= commands.add("ground", " <x> <z>: the ground's height at that spot (the place may need a few seconds to load)",
                       cmd_ground, nullptr, nullptr);
    return ok;
}

}  // namespace emf::core
