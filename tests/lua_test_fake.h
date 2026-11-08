// A fake EmfApi and a temporary mods folder for the Lua host tests (test_lua_host.cpp, test_lua_bindings.cpp).
#pragma once
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <functional>

#include "emf/emf_api.h"
#include "test_scratch.h"

namespace {

struct Fake {
    std::vector<std::string> log, notes, replies;
    std::function<void(const std::string&)> log_hook;  // called with each log line before it is kept (tests that hold a script)
    std::map<std::string, std::string> hud;  // id -> text
    std::map<std::string, std::pair<uint32_t, uint32_t>> hud_style;  // id -> (place, rgba) of its last hud_set
    struct Cmd {
        std::string name, help;
        EmfCommandCallback cb;
        void* user;
        uint32_t flags = 0;  // register_command_ex's EMF_COMMAND_* (v17)
    };
    std::vector<Cmd> commands;
    std::vector<EmfModInfo> mods;
    EmfCommandSender sender{};  // what command_sender reports (size 0: outside a command)
    std::map<std::string, std::string> settings;  // "mod.key" -> value text
    std::vector<std::pair<std::string, std::pair<EmfSettingCallback, void*>>> setting_watchers;
    // worlds, components, items
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> components;  // (hash, entity) -> data
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> templates;   // (hash, entity) -> template half
    std::vector<uint32_t> entities;
    struct Give {
        uint32_t player, item, count;
    };
    std::vector<Give> gives;            // the drops made
    std::set<uint32_t> give_fails;      // give_item at these entities fails (gone: no transform), as in the core
    std::vector<EmfItemsRequest> puts, takes;
    struct Spawn {
        uint8_t guid[16];
        double x, y, z;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> extra;  // spawn_ex: (hash, data) of each component
    };
    std::vector<Spawn> spawns;
    uint32_t next_spawn_id = 1234;  // spawn gives 1234, 1235, ...
    bool spawn_fails = false;       // spawn answers 0, as the core does for a template the game does not have
    std::set<std::vector<uint8_t>> missing_templates;  // these templates (game byte order) only: a pack not installed
    std::vector<uint32_t> destroyed;
    uint32_t tick = 0;  // what world_tick answers (60 per second of game time)
    struct Sub {
        uint8_t kind;
        std::string name;
        EmfWorldEventCallback cb;
        void* user;
        uint32_t expected = 0;  // the expected_size given to on_event
    };
    std::vector<Sub> subs;
    struct Program {
        std::string name;
        uint8_t kind;
        std::vector<std::string> out;
        EmfProgramCallback fn;
        void* user;
    };
    std::vector<Program> programs;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> emitted;  // name, event bytes
    struct Wheel {
        std::string title;
        std::vector<std::string> items;
        uint32_t hold;
        EmfWheelCallback cb;
        void* user;
    };
    std::vector<Wheel> wheels;
    // world loads (on_load) and the hosted session number (server_session)
    std::vector<std::pair<EmfSaveCallback, void*>> load_cbs;
    uint32_t session = 1;
    // the BasicPlayerInfos singleton (empty: none): 0xA0-byte entries, entity id at +0, account at +8
    std::vector<uint8_t> player_infos;
    std::map<std::string, std::vector<uint8_t>> singletons;  // other singletons by name (e.g. g38_daytime::Daytime)
    std::map<uint32_t, std::string> item_names;  // more items for item_info (0x11 "Wood" is always known)
    std::map<std::string, int> item_lookups;     // find_item calls by name (the game reads its whole item map each)
    // v18 attributes: (entity, attribute id) -> value; what attribute_add and damage were asked to do
    std::map<std::pair<uint32_t, uint32_t>, int32_t> attrs;
    std::vector<std::tuple<uint32_t, uint32_t, int32_t>> attr_adds;  // entity, id, delta
    std::vector<std::tuple<uint32_t, uint32_t, int32_t>> damages;    // attacker, target, amount
    // v19 chat and saves: what chat_tell / chat_broadcast were asked to send, the names player_name knows, the on_chat
    // listeners, how often request_save was called
    std::vector<std::pair<uint32_t, std::string>> tells;  // player, text
    std::vector<std::string> broadcasts;
    std::map<uint32_t, std::string> names;  // player entity -> name (absent: player_name answers false)
    std::vector<std::pair<EmfChatCallback, void*>> chat_cbs;
    int saves = 0;
    bool chat_ok = true;  // what chat_tell / chat_broadcast / on_chat / request_save answer
    // v20 teleport: the requests made (answer them with their cb/user as the core would)
    std::vector<EmfTeleport> teleports;
    bool teleport_ok = true;
    // v23 change_component: the requests (answer them with their cb/user as the core would) and their data
    std::vector<EmfComponentChange> component_changes;
    std::vector<std::vector<uint8_t>> component_data;
    bool component_ok = true;
    // v24 windows: what window_open and window_update got (strings copied; answer with cb/user as the core would)
    // and the ids closed
    struct Win {
        std::string id, title;
        uint32_t close_key = 0, place = 0;
        float width = 0;
        uint8_t kind = 0;
        std::vector<EmfUiWidget> widgets;
        std::vector<std::string> keys, texts, tips;
        EmfUiCallback cb = nullptr;
        void* user = nullptr;
    };
    std::vector<Win> windows, window_updates;
    struct Off {  // a v25 offer the host asked for
        EmfWorld* world;
        uint32_t entity;
        EmfOffer o;
        std::string text;
        EmfOfferCallback cb;
        void* user;
    };
    std::vector<Off> offers;
    std::vector<uint32_t> offers_removed;
    bool offer_ok = true;
    bool offer_remove_ok = true;  // false: the core had dropped the offer already
    bool offer_active = true;     // v26 offer_active's answer
    uint32_t next_offer_spawn = 9000;
    std::vector<std::string> windows_closed;
    // v26 pictures: the paths image_load got, its answers
    std::vector<std::string> images_loaded;
    bool image_ok = true;
    uint32_t next_image = 50;
    // v21 world settings: two settings, what set and reset were asked (key as passed, "" for NULL; flags), the
    // change listeners; ws_world false = no hosted world (every call answers false)
    struct WsField {
        const char* key;
        const char* alias;
        const char* group;
        uint32_t type;
        double min, max, value, world;
    };
    std::vector<WsField> ws = {{"resourceDropStackAmountFactor", "loot", "Resources", EMF_WS_FLOAT, 0.25, 2, 1, 1},
                               {"dayTimeDuration", "day_minutes", "Time", EMF_WS_MINUTES, 2, 60, 30, 30}};
    std::vector<std::tuple<std::string, double, uint32_t>> ws_sets;
    std::vector<std::pair<std::string, uint32_t>> ws_resets;
    std::vector<std::pair<EmfWorldSettingCallback, void*>> ws_cbs;
    bool ws_world = true;
    void player_info(uint32_t index, uint32_t entity, uint64_t account) {
        if (player_infos.size() < (index + 1) * 0xA0) player_infos.resize((index + 1) * 0xA0, 0);
        std::memcpy(player_infos.data() + index * 0xA0, &entity, 4);
        std::memcpy(player_infos.data() + index * 0xA0 + 8, &account, 8);
    }
};
Fake* g = nullptr;

void* f_get_component(EmfWorld*, uint32_t hash, uint32_t entity, uint64_t* size) {
    auto it = g->components.find({hash, entity});
    if (it == g->components.end()) {
        if (size) *size = 0;
        return nullptr;
    }
    if (size) *size = it->second.size();
    return it->second.data();
}

void* f_get_template_component(EmfWorld*, uint32_t hash, uint32_t entity, uint64_t* size) {
    auto it = g->templates.find({hash, entity});
    if (it == g->templates.end()) {
        if (size) *size = 0;
        return nullptr;
    }
    if (size) *size = it->second.size();
    return it->second.data();
}
void f_for_each(EmfWorld*, EmfEntityCallback cb, void* user) {
    for (uint32_t e : g->entities) cb(e, user);
}
uint32_t f_find_item(EmfWorld*, const char* name) {
    ++g->item_lookups[name];
    if (std::strcmp(name, "Wood") == 0) return 0x11u;
    for (const auto& [id, n] : g->item_names)
        if (n == name) return id;
    return 0u;
}
bool f_item_info(EmfWorld*, uint32_t id, EmfItemInfo* out) {
    if (auto it = g->item_names.find(id); it != g->item_names.end()) {
        out->item_id = id;
        out->max_stack = 100;
        std::snprintf(out->name, sizeof out->name, "%s", it->second.c_str());
        return true;
    }
    if (id != 0x11) return false;
    out->item_id = id;
    out->max_stack = 250;
    std::snprintf(out->name, sizeof out->name, "Wood");
    std::memset(out->guid, 0xAB, 16);
    return true;
}
uint32_t f_give(EmfWorld*, uint32_t player, uint32_t item, uint32_t count) {
    if (g->give_fails.count(player)) return 0;
    g->gives.push_back({player, item, count});
    return 900;
}
bool g_put_ok = true;  // what put_items_ex answers (false: the request was refused)
bool f_put_ex(EmfWorld*, const EmfItemsRequest* r) {
    g->puts.push_back(*r);
    return g_put_ok;
}
bool f_take(EmfWorld*, const EmfItemsRequest* r) {
    g->takes.push_back(*r);
    return true;
}
uint32_t f_spawn(EmfWorld*, const uint8_t guid[16], double x, double y, double z) {
    if (g->spawn_fails || g->missing_templates.count(std::vector<uint8_t>(guid, guid + 16))) return 0;
    Fake::Spawn s{};
    std::memcpy(s.guid, guid, 16);
    s.x = x;
    s.y = y;
    s.z = z;
    g->spawns.push_back(s);
    // the new entity stands where it was spawned (CurrentTransform: the position in fixed point, 1 m = 2^32, at offset 0)
    std::vector<uint8_t> t(56, 0);
    const int64_t fixed[3] = {static_cast<int64_t>(x * 4294967296.0), static_cast<int64_t>(y * 4294967296.0),
                              static_cast<int64_t>(z * 4294967296.0)};
    std::memcpy(t.data(), fixed, sizeof fixed);
    g->components[{0x027ac564u /* CurrentTransform */, g->next_spawn_id}] = t;
    return g->next_spawn_id++;
}
uint32_t f_spawn_ex(EmfWorld* w, const uint8_t guid[16], double x, double y, double z, const EmfComponentInit* c,
                    uint32_t count) {
    const uint32_t id = f_spawn(w, guid, x, y, z);
    if (!id) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto* p = static_cast<const uint8_t*>(c[i].data);
        g->spawns.back().extra.emplace_back(c[i].hash, std::vector<uint8_t>(p, p + c[i].size));
    }
    return id;
}
bool f_destroy(EmfWorld*, uint32_t e) {
    g->destroyed.push_back(e);
    return true;
}
bool f_on_event(uint8_t kind, const char* name, uint32_t expected, EmfWorldEventCallback cb, void* user) {
    g->subs.push_back({kind, name, cb, user, expected});
    return true;
}
bool f_program_def(const EmfProgramDef* d) {
    Fake::Program p{d->name, d->world_kind, {}, d->fn, d->user};
    for (const char* const* e = d->events_out; e && *e; ++e) p.out.push_back(*e);
    g->programs.push_back(p);
    return true;
}
void* f_emit(void*, const char* name, uint32_t size) {
    g->emitted.push_back({name, std::vector<uint8_t>(size, 0)});
    return g->emitted.back().second.data();
}
bool f_wheel(const EmfWheel* w) {
    Fake::Wheel x{w->title ? w->title : "", {}, w->hold_key, w->cb, w->user};
    for (uint32_t i = 0; i < w->count; ++i) x.items.push_back(w->items[i]);
    g->wheels.push_back(x);
    return true;
}

