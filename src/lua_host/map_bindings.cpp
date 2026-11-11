// Lua 1.26 the map (API v34): emf.map_cursor, emf.on_map_click, emf.map_action, emf.remove_map_action, emf.ground_at.
// A script's actions are "<script id>:<id>" in the core (every Lua script is the core's mod "lua") and leave with it
// (reload, pause, unload). Clicks come from the core on the render thread: they wait in each listening script's queue
// and its functions run on the next client world step (Lua never runs on the render thread).
#include <cstring>
#include <string>
#include <vector>

#include "host_impl.h"

namespace emf::lua {

namespace {

constexpr size_t kClicksQueued = 16;  // per script; more before its next client step are dropped

bool has_v34(const EmfApi* a) {
    return a && a->version >= 34 && a->size >= offsetof(EmfApi, ground_at) + sizeof(a->ground_at);
}

// ---- map actions ----
void map_action_cb_body(EmfWorld* w, const EmfMapDone& d, void* user) {
    auto* slot = static_cast<MapActionSlot*>(user);
    Script* s = slot->host->find(slot->script);
    if (!s) return;
    std::lock_guard<std::recursive_mutex> l(s->m);
    if (!s->L || s->paused) return;
    const auto it = s->map_actions.find(slot->full);
    if (it == s->map_actions.end()) return;  // removed while the player held
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, it->second);
    push_world(s->L, *s, w);
    lua_pushinteger(s->L, d.player);
    lua_pushnumber(s->L, d.x);
    lua_pushnumber(s->L, d.z);
    lua_pushboolean(s->L, d.admin != 0);
    call_lua(*s, 5);
    ++s->generation;
}

// Called by EMF: nothing may escape into the game (a Lua error is a C++ exception).
void map_action_cb(EmfWorld* w, const EmfMapDone* d, void* user) {
    try {
        if (d) map_action_cb_body(w, *d, user);
    } catch (...) {
    }
}

MapActionSlot* slot_for(Host::Impl& h, const std::string& script, const std::string& full) {
    std::lock_guard<std::mutex> l(h.map_m);
    auto& p = h.map_action_slots[full];
    if (!p) p = std::make_unique<MapActionSlot>();
    p->host = &h;
    p->script = script;
    p->full = full;
    return p.get();
}

// ---- ground_at ----
struct GroundSlot {
    Host::Impl* host;
    std::string id;
    int ref;
    uint64_t load;  // the script's load counter when asked (a reload drops the answer)
};

void ground_cb_body(EmfWorld* w, const EmfGround* g, void* user) {
    auto* slot = static_cast<GroundSlot*>(user);
    Script* s = slot->host->find(slot->id);
    if (s) {
        std::lock_guard<std::recursive_mutex> l(s->m);
        const bool same_state = s->L && slot->load == s->loads;  // the reference lives in the state that asked
        if (same_state) {
            if (!s->paused) {
                lua_rawgeti(s->L, LUA_REGISTRYINDEX, slot->ref);
                push_world(s->L, *s, w);
                const bool ok = g && g->status == EMF_GROUND_OK;
                lua_pushboolean(s->L, ok);
                lua_pushnumber(s->L, g ? g->x : 0);
                lua_pushnumber(s->L, ok ? g->y : 0);
                lua_pushnumber(s->L, g ? g->z : 0);
                lua_pushstring(s->L, g && g->why ? g->why : "");
                if (g && g->status == EMF_GROUND_NOT_LOADED) lua_pushnumber(s->L, g->hop_y);
                else lua_pushnil(s->L);
                call_lua(*s, 7);
                ++s->generation;
            }
            if (s->L) luaL_unref(s->L, LUA_REGISTRYINDEX, slot->ref);
        }
    }
    delete slot;
}

void ground_cb(EmfWorld* w, const EmfGround* g, void* user) {
    try {
        ground_cb_body(w, g, user);
    } catch (...) {
    }
}

// ---- clicks ----
void click_cb(const EmfMapClick* c, void* user) {
    auto* h = static_cast<Host::Impl*>(user);
    if (!c) return;
    std::lock_guard<std::mutex> l(h->map_m);
    for (auto& sp : h->scripts)
        if (sp->map_click_count.load(std::memory_order_acquire) && sp->map_clicks.size() < kClicksQueued)
            sp->map_clicks.push_back(*c);
}

void push_click(lua_State* L, const EmfMapClick& c) {
    lua_createtable(L, 0, 6);
    lua_pushstring(L, c.button == 1 ? "right" : "left");
    lua_setfield(L, -2, "button");
    lua_pushboolean(L, (c.mods & EMF_MAP_SHIFT) != 0);
    lua_setfield(L, -2, "shift");
    lua_pushboolean(L, (c.mods & EMF_MAP_CTRL) != 0);
    lua_setfield(L, -2, "ctrl");
    lua_pushboolean(L, (c.mods & EMF_MAP_ALT) != 0);
    lua_setfield(L, -2, "alt");
    lua_pushnumber(L, c.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, c.z);
    lua_setfield(L, -2, "z");
}

}  // namespace

