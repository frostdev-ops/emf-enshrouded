#include "world_commands.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "world_settings.h"
#include "world_settings_service.h"

namespace emf::core {
namespace {

using ws::Field;

ws::Service& svc_of(void* u) { return *static_cast<ws::Service*>(u); }

std::string label(const Field& f) { return f.alias ? std::string(f.alias) + " (" + f.key + ")" : f.key; }
std::string short_name(const Field& f) { return f.alias ? f.alias : f.key; }

bool persist_word(int argc, const char* const* argv, int at) { return argc > at && std::strcmp(argv[at], "persist") == 0; }

void cmd_get(EmfWorld*, int argc, const char* const* argv, void* u) {
    auto& svc = svc_of(u);
    const auto rows = svc.rows();
    if (rows.empty()) {
        std::string why;
        double v = 0;
        svc.get(argc > 1 ? argv[1] : "loot", &v, &why);
        command_reply_line("world.get: " + why);
        return;
    }
    if (argc > 1) {
        const Field* f = ws::find(argv[1]);
        if (!f) {
            command_reply_line("world.get: \"" + std::string(argv[1]) + "\" is not a world setting (world.list shows them)");
            return;
        }
        for (const auto& r : rows)
            if (r.field == f)
                command_reply_line(label(*f) + " = " + ws::text(*f, r.value) + " (this world: " + ws::text(*f, r.world) +
                                   "; allowed " + ws::range_text(*f) + ")");
        return;
    }
    std::string changed;
    for (const auto& r : rows)
        if (r.changed) changed += (changed.empty() ? "" : ", ") + short_name(*r.field) + " = " + ws::text(*r.field, r.value);
    command_reply_line(changed.empty() ? "every world setting has this world's own value (world.list shows them)"
                                       : "changed from this world's own values: " + changed);
}

void cmd_set(EmfWorld*, int argc, const char* const* argv, void* u) {
    if (argc < 3) {
        command_reply_line("usage: world.set <setting> <value> [persist] (world.list shows the settings)");
        return;
    }
    const bool persist = persist_word(argc, argv, 3);
    std::string why;
    if (!svc_of(u).set_text(argv[1], argv[2], persist, &why)) {
        command_reply_line("world.set: " + why);
        return;
    }
    const Field* f = ws::find(argv[1]);
    const ws::Parsed p = ws::parse(*f, argv[2]);
    command_reply_line(label(*f) + " = " + ws::text(*f, p.value) + " from the next step; every player gets it" +
                       (persist ? "; kept for this world" : "; until the world is loaded again"));
}

void cmd_reset(EmfWorld*, int argc, const char* const* argv, void* u) {
    const bool persist_only = argc == 2 && std::strcmp(argv[1], "persist") == 0;
    const std::string key = argc > 1 && !persist_only ? argv[1] : "all";
    const bool persist = persist_only || persist_word(argc, argv, 2);
    std::string why;
    if (!svc_of(u).reset(key, persist, &why)) {
        command_reply_line("world.reset: " + why);
        return;
    }
    const Field* f = key == "all" ? nullptr : ws::find(key);
    command_reply_line(std::string(f ? label(*f) + " goes" : "every setting goes") +
                       " back to this world's own value from the next step" + (persist ? "; no longer kept" : ""));
}

void cmd_list(EmfWorld*, int, const char* const*, void* u) {
    const auto rows = svc_of(u).rows();
    if (rows.empty()) {
        command_reply_line("world.list: only the host or the dedicated server changes world settings (no world is hosted "
                           "here)");
        return;
    }
    std::vector<std::string> groups;
    for (const auto& r : rows) {
        bool seen = false;
        for (const auto& g : groups) seen |= g == r.field->group;
        if (!seen) groups.push_back(r.field->group);
    }
    for (const auto& g : groups) {
        std::string line = g + ":";
        bool first = true;
        for (const auto& r : rows) {
            if (r.field->group != g) continue;
            line += std::string(first ? " " : ", ") + short_name(*r.field) + "=" + ws::text(*r.field, r.value) +
                    (r.changed ? "*" : "");
            first = false;
        }
        command_reply_line(line);
    }
    command_reply_line("(* changed from this world's own value; world.get <setting> shows its range)");
}

void cmd_export(EmfWorld*, int, const char* const*, void* u) {
    uint8_t copy[ws::kStructSize];
    if (!svc_of(u).snapshot(copy)) {
        command_reply_line("world.export: no world is hosted here");
        return;
    }
    command_reply_line("for enshrouded_server.json (replace its gameSettingsPreset and gameSettings; EMF never edits it):");
    std::istringstream in(ws::export_block(copy));
    std::string line;
    while (std::getline(in, line)) command_reply_line(line);
}

}  // namespace

bool add_world_commands(CommandCenter& center, ws::Service& svc) {
    struct Def {
        const char* name;
        const char* help;
        CommandFn fn;
    } defs[] = {
        {"world.get", "[setting]: a world setting with this world's own value and its range; without one, what changed",
         cmd_get},
        {"world.set", "<setting> <value> [persist]: change a world setting now, for every player (admin)", cmd_set},
        {"world.reset", "[setting|all] [persist]: back to this world's own values (admin)", cmd_reset},
        {"world.list", ": every world setting by group", cmd_list},
        {"world.export", ": the settings as a block for enshrouded_server.json", cmd_export},
    };
    bool ok = true;
    for (const auto& d : defs) ok &= center.add(d.name, d.help, d.fn, &svc, nullptr);
    return ok;
}

}  // namespace emf::core
