// API v32 buffs: the game's buff types and an entity's buffs (read where no program runs), giving and removing through
// the emf_buffs program (BuffService), the players' buff events (their containers compared after the server step), the
// console's buffs.* commands. Private research buffs_2026-11-11.
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/api.h"
#include "core/buff_service.h"
#include "core/game_text.h"
#include "common/seh.h"
#include "emf/generated/components.hpp"

namespace emf::core {

namespace {

namespace ecs = emf::gen::ecs;

uint8_t* raw(EmfWorld* w) { return reinterpret_cast<uint8_t*>(w); }
void say(const std::string& line) {
    if (CoreState* s = core_state(); s && s->log_line) s->log_line(line);
}
std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

const uint8_t* buff_registry(EmfWorld* w) {
    return w ? static_cast<const uint8_t*>(WorldView(raw(w)).singleton("BuffRegistry")) : nullptr;
}

BuffNet net_of(uint32_t buff_entity, void* ctx) {
    BuffNet n;
    CoreState* s = core_state();
    if (!s || !s->get_component) return n;
    const ComponentRef r = get_component(s->get_component, static_cast<uint8_t*>(ctx), ecs::BuffNetworkData::kHash, buff_entity);
    uint8_t d[24];
    if (!r.ptr || !seh_read(r.ptr, d, sizeof d)) return n;
    std::memcpy(&n.life, d, 8);
    std::memcpy(&n.end, d + 8, 8);
    std::memcpy(&n.type, d + 16, 4);
    std::memcpy(&n.level, d + 20, 4);
    n.known = true;
    return n;
}

bool container_of(EmfWorld* w, uint32_t entity, uint8_t* out) {
    CoreState* s = core_state();
    if (!s || !s->get_component || !w || !entity) return false;
    const ComponentRef r = get_component(s->get_component, raw(w), ecs::BuffContainer::kHash, entity);
    return r.ptr && (r.size == 0 || r.size >= kBuffContainerSize) && seh_read(r.ptr, out, kBuffContainerSize);
}

bool guarded_watcher(EmfBuffCallback cb, EmfWorld* w, const EmfBuffEvent* e, void* user) {
    __try {
        cb(w, e, user);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string seconds(int64_t ns) { return std::to_string(ns / 1'000'000'000LL) + " s"; }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

uint32_t api_buff_types(EmfWorld* w, EmfBuffType* out, uint32_t cap) {
    BuffCollection c;
    if (!buff_collection(buff_registry(w), &c)) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < c.count; ++i) {
        EmfBuffType t{};
        if (!buff_type_of(c.records + static_cast<size_t>(i) * kBuffTypeSize, &t)) break;
        if (out && n < cap) out[n] = t;
        ++n;
    }
    return n;
}

bool api_buff_type(EmfWorld* w, uint32_t id, EmfBuffType* out) {
    BuffCollection c;
    if (!out || !buff_collection(buff_registry(w), &c)) return false;
    return buff_type_of(buff_record(c, id), out);
}

bool api_buff_text(uint32_t id, uint32_t which, char* buf, uint32_t cap) {
    if (buf && cap) buf[0] = '\0';
    if (!buf || cap < 2 || which > 1) return false;
    const EmfApi* api = api_table();
    EmfWorld* w = api->client_world();
    if (!w) w = api->server_world();
    EmfBuffType t{};
    if (!api_buff_type(w, id, &t)) return false;
    const uint32_t tag = which == 0 ? t.name_tag : t.desc_tag;
    if (!tag) return false;
    const auto text = game_text().text(tag);
    if (!text || text->empty()) return false;
    size_t n = (std::min)(text->size(), static_cast<size_t>(cap - 1));
    while (n > 0 && n < text->size() && (static_cast<unsigned char>((*text)[n]) & 0xC0) == 0x80) --n;
    std::memcpy(buf, text->data(), n);
    buf[n] = '\0';
    return true;
}

std::string buff_name(EmfWorld* w, uint32_t id) {
    EmfBuffType t{};
    if (!api_buff_type(w, id, &t) || !t.name_tag) return {};
    const auto text = game_text().text(t.name_tag);
    return text ? *text : std::string();
}

uint32_t api_buffs_of(EmfWorld* w, uint32_t entity, EmfBuff* out, uint32_t cap) {
    alignas(8) uint8_t box[kBuffContainerSize];
    if (!container_of(w, entity, box)) return 0;
    return read_buffs(box, &net_of, w, api_table()->ecs_time_ns(w), out, cap);
}

bool buff_request(BuffRequest r, std::string* why) {
    auto refuse = [&](const std::string& text) {
        if (why) *why = text;
        return false;
    };
    CoreState* s = core_state();
    if (!s) return refuse("EMF is not running");
    if (!r.world || WorldView(raw(r.world)).kind() != EMF_WORLD_SERVER)
        return refuse("buffs are given and removed on the server world only (the host's game or the dedicated server)");
    if (!s->buffs) return refuse("buffs are off (the emf_buffs program could not be added: see the log)");
    BuffCollection c;
    if (buff_collection(buff_registry(r.world), &c) && !buff_record(c, r.type))
        return refuse("unknown buff type " + hex(r.type));
    return s->buffs->request(std::move(r), why);
}

bool api_buff_apply(EmfWorld* w, uint32_t target, uint32_t type, int64_t duration_ns, uint32_t source, uint32_t item,
                    EmfBuffCallback done, void* user) {
    BuffRequest r;
    r.world = w;
    r.target = target;
    r.type = type;
    r.duration_ns = duration_ns;
    r.source = source;
    r.item = item;
    r.done = done;
    r.user = user;
    std::string why;
    if (buff_request(std::move(r), &why)) return true;
    say("buff_apply refused: " + why);
    return false;
}

bool api_buff_remove(EmfWorld* w, uint32_t target, uint32_t type, EmfBuffCallback done, void* user) {
    BuffRequest r;
    r.world = w;
    r.remove = true;
    r.target = target;
    r.type = type;
    r.done = done;
    r.user = user;
    std::string why;
    if (buff_request(std::move(r), &why)) return true;
    say("buff_remove refused: " + why);
    return false;
}

bool api_on_buff(EmfBuffCallback cb, void* user) {
    CoreState* s = core_state();
    if (!s || !cb) return false;
    std::lock_guard<std::mutex> l(s->buff_watchers_lock);
    if (s->buff_watchers.size() >= 16) {
        say("on_buff refused: 16 callbacks already");
        return false;
    }
    s->buff_watchers.push_back({cb, user, true});
    return true;
}

void buffs_server_step(EmfWorld* w) {
    CoreState* s = core_state();
    if (!s || !w) return;
    std::vector<CoreState::BuffWatcher> watchers;
    {
        std::lock_guard<std::mutex> l(s->buff_watchers_lock);
        for (const auto& x : s->buff_watchers)
            if (x.on) watchers.push_back(x);
    }
    if (watchers.empty()) return;
    const uint64_t now = s->now_ms ? s->now_ms() : GetTickCount64();
    if (!s->buff_diff_ms || now - s->buff_diff_ms >= kBuffDiffMs) {
        s->buff_diff_ms = now;
        std::vector<uint32_t> list;
        if (s->buff_players) {
            list = s->buff_players(w);
        } else {
            for (const auto& p : read_players(api_table(), w)) list.push_back(p.entity);
        }
        s->buff_player_list = std::move(list);
        s->buff_diff.retain(s->buff_player_list);
    }
    const std::vector<uint32_t>& players = s->buff_player_list;
    const int64_t ecs_now = api_table()->ecs_time_ns(w);
    std::vector<EmfBuffEvent> events;
    for (uint32_t p : players) {
        alignas(8) uint8_t box[kBuffContainerSize];
        if (!container_of(w, p, box)) {
            s->buff_diff.forget(p);
            continue;
        }
        for (const auto& e : s->buff_diff.step(p, box, &net_of, w, ecs_now)) events.push_back(e);
    }
    for (const auto& e : events)
        for (auto& x : watchers) {
            if (!x.on) continue;
            if (guarded_watcher(x.cb, w, &e, x.user)) continue;
            x.on = false;
            std::lock_guard<std::mutex> l(s->buff_watchers_lock);
            for (auto& y : s->buff_watchers)
                if (y.cb == x.cb && y.user == x.user) y.on = false;
            say("on_buff: a callback faulted and is switched off");
        }
}

void buffs_new_session() {
    if (CoreState* s = core_state()) {
        s->buff_diff.reset();
        s->buff_player_list.clear();
        s->buff_diff_ms = 0;
    }
}

std::string buff_type_line(const EmfBuffType& t, const std::string& name) {
    std::string flags;
    auto add = [&](uint32_t bit, const char* word) {
        if (t.flags & bit) flags += std::string(flags.empty() ? "" : ", ") + word;
    };
    add(EMF_BUFF_SAVED, "saved");
    add(EMF_BUFF_HIDDEN, "hidden");
    add(EMF_BUFF_DESPAWN_ON_DEATH, "ends on death");
    add(EMF_BUFF_FOOD_POISON, "food poison");
    std::string line = hex(t.id) + " " + buff_slot_name(t.slot) + " " + seconds(t.default_ns) + " " +
                       (name.empty() ? std::string("-") : name);
    if (!flags.empty()) line += " (" + flags + ")";
    return line;
}

std::string buff_line(const EmfBuff& b, const std::string& name) {
    std::string line = hex(b.type) + " " + buff_slot_name(b.slot) + " " + (name.empty() ? std::string("-") : name) + ": ";
    if (b.remaining_ns >= 0 && b.total_ns >= 0)
        line += seconds(b.remaining_ns) + " of " + seconds(b.total_ns) + " left";
    else
        line += "time not known yet";
    line += " (entity " + std::to_string(b.entity) + ")";
    if (b.cleansed) line += ", being removed";
    return line;
}

// ---- console ----
namespace {

uint32_t number(const char* s) { return s ? static_cast<uint32_t>(std::strtoul(s, nullptr, 0)) : 0; }

void cmd_buffs_list(EmfWorld* w, int argc, const char* const* argv, void*) {
    int slot = -1;
    std::string text;
    for (int i = 1; i < argc; ++i) {
        const std::string a = lower(argv[i]);
        int found = -1;
        for (int k = 0; k <= EMF_BUFF_TERRAIN; ++k)
            if (a == buff_slot_name(static_cast<uint8_t>(k))) found = k;
        if (found >= 0)
            slot = found;
        else
            text = a;
    }
    std::vector<EmfBuffType> types(api_buff_types(w, nullptr, 0));
    const uint32_t n = api_buff_types(w, types.data(), static_cast<uint32_t>(types.size()));
    if (!n) {
        command_reply_line("buffs: this world's buff types cannot be read");
        return;
    }
    size_t shown = 0;
    for (uint32_t i = 0; i < n && i < types.size(); ++i) {
        const EmfBuffType& t = types[i];
        if (slot >= 0 && t.slot != slot) continue;
        const std::string name = buff_name(w, t.id);
        if (!text.empty() && lower(name).find(text) == std::string::npos) continue;
        command_reply_line(buff_type_line(t, name));
        ++shown;
    }
    command_reply_line("buffs: " + std::to_string(shown) + " of " + std::to_string(n) + " types");
}

void cmd_buffs_of(EmfWorld* w, int argc, const char* const* argv, void*) {
    const uint32_t entity = argc > 1 ? number(argv[1]) : 1;
    EmfBuff b[40];
    const uint32_t n = api_buffs_of(w, entity, b, 40);
    for (uint32_t i = 0; i < n && i < 40; ++i) command_reply_line(buff_line(b[i], buff_name(w, b[i].type)));
    command_reply_line("buffs: entity " + std::to_string(entity) + " has " + std::to_string(n));
}

void give_or_clear(EmfWorld* w, int argc, const char* const* argv, bool remove) {
    if (argc < 3) {
        command_reply_line(remove ? "usage: buffs.clear <entity> <type id>" : "usage: buffs.give <entity> <type id> [seconds]");
        return;
    }
    BuffRequest r;
    r.world = w;
    r.remove = remove;
    r.target = number(argv[1]);
    r.type = number(argv[2]);
    if (!remove && argc > 3) r.duration_ns = static_cast<int64_t>(std::strtoll(argv[3], nullptr, 10)) * 1'000'000'000LL;
    r.then = [](EmfWorld* world, const EmfBuffEvent& e) {
        const std::string what = hex(e.type) + " " + buff_name(world, e.type) + " on " + std::to_string(e.target);
        if (e.kind == EMF_BUFF_REFUSED)
            say("buffs: refused " + what + ": " + (e.why ? e.why : "?"));
        else
            say(std::string("buffs: ") + (e.kind == EMF_BUFF_REMOVED ? "removed " : "gave ") + what + " (buff entity " +
                std::to_string(e.buff_entity) + ")");
    };
    std::string why;
    if (buff_request(std::move(r), &why))
        command_reply_line("buffs: queued (the result is in the log after the next step)");
    else
        command_reply_line("buffs: refused: " + why);
}

void cmd_buffs_give(EmfWorld* w, int argc, const char* const* argv, void*) { give_or_clear(w, argc, argv, false); }
void cmd_buffs_clear(EmfWorld* w, int argc, const char* const* argv, void*) { give_or_clear(w, argc, argv, true); }

}  // namespace

bool add_buff_commands(CommandCenter& commands) {
    bool ok = commands.add("buffs.list", " [food|buff|debuff|rested|terrain] [text]: the game's buff types", cmd_buffs_list,
                           nullptr, nullptr);
    ok &= commands.add("buffs.of", " [entity]: an entity's active buffs (default 1)", cmd_buffs_of, nullptr, nullptr);
    ok &= commands.add("buffs.give", " <entity> <type id> [seconds]: gives a buff (server world)", cmd_buffs_give, nullptr,
                       nullptr);
    ok &= commands.add("buffs.clear", " <entity> <type id>: removes a buff (server world)", cmd_buffs_clear, nullptr,
                       nullptr);
    return ok;
}

}  // namespace emf::core