void f_log(const char* mod, const char* fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (g->log_hook) g->log_hook(b);
    g->log.push_back(std::string("[") + mod + "] " + b);
}
void f_notify(const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    g->notes.push_back(b);
}
void f_reply(const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    g->replies.push_back(b);
}
bool f_hud(const char* id, const char* text, uint32_t place, uint32_t rgba) {
    if (!text || !*text) g->hud.erase(id);
    else g->hud[id] = text;
    g->hud_style[id] = {place, rgba};
    return true;
}
bool f_register_command(const char* name, const char* help, EmfCommandCallback cb, void* user) {
    for (const auto& c : g->commands)
        if (_stricmp(c.name.c_str(), name) == 0) return false;  // as the core: unique ignoring case
    g->commands.push_back({name, help ? help : "", cb, user});
    return true;
}
bool f_register_command_ex(const EmfCommandDef* d) {
    for (auto& c : g->commands)
        if (_stricmp(c.name.c_str(), d->name) == 0) {
            if (c.cb != d->cb || c.user != d->user) return false;  // as the core: only the same owner updates
            c.help = d->help ? d->help : "";
            c.flags = d->flags;
            return true;
        }
    g->commands.push_back({d->name, d->help ? d->help : "", d->cb, d->user, d->flags});
    return true;
}
bool f_mod_info_at(uint32_t i, EmfModInfo* out) {
    if (i >= g->mods.size()) return false;
    *out = g->mods[i];
    return true;
}
int64_t f_setting_int(const char* mod, const char* key, int64_t fallback) {
    auto it = g->settings.find(std::string(mod) + "." + key);
    return it == g->settings.end() ? fallback : std::atoll(it->second.c_str());
}
double f_setting_float(const char* mod, const char* key, double fallback) {
    auto it = g->settings.find(std::string(mod) + "." + key);
    return it == g->settings.end() ? fallback : std::atof(it->second.c_str());
}
uint32_t f_setting_text(const char* mod, const char* key, char* buf, uint32_t size) {
    auto it = g->settings.find(std::string(mod) + "." + key);
    if (it == g->settings.end()) {
        if (size) buf[0] = 0;
        return 0;
    }
    std::snprintf(buf, size, "%s", it->second.c_str());
    return static_cast<uint32_t>(it->second.size());
}
bool f_setting_set(const char* mod, const char* key, const char* value) {
    g->settings[std::string(mod) + "." + key] = value;
    return true;
}
void f_on_setting_changed(const char* mod, EmfSettingCallback cb, void* user) {
    g->setting_watchers.push_back({mod ? mod : "", {cb, user}});
}
bool f_command_sender(EmfCommandSender* out) {
    if (!g->sender.size || !out || out->size < sizeof(EmfCommandSender)) return false;
    *out = g->sender;
    return true;
}
uint8_t f_kind(EmfWorld* w) { return static_cast<uint8_t>(reinterpret_cast<uintptr_t>(w) & 3); }
void f_on_load(EmfSaveCallback cb, void* user) { g->load_cbs.push_back({cb, user}); }
void* f_singleton(EmfWorld*, const char* name) {
    if (auto it = g->singletons.find(name); it != g->singletons.end()) return it->second.data();
    if (std::strcmp(name, "BasicPlayerInfos") != 0 || g->player_infos.empty()) return nullptr;
    return g->player_infos.data();
}
bool f_attr_get(EmfWorld*, uint32_t e, uint32_t id, int32_t* out) {
    auto it = g->attrs.find({e, id});
    if (it == g->attrs.end()) return false;
    *out = it->second;
    return true;
}
bool f_attr_add(EmfWorld*, uint32_t e, uint32_t id, int32_t delta) {
    auto it = g->attrs.find({e, id});
    if (it == g->attrs.end()) return false;
    g->attr_adds.emplace_back(e, id, delta);
    const int64_t v = static_cast<int64_t>(it->second) + delta;  // the core floors at 0
    it->second = static_cast<int32_t>(v < 0 ? 0 : v);
    return true;
}
bool f_damage(EmfWorld*, uint32_t attacker, uint32_t target, int32_t amount) {
    g->damages.emplace_back(attacker, target, amount);
    return amount > 0;
}
void* f_resolve(const char* name) {
    static int fn = 0;
    return std::strcmp(name, "AttrFind") == 0 || std::strcmp(name, "AttrAdd") == 0 ? &fn : nullptr;
}
// The v18 attribute functions of a core on a game build where they were found (fake_api leaves them out, as a core
// on a build without the signatures would answer: emf.kill then takes the old path).
void with_attributes(EmfApi& a) {
    a.attribute_get = f_attr_get;
    a.attribute_add = f_attr_add;
    a.damage = f_damage;
    a.resolve = f_resolve;
}
bool f_teleport(EmfWorld*, const EmfTeleport* t) {
    g->teleports.push_back(*t);
    return g->teleport_ok;
}
// A v20 core (fake_api is older for the Lua host's teleport: without it emf.teleport answers false).
void with_teleport(EmfApi& a) { a.teleport = f_teleport; }
// v22 levels: 1000 XP per level, cap 25; player_level_set is recorded in attr_adds as (player, level, xp).
uint32_t f_xp_required(EmfWorld*, uint32_t level) { return level >= 1 && level <= 25 ? 1000u * level : 0u; }
uint32_t f_level_cap(EmfWorld*) { return 25u; }
bool f_level_set(EmfWorld*, uint32_t player, uint32_t level, uint32_t xp) {
    g->attr_adds.emplace_back(player, level, static_cast<int32_t>(xp));
    return level >= 1 && level <= 25;
}
bool f_change_component(EmfWorld*, const EmfComponentChange* c) {
    g->component_changes.push_back(*c);
    const auto* p = static_cast<const uint8_t*>(c->data);
    g->component_data.emplace_back(p, p ? p + c->data_size : p);
    return g->component_ok;
}
// A v23 core (fake_api leaves change_component out: emf.add_component answers false without it).
void with_components(EmfApi& a) { a.change_component = f_change_component; }
Fake::Win copy_window(const EmfWindowDef* d) {
    Fake::Win w;
    w.id = d->id ? d->id : "";
    w.title = d->title ? d->title : "";
    w.close_key = d->close_key;
    w.place = d->place;
    w.width = d->width;
    w.kind = d->world_kind;
    w.cb = d->cb;
    w.user = d->user;
    for (uint32_t i = 0; i < d->count; ++i) {
        const EmfUiWidget& x = d->widgets[i];
        w.widgets.push_back(x);
        w.keys.emplace_back(x.key ? x.key : "");
        w.texts.emplace_back(x.text ? x.text : "");
        w.tips.emplace_back(x.tooltip ? x.tooltip : "");
    }
    return w;
}
bool f_window_open(const EmfWindowDef* d) {
    g->windows.push_back(copy_window(d));
    return true;
}
bool f_window_update(const EmfWindowDef* d) {
    g->window_updates.push_back(copy_window(d));
    return true;
}
bool f_window_close(const char* id) {
    g->windows_closed.emplace_back(id);
    return true;
}
uint32_t f_spawn_offer(EmfWorld* w, const uint8_t*, double, double, double, const EmfOffer* o, EmfOfferCallback cb,
                       void* user) {
    if (!g->offer_ok) return 0;
    const uint32_t id = g->next_offer_spawn++;
    g->offers.push_back({w, id, *o, o->text ? o->text : "", cb, user});
    return id;
}
bool f_offer(EmfWorld* w, uint32_t entity, const EmfOffer* o, EmfOfferCallback cb, void* user) {
    if (!g->offer_ok) return false;
    g->offers.push_back({w, entity, *o, o->text ? o->text : "", cb, user});
    return true;
}
bool f_offer_remove(EmfWorld*, uint32_t entity) {
    g->offers_removed.push_back(entity);
    return g->offer_remove_ok;
}
uint32_t f_image_load(const char* path) {
    g->images_loaded.emplace_back(path ? path : "");
    return g->image_ok ? g->next_image++ : 0;
}
// A v26 core (fake_api leaves image_load out: emf.image answers nil without it).
void with_images(EmfApi& a) { a.image_load = f_image_load; }
// A v25 core (fake_api leaves the offer calls out: emf.offer answers false without them).
bool f_offer_active(EmfWorld*, uint32_t) { return g->offer_active; }
void with_offers(EmfApi& a) {
    a.offer_active = f_offer_active;
    a.spawn_offer = f_spawn_offer;
    a.offer = f_offer;
    a.offer_remove = f_offer_remove;
}
// A v24 core (fake_api leaves the window calls out: emf.window answers false without them).
void with_windows(EmfApi& a) {
    a.window_open = f_window_open;
    a.window_update = f_window_update;
    a.window_close = f_window_close;
}
void with_levels(EmfApi& a) {
    a.player_xp_required = f_xp_required;
    a.player_level_cap = f_level_cap;
    a.player_level_set = f_level_set;
}
const Fake::WsField* f_ws_find(const char* key) {
    if (!key) return nullptr;
    for (const auto& f : g->ws)
        if (_stricmp(key, f.key) == 0 || _stricmp(key, f.alias) == 0) return &f;
    return nullptr;
}
bool f_ws_get(const char* key, double* value) {
    const auto* f = g->ws_world ? f_ws_find(key) : nullptr;
    if (!f || !value) return false;
    *value = f->value;
    return true;
}
bool f_ws_set(const char* key, double value, uint32_t flags) {
    const auto* f = g->ws_world ? f_ws_find(key) : nullptr;
    if (!f || value < f->min || value > f->max || (flags & ~EMF_WS_PERSIST)) return false;
    g->ws_sets.emplace_back(key, value, flags);
    return true;
}
bool f_ws_reset(const char* key, uint32_t flags) {
    if (!g->ws_world || (key && !f_ws_find(key) && _stricmp(key, "all") != 0)) return false;
    g->ws_resets.emplace_back(key ? key : "", flags);
    return true;
}
bool f_ws_on_changed(EmfWorldSettingCallback cb, void* user) {
    if (!cb) return false;
    g->ws_cbs.emplace_back(cb, user);
    return true;
}
bool f_ws_info(uint32_t index, EmfWorldSettingInfo* out) {
    if (!g->ws_world || !out || out->size < sizeof(EmfWorldSettingInfo) || index >= g->ws.size()) return false;
    const auto& f = g->ws[index];
    out->type = f.type;
    out->key = f.key;
    out->alias = f.alias;
    out->group = f.group;
    out->min = f.min;
    out->max = f.max;
    out->value = f.value;
    out->world = f.world;
    out->flags = f.value != f.world ? EMF_WS_CHANGED : 0u;
    return true;
}
// A v21 core (fake_api is older for the Lua host's world settings: without it they answer nil and false).
void with_world_settings(EmfApi& a) {
    a.world_setting_get = f_ws_get;
    a.world_setting_set = f_ws_set;
    a.world_setting_reset = f_ws_reset;
    a.on_world_setting_changed = f_ws_on_changed;
    a.world_setting_info = f_ws_info;
}
uint32_t f_session() { return g->session; }
uint32_t f_tick(EmfWorld*) { return g->tick; }
bool f_chat_tell(EmfWorld*, uint32_t player, const char* text) {
    if (!g->chat_ok) return false;
    g->tells.emplace_back(player, text ? text : "");
    return true;
}
bool f_chat_broadcast(const char* text) {
    if (!g->chat_ok) return false;
    g->broadcasts.emplace_back(text ? text : "");
    return true;
}
bool f_player_name(EmfWorld*, uint32_t player, char* buf, uint32_t size) {
    if (size) buf[0] = 0;
    auto it = g->names.find(player);
    if (it == g->names.end() || !size) return false;
    std::snprintf(buf, size, "%s", it->second.c_str());
    return true;
}
bool f_on_chat(EmfChatCallback cb, void* user) {
    if (!g->chat_ok || !cb) return false;
    g->chat_cbs.emplace_back(cb, user);
    return true;
}
bool f_request_save() {
    if (!g->chat_ok) return false;
    ++g->saves;
    return true;
}
// A player's chat line reaches the server (every on_chat listener hears it on `world`).
void fire_chat(EmfWorld* world, uint64_t account, uint32_t entity, const char* text) {
    const auto cbs = g->chat_cbs;
    for (auto& [cb, user] : cbs) cb(world, account, entity, text, user);
}
// The host loaded world save `slot` (every on_load listener hears it).
void fire_load(uint16_t slot) {
    for (auto& [cb, user] : g->load_cbs) cb(slot, user);
}

