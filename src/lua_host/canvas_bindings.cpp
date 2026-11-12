// Lua 1.27 a minimap's parts (API v35): emf.player_view, emf.fog_at, emf.map_markers, emf.canvas, emf.canvas_draw. A
// script's canvases are "<script id>:<id>" in the core (every Lua script is the core's mod "lua") and leave with it
// (reload, pause, unload).
#include <cstring>
#include <string>
#include <vector>

#include "host_impl.h"

namespace emf::lua {

namespace {

bool has_v35(const EmfApi* a) {
    return a && a->version >= 35 && a->size >= offsetof(EmfApi, canvas_draw) + sizeof(a->canvas_draw);
}

void set_num(lua_State* L, const char* k, double v) {
    lua_pushnumber(L, v);
    lua_setfield(L, -2, k);
}

float num_field(lua_State* L, int t, const char* key, float def, const char* fn) {
    lua_getfield(L, t, key);
    float v = def;
    if (!lua_isnil(L, -1)) {
        if (!lua_isnumber(L, -1)) luaL_error(L, "%s: %s must be a number", fn, key);
        v = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
    return v;
}

struct Kind {
    const char* name;
    uint8_t kind;
    int numbers;  // positional numbers after the name
};
const Kind kKinds[] = {{"rect", EMF_CANVAS_RECT_CMD, 4},  {"circle", EMF_CANVAS_CIRCLE_CMD, 3}, {"line", EMF_CANVAS_LINE, 4},
                       {"tri", EMF_CANVAS_TRI, 6},         {"text", EMF_CANVAS_TEXT, 2},          {"image", EMF_CANVAS_IMAGE, 0},
                       {"fog", EMF_CANVAS_FOG, 3},         {"markers", EMF_CANVAS_MARKERS, 3},    {"map", EMF_CANVAS_MAP, 3}};

}  // namespace

// emf.player_view() -> {x, y, z, heading, camera_yaw, world_size}, or nil (no player yet). camera_yaw is nil until a
// frame of the 3D view was seen. Degrees: 0 north (+z), 90 east (+x).
int l_player_view(lua_State* L) {
    Script* s = script_of(L);
    const EmfApi* a = s->host->api;
    EmfPlayerView v{};
    v.size = sizeof v;
    if (!has_v35(a) || !a->player_view || !a->player_view(&v)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 6);
    set_num(L, "x", v.x);
    set_num(L, "y", v.y);
    set_num(L, "z", v.z);
    set_num(L, "heading", v.heading_deg);
    if (v.flags & EMF_VIEW_CAMERA) set_num(L, "camera_yaw", v.camera_yaw_deg);
    set_num(L, "world_size", v.world_size);
    return 1;
}

// emf.fog_at(x, z) -> 0 unexplored .. 1 explored on this game's map, or nil.
int l_fog_at(lua_State* L) {
    Script* s = script_of(L);
    const double x = luaL_checknumber(L, 1), z = luaL_checknumber(L, 2);
    const EmfApi* a = s->host->api;
    float e = 0;
    if (!has_v35(a) || !a->fog_at || !a->fog_at(x, z, &e)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, e);
    return 1;
}

// emf.map_markers() -> a list of {type, x, y, z, shown}: the game's own map markers in this game's world.
int l_map_markers(lua_State* L) {
    Script* s = script_of(L);
    const EmfApi* a = s->host->api;
    lua_newtable(L);
    if (!has_v35(a) || !a->map_markers) return 1;
    const uint32_t n = a->map_markers(nullptr, 0);
    std::vector<EmfGameMarker> m(n);
    const uint32_t got = n ? a->map_markers(m.data(), n) : 0;
    for (uint32_t i = 0; i < got && i < n; ++i) {
        lua_createtable(L, 0, 5);
        lua_pushinteger(L, m[i].type);
        lua_setfield(L, -2, "type");
        set_num(L, "x", m[i].x);
        set_num(L, "y", m[i].y);
        set_num(L, "z", m[i].z);
        lua_pushboolean(L, (m[i].flags & EMF_GAME_MARKER_SHOWN) != 0);
        lua_setfield(L, -2, "shown");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
    }
    return 1;
}

// emf.canvas(id, spec | nil): a drawing panel of this mod. spec: corner ("top_left", "top_right", "bottom_left",
// "bottom_right"), x, y (from the corner), w, h (1080-pixel units), shape ("rect" or "circle"), hidden. nil removes it.
int l_canvas(lua_State* L) {
    Script* s = script_of(L);
    const std::string full = scoped_id(L, s, luaL_checkstring(L, 1), "emf.canvas");
    const EmfApi* a = s->host->api;
    if (lua_isnoneornil(L, 2)) {
        const bool ok = has_v35(a) && a->canvas_set && a->canvas_set(full.c_str(), nullptr);
        s->canvases.erase(full);
        lua_pushboolean(L, ok);
        return 1;
    }
    luaL_checktype(L, 2, LUA_TTABLE);
    EmfCanvas c{};
    c.size = sizeof c;
    c.corner = EMF_CANVAS_TOP_RIGHT;
    lua_getfield(L, 2, "corner");
    if (!lua_isnil(L, -1)) {
        const char* v = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
        if (std::strcmp(v, "top_left") == 0) c.corner = EMF_CANVAS_TOP_LEFT;
        else if (std::strcmp(v, "top_right") == 0) c.corner = EMF_CANVAS_TOP_RIGHT;
        else if (std::strcmp(v, "bottom_left") == 0) c.corner = EMF_CANVAS_BOTTOM_LEFT;
        else if (std::strcmp(v, "bottom_right") == 0) c.corner = EMF_CANVAS_BOTTOM_RIGHT;
        else return luaL_error(L, "emf.canvas: corner '%s' (top_left, top_right, bottom_left, bottom_right)", v);
    }
    lua_pop(L, 1);
    lua_getfield(L, 2, "shape");
    if (!lua_isnil(L, -1)) {
        const char* v = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
        if (std::strcmp(v, "circle") == 0) c.shape = EMF_CANVAS_CIRCLE;
        else if (std::strcmp(v, "rect") == 0) c.shape = EMF_CANVAS_RECT;
        else return luaL_error(L, "emf.canvas: shape '%s' (rect or circle)", v);
    }
    lua_pop(L, 1);
    c.x = num_field(L, 2, "x", 16, "emf.canvas");
    c.y = num_field(L, 2, "y", 16, "emf.canvas");
    c.w = num_field(L, 2, "w", 200, "emf.canvas");
    c.h = num_field(L, 2, "h", 200, "emf.canvas");
    lua_getfield(L, 2, "hidden");
    c.hidden = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);
    const bool ok = has_v35(a) && a->canvas_set && a->canvas_set(full.c_str(), &c);
    if (ok) s->canvases.insert(full);
    lua_pushboolean(L, ok);
    return 1;
}

// emf.canvas_draw(id, list): the canvas's drawing, replacing the last. Each item: {kind, numbers..., colour = 0xRRGGBBAA,
// width = n, size = n, turn = degrees}: "rect" x y w h, "circle" cx cy r, "line" x1 y1 x2 y2, "tri" x1 y1 x2 y2 x3 y3,
// "text" x y "text", "image" picture x y w h, "fog" cx cz metres_per_unit, "markers" cx cz metres_per_unit, "map" cx cz
// metres_per_unit. Canvas units (0, 0 its top left). An unknown kind is an error.
int l_canvas_draw(lua_State* L) {
    Script* s = script_of(L);
    const std::string full = scoped_id(L, s, luaL_checkstring(L, 1), "emf.canvas_draw");
    luaL_checktype(L, 2, LUA_TTABLE);
    const lua_Integer n = luaL_len(L, 2);
    if (n > 2048) return luaL_error(L, "emf.canvas_draw: more than 2048 items");
    std::vector<EmfCanvasCmd> cmds;
    std::vector<std::string> texts;
    cmds.reserve(static_cast<size_t>(n));
    texts.reserve(static_cast<size_t>(n));
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, 2, i);
        const int t = lua_gettop(L);
        if (!lua_istable(L, t)) return luaL_error(L, "emf.canvas_draw: item %d is not a table", static_cast<int>(i));
        lua_rawgeti(L, t, 1);
        const char* name = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
        const Kind* k = nullptr;
        for (const Kind& x : kKinds)
            if (std::strcmp(x.name, name) == 0) k = &x;
        lua_pop(L, 1);
        if (!k)
            return luaL_error(L, "emf.canvas_draw: item %d: unknown kind '%s' (rect, circle, line, tri, text, image, "
                                 "fog, markers, map)", static_cast<int>(i), name);
        EmfCanvasCmd c{};
        c.kind = k->kind;
        c.rgba = 0xffffffffu;
        int pos = 2;
        if (k->kind == EMF_CANVAS_IMAGE) {
            lua_rawgeti(L, t, pos++);
            c.image = static_cast<uint32_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);
            for (int j = 0; j < 4; ++j) {
                lua_rawgeti(L, t, pos++);
                c.a[j] = static_cast<float>(lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
            c.a[6] = c.a[7] = 0;  // the whole picture
            c.a[8] = num_field(L, t, "turn", 0, "emf.canvas_draw");
        } else {
            for (int j = 0; j < k->numbers; ++j) {
                lua_rawgeti(L, t, pos++);
                c.a[j] = static_cast<float>(lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
        }
        if (k->kind == EMF_CANVAS_TEXT) {
            lua_rawgeti(L, t, pos);
            texts.emplace_back(lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "");
            lua_pop(L, 1);
            c.a[2] = num_field(L, t, "size", 18, "emf.canvas_draw");
        }
        const float width = num_field(L, t, "width", 0, "emf.canvas_draw");
        if (k->kind == EMF_CANVAS_RECT_CMD || k->kind == EMF_CANVAS_LINE) c.a[4] = width;
        if (k->kind == EMF_CANVAS_CIRCLE_CMD) c.a[3] = width;
        if (k->kind == EMF_CANVAS_LINE && width <= 0) c.a[4] = 1;
        lua_getfield(L, t, "colour");
        if (!lua_isnil(L, -1)) c.rgba = static_cast<uint32_t>(static_cast<int64_t>(lua_tonumber(L, -1)));
        lua_pop(L, 1);
        lua_pop(L, 1);  // the item
        cmds.push_back(c);
    }
    size_t ti = 0;  // the texts live in `texts` (reserved: no reallocation)
    for (EmfCanvasCmd& c : cmds)
        if (c.kind == EMF_CANVAS_TEXT) c.s = texts[ti++].c_str();
    const EmfApi* a = s->host->api;
    const bool ok = has_v35(a) && a->canvas_draw &&
                    a->canvas_draw(full.c_str(), cmds.data(), static_cast<uint32_t>(cmds.size()));
    lua_pushboolean(L, ok);
    return 1;
}

void drop_canvases(Host::Impl& h, Script& s) {
    const EmfApi* a = h.api;
    if (has_v35(a) && a->canvas_set)
        for (const auto& id : s.canvases) a->canvas_set(id.c_str(), nullptr);
    s.canvases.clear();
}

}  // namespace emf::lua