// emf.map_cursor() -> x, z: the world spot under the mouse while the game's map is open; nil otherwise.
int l_map_cursor(lua_State* L) {
    Script* s = script_of(L);
    const EmfApi* a = s->host->api;
    double x = 0, z = 0;
    if (!has_v34(a) || !a->map_cursor || !a->map_cursor(&x, &z)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, z);
    return 2;
}

// emf.on_map_click(fn): fn(world, click) on the client world's next step after a click on this game's map screen.
// click: button ("left" or "right"), shift, ctrl, alt, x, z.
int l_on_map_click(lua_State* L) {
    Script* s = script_of(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    Host::Impl* h = s->host;
    int state = h->click_hooked.load();
    if (state == 0) {
        const bool ok = has_v34(h->api) && h->api->on_map_click && h->api->on_map_click(click_cb, h);
        int expected = 0;
        h->click_hooked.compare_exchange_strong(expected, ok ? 1 : -1);
        state = h->click_hooked.load();
    }
    lua_pushvalue(L, 1);
    s->map_click_cbs.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    s->map_click_count.store(static_cast<uint32_t>(s->map_click_cbs.size()), std::memory_order_release);
    lua_pushboolean(L, state == 1);
    return 1;
}

// emf.map_action(id, opts, fn(world, player, x, z, admin)): a map action players do with Shift + hold on their map (the
// server end: the host's game or the dedicated server). admin: the host's own game or a [chat] admin. opts (or nil):
// label, hold_ms. True when the core took it.
int l_map_action(lua_State* L) {
    Script* s = script_of(L);
    const char* id = luaL_checkstring(L, 1);
    const std::string full = scoped_id(L, s, id, "emf.map_action");
    std::string label = id;
    uint32_t hold_ms = 0;
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L, 2)) {
            const char* k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "";
            if (std::strcmp(k, "label") != 0 && std::strcmp(k, "hold_ms") != 0)
                return luaL_error(L, "emf.map_action: unknown option '%s' (label, hold_ms)", k);
            lua_pop(L, 1);
        }
        lua_getfield(L, 2, "label");
        if (!lua_isnil(L, -1)) {
            if (lua_type(L, -1) != LUA_TSTRING) return luaL_error(L, "emf.map_action: label must be a string");
            label = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
        lua_getfield(L, 2, "hold_ms");
        if (!lua_isnil(L, -1)) {
            if (!lua_isnumber(L, -1)) return luaL_error(L, "emf.map_action: hold_ms must be a number");
            const lua_Number v = lua_tonumber(L, -1);
            hold_ms = v <= 0 ? 0u : v >= 10000 ? 10000u : static_cast<uint32_t>(v);
        }
        lua_pop(L, 1);
    }
    luaL_checktype(L, 3, LUA_TFUNCTION);
    const EmfApi* a = s->host->api;
    bool ok = false;
    if (has_v34(a) && a->map_action) {
        MapActionSlot* slot = slot_for(*s->host, s->id, full);
        ok = a->map_action(full.c_str(), label.c_str(), hold_ms, map_action_cb, slot);
    }
    if (ok) {
        const auto old = s->map_actions.find(full);
        if (old != s->map_actions.end()) luaL_unref(L, LUA_REGISTRYINDEX, old->second);
        lua_pushvalue(L, 3);
        s->map_actions[full] = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_pushboolean(L, ok);
    return 1;
}