EmfApi fake_api() {
    EmfApi a{};
    a.version = EMF_API_VERSION;
    a.size = sizeof a;
    a.log = f_log;
    a.notify = f_notify;
    a.command_reply = f_reply;
    a.hud_set = f_hud;
    a.register_command = f_register_command;
    a.register_command_ex = f_register_command_ex;
    a.mod_info_at = f_mod_info_at;
    a.setting_int = f_setting_int;
    a.setting_float = f_setting_float;
    a.setting_text = f_setting_text;
    a.setting_set = f_setting_set;
    a.on_setting_changed = f_on_setting_changed;
    a.world_kind = f_kind;
    a.get_component = f_get_component;
    a.get_template_component = f_get_template_component;
    a.for_each_entity = f_for_each;
    a.find_item = f_find_item;
    a.item_info = f_item_info;
    a.give_item = f_give;
    a.put_items_ex = f_put_ex;
    a.take_items = f_take;
    a.spawn = f_spawn;
    a.spawn_ex = f_spawn_ex;
    a.destroy = f_destroy;
    a.on_event = f_on_event;
    a.register_program_def = f_program_def;
    a.emit_event = f_emit;
    a.wheel_open = f_wheel;
    a.command_sender = f_command_sender;
    a.on_load = f_on_load;
    a.server_session = f_session;
    a.singleton = f_singleton;
    a.world_tick = f_tick;
    a.chat_tell = f_chat_tell;  // v19
    a.chat_broadcast = f_chat_broadcast;
    a.player_name = f_player_name;
    a.on_chat = f_on_chat;
    a.request_save = f_request_save;
    return a;
}

