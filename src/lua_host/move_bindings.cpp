// Lua 1.33 move orders (API v40; 1.34: stuck, peaceful = "all", API v41): emf.enemy_move(world, enemy, points [, opts] [, fn]), emf.enemy_move_cancel(id),
// emf.enemy_moving(world, enemy). fn(world, id, status) comes once when the move ends; a script paused or reloaded in
// between hears nothing (its moves end then).
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "host_impl.h"

namespace emf::lua {

namespace {

bool has_v40(const EmfApi* a) {
    return a && a->version >= 40 && a->size >= offsetof(EmfApi, enemy_moving) + sizeof(a->enemy_moving) && a->enemy_move;
}

// {x, y, z} or {x = .., y = .., z = ..} at the given index; false when it is not one
bool read_point(lua_State* L, int index, EmfPathPoint* out) {
    if (!lua_istable(L, index)) return false;
    static const char* const kKeys[3] = {"x", "y", "z"};
    float v[3];
    for (int i = 0; i < 3; ++i) {
        lua_rawgeti(L, index, i + 1);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_getfield(L, index, kKeys[i]);
        }
        const bool ok = lua_type(L, -1) == LUA_TNUMBER;
        if (ok) v[i] = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        if (!ok) return false;
    }
    *out = {v[0], v[1], v[2], 0};
    return true;
}

const char* status_name(uint32_t s) {
    switch (s) {
        case EMF_MOVE_ARRIVED: return "arrived";
        case EMF_MOVE_STUCK: return "stuck";
        case EMF_MOVE_TIMEOUT: return "timeout";
        case EMF_MOVE_GONE: return "gone";
        default: return "cancelled";
    }
}

struct MoveSlot {
    Host::Impl* host;
    std::string id;
    int ref;        // the script's function, LUA_NOREF when none
    uint64_t load;  // the script's load counter when asked (a reload drops the answer)
};

void move_cb_body(EmfWorld* w, uint32_t id, uint32_t status, void* user) {
    const std::unique_ptr<MoveSlot> slot(static_cast<MoveSlot*>(user));
    Script* s = slot->host->find(slot->id);
    if (!s) return;
    std::lock_guard<std::recursive_mutex> l(s->m);
    const bool mine = s->enemy_moves.erase(id) > 0;  // not after drop_enemy_moves (a pause or a reload)
    if (!s->L || slot->ref == LUA_NOREF) return;
    if (!mine || s->paused || slot->load != s->loads) {
        if (slot->load == s->loads) luaL_unref(s->L, LUA_REGISTRYINDEX, slot->ref);
        return;
    }
    lua_State* L = s->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, slot->ref);
    luaL_unref(L, LUA_REGISTRYINDEX, slot->ref);
    if (w) push_world(L, *s, w);
    else lua_pushnil(L);  // the world closed
    lua_pushinteger(L, id);
    lua_pushstring(L, status_name(status));
    call_lua(*s, 3);
    ++s->generation;
}

void move_cb(EmfWorld* w, uint32_t id, uint32_t status, void* user) {
    try {
        move_cb_body(w, id, status, user);
    } catch (...) {
    }
}


}  // namespace