// emf.remove_map_action(id): true when the script had that action.
int l_remove_map_action(lua_State* L) {
    Script* s = script_of(L);
    const std::string full = scoped_id(L, s, luaL_checkstring(L, 1), "emf.remove_map_action");
    const EmfApi* a = s->host->api;
    const bool ok = has_v34(a) && a->map_action && a->map_action(full.c_str(), nullptr, 0, nullptr, nullptr);
    const auto it = s->map_actions.find(full);
    if (it != s->map_actions.end()) {
        luaL_unref(L, LUA_REGISTRYINDEX, it->second);
        s->map_actions.erase(it);
    }
    lua_pushboolean(L, ok);
    return 1;
}

// emf.ground_at(world, x, z, fn(world, ok, x, y, z, why, hop_y)): the ground under (x, z) of the server world; fn once
// with the answer (the place may take a few seconds to load). hop_y: when nobody is near the spot (not ok), a height over
// it where a player may be teleported so it loads (ask again then); nil otherwise. True when asked; false when refused.
int l_ground_at(lua_State* L) {
    Script* s = script_of(L);
    EmfWorld* w = lua_world_arg(L, 1);
    const double x = luaL_checknumber(L, 2), z = luaL_checknumber(L, 3);
    luaL_checktype(L, 4, LUA_TFUNCTION);
    const EmfApi* a = s->host->api;
    if (!has_v34(a) || !a->ground_at) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_pushvalue(L, 4);
    auto* slot = new GroundSlot{s->host, s->id, luaL_ref(L, LUA_REGISTRYINDEX), s->loads};
    const bool ok = a->ground_at(w, x, z, ground_cb, slot);
    if (!ok) {
        luaL_unref(L, LUA_REGISTRYINDEX, slot->ref);
        delete slot;
    }
    lua_pushboolean(L, ok);
    return 1;
}

void deliver_map_clicks(Host::Impl& h, Script& s, EmfWorld* world) {
    std::deque<EmfMapClick> clicks;
    {
        std::lock_guard<std::mutex> l(h.map_m);
        clicks.swap(s.map_clicks);
    }
    for (const EmfMapClick& c : clicks) {
        const std::vector<int> refs = s.map_click_cbs;  // a function may add another
        for (const int ref : refs) {
            lua_rawgeti(s.L, LUA_REGISTRYINDEX, ref);
            push_world(s.L, s, world);
            push_click(s.L, c);
            if (!call_lua(s, 2)) return;
        }
        ++s.generation;
    }
}

void drop_map(Host::Impl& h, Script& s) {
    const EmfApi* a = h.api;
    for (const auto& [full, ref] : s.map_actions) {
        if (has_v34(a) && a->map_action) a->map_action(full.c_str(), nullptr, 0, nullptr, nullptr);
        if (s.L) luaL_unref(s.L, LUA_REGISTRYINDEX, ref);
    }
    s.map_actions.clear();
    if (s.L)
        for (const int ref : s.map_click_cbs) luaL_unref(s.L, LUA_REGISTRYINDEX, ref);
    s.map_click_cbs.clear();
    s.map_click_count.store(0, std::memory_order_release);
    std::lock_guard<std::mutex> l(h.map_m);
    s.map_clicks.clear();
}

}  // namespace emf::lua
