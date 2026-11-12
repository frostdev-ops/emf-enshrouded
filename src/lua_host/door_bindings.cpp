// Lua 1.30 doors (API v38): emf.door, emf.door_open, emf.door_close, emf.door_lock. Changes land at the next server
// step; a lock lasts until the world closes.
#include <string>

#include "host_impl.h"

namespace emf::lua {

namespace {
bool has_v38(const EmfApi* a) {
    return a && a->version >= 38 && a->size >= offsetof(EmfApi, door_lock) + sizeof(a->door_lock) && a->door_state;
}
}  // namespace

// emf.door(world, id): "open" or "closed" plus {locked = bool, logic = bool}, or nil (not a door).
int l_door(lua_State* L) {
    Script* s = script_of(L);
    EmfWorld* w = lua_world_arg(L, 1);
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    const EmfApi* a = s->host->api;
    const uint32_t st = has_v38(a) ? a->door_state(w, id) : EMF_DOOR_NONE;
    if (!(st & (EMF_DOOR_OPEN | EMF_DOOR_CLOSED))) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, (st & EMF_DOOR_OPEN) ? "open" : "closed");
    lua_createtable(L, 0, 2);
    lua_pushboolean(L, (st & EMF_DOOR_LOCKED) != 0);
    lua_setfield(L, -2, "locked");
    lua_pushboolean(L, (st & EMF_DOOR_LOGIC) != 0);
    lua_setfield(L, -2, "logic");
    return 2;
}

// emf.door_open(world, id [, "front" | "back"]) / emf.door_close(world, id): true when it will change at the next step.
int door_set(lua_State* L, bool open) {
    Script* s = script_of(L);
    EmfWorld* w = lua_world_arg(L, 1);
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    uint32_t side = 0;
    if (open && !lua_isnoneornil(L, 3)) {
        const std::string word = luaL_checkstring(L, 3);
        if (word == "front") side = 1;
        else if (word == "back") side = 2;
        else return luaL_error(L, "emf.door_open: the side is 'front' or 'back'");
    }
    const EmfApi* a = s->host->api;
    lua_pushboolean(L, has_v38(a) && a->door_set(w, id, open ? 1u : 0u, side));
    return 1;
}
int l_door_open(lua_State* L) { return door_set(L, true); }
int l_door_close(lua_State* L) { return door_set(L, false); }

// emf.door_lock(world, id, locked): true when it will be (un)locked at the next step.
int l_door_lock(lua_State* L) {
    Script* s = script_of(L);
    EmfWorld* w = lua_world_arg(L, 1);
    const uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    luaL_checktype(L, 3, LUA_TBOOLEAN);
    const bool locked = lua_toboolean(L, 3) != 0;
    const EmfApi* a = s->host->api;
    lua_pushboolean(L, has_v38(a) && a->door_lock(w, id, locked ? 1u : 0u));
    return 1;
}

}  // namespace emf::lua