// %TEMP%\emf_lua_test_<pid>_<name>. Rigs with the same name in one run share it (a restart that finds what the first
// one saved), so it is removed when the run ends, not with the Rig.
std::wstring temp_dir(const wchar_t* name) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring d = std::wstring(tmp) + L"emf_lua_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" + name;
    CreateDirectoryW(d.c_str(), nullptr);
    test_scratch::remove_at_exit(d);
    return d;
}
void write(const std::wstring& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }

// One script mod `id` with main.lua = `code` (and a mod.json with `settings_json` settings).
void add_script(const std::wstring& dir, const char* id, const std::string& code, const std::string& settings_json = "[]") {
    const std::wstring folder = dir + L"\\" + std::wstring(id, id + std::strlen(id));
    CreateDirectoryW(folder.c_str(), nullptr);
    write(folder + L"\\main.lua", code);
    write(folder + L"\\mod.json", std::string(R"({"id": ")") + id + R"(", "version": "1", "main": "main.lua", "settings": )" +
                                      settings_json + "}");
    EmfModInfo info{};
    info.size = sizeof info;
    info.status = EMF_MOD_SCRIPT;
    std::snprintf(info.id, sizeof info.id, "%s", id);
    std::snprintf(info.name, sizeof info.name, "%s", id);
    std::snprintf(info.source, sizeof info.source, "%s\\", id);
    std::snprintf(info.main, sizeof info.main, "main.lua");
    g->mods.push_back(info);
}
bool logged(const char* needle) {
    for (const auto& l : g->log)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}
EmfWorld* const kServer = reinterpret_cast<EmfWorld*>(0x10000);  // kind 0
EmfWorld* const kClient = reinterpret_cast<EmfWorld*>(0x10002);  // kind 2

struct Rig {
    Fake fake;
    EmfApi api;
    std::wstring dir;
    explicit Rig(const wchar_t* name) {
        g = &fake;
        api = fake_api();
        dir = temp_dir(name) + L"\\mods";  // <test>\mods, so <test>\config is beside it as in the game
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    ~Rig() { g = nullptr; }
    void run_command(const std::string& line) {
        std::vector<std::string> words;
        size_t p = 0;
        while (p < line.size()) {
            size_t e = line.find(' ', p);
            if (e == std::string::npos) e = line.size();
            words.push_back(line.substr(p, e - p));
            p = e + 1;
        }
        std::vector<const char*> argv;
        for (auto& w : words) argv.push_back(w.c_str());
        for (auto& c : fake.commands)
            if (c.name == words[0]) c.cb(kServer, static_cast<int>(argv.size()), argv.data(), c.user);
    }
};
}  // namespace