// emf.enemy_move(world, enemy, points [, {arrive, seconds, peaceful, allies}] [, fn]): the move's id, or nil and why.
// points: one point {x, y, z} or a list of them (path_find's points fit).
int l_enemy_move(lua_State* L) {
    Script* s = script_of(L);
    EmfWorld* w = lua_world_arg(L, 1);
    const uint32_t enemy = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    luaL_checktype(L, 3, LUA_TTABLE);
    std::vector<EmfPathPoint> points;
    EmfPathPoint one{};
    if (read_point(L, 3, &one)) {
        points.push_back(one);
    } else {
        const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, 3));
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, 3, i);
            const bool ok = read_point(L, lua_gettop(L), &one);
            lua_pop(L, 1);
            if (!ok) return luaL_error(L, "emf.enemy_move: point %d is not {x, y, z}", static_cast<int>(i));
            points.push_back(one);
        }
        if (points.empty()) return luaL_error(L, "emf.enemy_move: points is {x, y, z} or a list of them");
    }
    EmfMoveOrder o{};
    o.size = sizeof o;
    o.enemy = enemy;
    int fn_index = 4;
    if (lua_isnil(L, 4) && lua_gettop(L) >= 5) fn_index = 5;  // emf.enemy_move(w, e, p, nil, fn)
    if (lua_istable(L, 4)) {  // read strictly: a typo errors at once
        fn_index = 5;
        lua_pushnil(L);
        while (lua_next(L, 4)) {
            const char* k = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "";
            const bool number = !std::strcmp(k, "arrive") || !std::strcmp(k, "seconds") || !std::strcmp(k, "stuck");
            const bool peaceful = !std::strcmp(k, "peaceful");
            const bool flag = !std::strcmp(k, "allies");
            if (!number && !flag && !peaceful)
                return luaL_error(L, "emf.enemy_move: unknown option '%s' (arrive, seconds, stuck, peaceful, allies)", k);
            if (number && lua_type(L, -1) != LUA_TNUMBER) return luaL_error(L, "emf.enemy_move: %s is a number", k);
            if (flag && !lua_isboolean(L, -1)) return luaL_error(L, "emf.enemy_move: %s is true or false", k);
            if (peaceful) {  // true / "players": its fights with players are cleared; "all": every fight
                const bool text = lua_type(L, -1) == LUA_TSTRING;
                const char* v = text ? lua_tostring(L, -1) : "";
                if (!lua_isboolean(L, -1) && !(text && (!std::strcmp(v, "players") || !std::strcmp(v, "all"))))
                    return luaL_error(L, "emf.enemy_move: peaceful is true, false, \"players\" or \"all\"");
                if (text && !std::strcmp(v, "all")) o.flags |= EMF_MOVE_PEACEFUL_ALL;
                else if (text || lua_toboolean(L, -1)) o.flags |= EMF_MOVE_PEACEFUL;
            } else if (!std::strcmp(k, "arrive")) {
                o.arrive = static_cast<float>(lua_tonumber(L, -1));
            } else if (!std::strcmp(k, "seconds")) {
                o.seconds = static_cast<float>(lua_tonumber(L, -1));
            } else if (!std::strcmp(k, "stuck")) {
                o.stuck_seconds = static_cast<float>(lua_tonumber(L, -1));
            } else if (lua_toboolean(L, -1)) {
                o.flags |= EMF_MOVE_ALLIES;
            }
            lua_pop(L, 1);
        }
    }
    const bool has_fn = !lua_isnoneornil(L, fn_index);
    if (has_fn) luaL_checktype(L, fn_index, LUA_TFUNCTION);
    const EmfApi* a = s->host->api;
    if (!has_v40(a)) {
        lua_pushnil(L);
        lua_pushstring(L, "this EMF has no move orders (EMF API v40)");
        return 2;
    }
    o.count = static_cast<uint32_t>(points.size());
    o.points = points.data();
    int ref = LUA_NOREF;
    if (has_fn) {
        lua_pushvalue(L, fn_index);
        ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    auto* slot = new MoveSlot{s->host, s->id, ref, s->loads};
    const uint32_t id = a->enemy_move(w, &o, move_cb, slot);
    if (!id) {
        if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
        delete slot;  // a refused move never calls back
        lua_pushnil(L);
        lua_pushstring(L, o.why);
        return 2;
    }
    s->enemy_moves.insert(id);
    lua_pushinteger(L, id);
    return 1;
}

// emf.enemy_move_cancel(id): true when the move was there (its function gets "cancelled").
int l_enemy_move_cancel(lua_State* L) {
    Script* s = script_of(L);
    const lua_Integer id = luaL_checkinteger(L, 1);
    const EmfApi* a = s->host->api;
    lua_pushboolean(L, has_v40(a) && id > 0 && id <= 0xffffffff && a->enemy_move_cancel(static_cast<uint32_t>(id)));
    return 1;
}

// emf.enemy_moving(world, enemy): its move's id, or nil.
int l_enemy_moving(lua_State* L) {
    Script* s = script_of(L);
    EmfWorld* w = lua_world_arg(L, 1);
    const uint32_t enemy = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    const EmfApi* a = s->host->api;
    const uint32_t id = has_v40(a) ? a->enemy_moving(w, enemy) : 0;
    if (id) lua_pushinteger(L, id);
    else lua_pushnil(L);
    return 1;
}

// A script that reloads or pauses: its moves end (their functions are not called).
void drop_enemy_moves(Host::Impl& h, Script& s) {
    const std::vector<uint32_t> ids(s.enemy_moves.begin(), s.enemy_moves.end());  // a cancel's callback edits the set
    s.enemy_moves.clear();
    for (const uint32_t id : ids)
        if (has_v40(h.api)) h.api->enemy_move_cancel(id);
}

}  // namespace emf::lua
