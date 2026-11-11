#include "host_impl.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "emf/keys.hpp"
#include "lua_host/lua_json.h"

namespace emf::lua {

namespace {

constexpr uint64_t kCallLimitMs = 250;
// A client or prediction step leaves out a script busy in another world's step at most this many steps in a row (half a
// second at 60 steps a second), then waits for it.
constexpr uint32_t kMaxBusySkips = 30;
constexpr uint64_t kSaveCreditMs = 1000;  // world-data writes left out of the time limit, per call
constexpr size_t kBusMaxTopic = 96;
constexpr size_t kBusMaxPayload = 64 * 1024;
constexpr size_t kBusMaxPending = 4096;
constexpr size_t kBusMaxPerMod = 256;
constexpr const char* kScriptKey = "emf.script";

std::wstring wide(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool read_file(const std::wstring& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

uint64_t mtime(const WIN32_FIND_DATAW& fd) {
    return (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
}

// Every .lua file and mod.json under `folder` (subfolders too) with its last write time.
void scan(const std::wstring& folder, std::map<std::wstring, uint64_t>& out, int depth = 0) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((folder + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring path = folder + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 4) scan(path, out, depth + 1);
        } else if (name == L"mod.json" ||
                   (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".lua") == 0)) {
            out[path] = mtime(fd);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// The allocator of every script state: counts its bytes and, inside protected calls, refuses to grow past the limit
// (Lua then collects garbage, tries again, and raises "not enough memory"). Freeing and shrinking always work.
void* capped_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* m = static_cast<Memory*>(ud);
    if (!ptr) osize = 0;  // for a new block osize is the kind of object, not a size
    if (nsize == 0) {
        std::free(ptr);
        m->used -= osize;
        return nullptr;
    }
    if (nsize > osize && m->armed && m->used - osize + nsize > m->limit) return nullptr;
    void* p = std::realloc(ptr, nsize);
    if (p) m->used = m->used - osize + nsize;
    return p;
}

// The clock of the time limit: the host's (tests replace it).
uint64_t clock_ms(const Script& s) { return s.host && s.host->now_ms ? s.host->now_ms() : GetTickCount64(); }

double perf_now(const Host::Impl& h) { return h.perf_clock ? h.perf_clock() : emf::perf_clock_ms(); }

const char* world_name(int kind) {
    return kind == EMF_WORLD_SERVER ? "server" : kind == EMF_WORLD_CLIENT ? "client" : "prediction";
}

// One script's work in one world step, for perf_report; a step over 25 ms is logged at most once a minute per script
// (by the scripts' clock), so a player's own session leaves a trace in the log.
struct ScriptStepTimer {
    Host::Impl& h;
    const Script& s;
    int kind;
    double start;
    ScriptStepTimer(Host::Impl& host, const Script& script, int k)
        : h(host), s(script), kind(k), start(perf_now(host)) {}
    ScriptStepTimer(const ScriptStepTimer&) = delete;
    ScriptStepTimer& operator=(const ScriptStepTimer&) = delete;
    ~ScriptStepTimer() {
        try {
            report();
        } catch (...) {  // out of memory: no timing this step (a destructor must not throw)
        }
    }
    void report() {
        const double ms = perf_now(h) - start;
        std::string& name = const_cast<Script&>(s).perf_names[kind >= 0 && kind < 3 ? kind : 0];
        if (name.empty()) name = s.id + " (" + world_name(kind) + ")";
        h.perf.add(name, ms);
        if (ms <= 25.0) return;
        const uint64_t now = clock_ms(s);
        bool say = false;
        {
            std::lock_guard<std::mutex> l(h.slow_m);
            auto it = h.slow_said.find(s.id);
            say = it == h.slow_said.end() || now - it->second >= 60000;
            if (say) h.slow_said[s.id] = now;
        }
        if (say && h.api && h.api->log)
            h.api->log("lua", "slow: %s took %.0f ms in one world step (said at most once a minute; lua.perf in the "
                              "console has the numbers)", s.id.c_str(), ms);
    }
};

bool over_time(const Script* s) { return s && s->call_start && clock_ms(*s) - s->call_start > kCallLimitMs; }

// Every 10000 instructions: past the limit, raise an error naming where the script was running.
void count_hook(lua_State* L, lua_Debug* ar) {
    Script* s = script_of(L);
    if (!over_time(s)) return;
    std::string where;
    if (lua_getinfo(L, "Sl", ar) && ar->currentline > 0) where = std::string(ar->short_src) + ":" + std::to_string(ar->currentline) + ": ";
    s->timeout = where + "the script ran longer than " + std::to_string(kCallLimitMs) + " ms (an endless loop?)";
    lua_pushstring(L, s->timeout.c_str());
    lua_error(L);
}

// After pcall/xpcall/coroutine.resume return: a time limit they caught is raised again, so nothing can swallow it.
int l_deadline(lua_State* L) {
    Script* s = script_of(L);
    if (!over_time(s)) return 0;
    if (s->timeout.empty())
        s->timeout = "the script ran longer than " + std::to_string(kCallLimitMs) + " ms (an endless loop?)";
    lua_pushstring(L, s->timeout.c_str());
    return lua_error(L);
}

// Run once in each new state (with l_deadline as its argument). It removes what can end the game, launch programs,
// load bytecode or lift the time limit; file access stays (mods keep their data in files).
constexpr const char* kSandbox = R"lua(
local deadline = ...
os.exit = nil
os.execute = nil
io.popen = nil
package.loadlib = nil
package.cpath = ''
debug = { traceback = debug.traceback }
package.loaded.debug = debug
local raw_load, raw_loadfile, raw_pcall, raw_xpcall, raw_resume = load, loadfile, pcall, xpcall, coroutine.resume
load = function(chunk, name, mode, ...)
  if select('#', ...) > 0 then return raw_load(chunk, name, 't', ...) end
  return raw_load(chunk, name, 't')
end
loadfile = function(file, mode, ...)
  if select('#', ...) > 0 then return raw_loadfile(file, 't', ...) end
  return raw_loadfile(file, 't')
end
dofile = function(file)
  local f = assert(raw_loadfile(file, 't'))
  return f()
end
local function after(...) deadline() return ... end
pcall = function(...) return after(raw_pcall(...)) end
xpcall = function(...) return after(raw_xpcall(...)) end
coroutine.resume = function(...) return after(raw_resume(...)) end
local searchpath, fmt = package.searchpath, string.format
package.searchers = { package.searchers[1], function(name)
  local path, err = searchpath(name, package.path)
  if not path then return '\n\t' .. err end
  local f, e = raw_loadfile(path, 't')
  if not f then error(fmt("error loading module '%s' from file '%s':\n\t%s", name, path, e), 0) end
  return f, path
end }
)lua";

// A command or HUD id under the mod's name: "<id>.<name>". A name that already starts with "<id>." stays, and (lua 1.6) a
// name that is exactly the mod's id stays too: emf.command(emf.id, ...) makes the command "/<id>".
std::string prefixed(const Script& s, const std::string& name) {
    const std::string p = s.id + ".";
    return name.rfind(p, 0) == 0 || name == s.id ? name : p + name;
}

// ---- emf.* functions ----
int l_log(lua_State* L) {
    Script* s = script_of(L);
    std::string line;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) line += (i > 1 ? " " : "") + to_text(L, i);
    s->host->api->log(s->id.c_str(), "%s", line.c_str());
    return 0;
}

int l_notify(lua_State* L) {
    Script* s = script_of(L);
    const std::string text = to_text(L, 1);
    if (s->host->api->notify) s->host->api->notify("%s", text.c_str());
    return 0;
}

int l_reply(lua_State* L) {
    Script* s = script_of(L);
    const std::string text = to_text(L, 1);
    if (s->host->api->command_reply) s->host->api->command_reply("%s", text.c_str());
    return 0;
}

std::string lower_text(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// A HUD corner: 0-2, or a select setting's value "top_right", "bottom_right", "bottom_left" (spaces too, any case).
uint32_t hud_place(lua_State* L, int index) {
    if (lua_isnoneornil(L, index)) return EMF_HUD_TOP_RIGHT;
    if (lua_type(L, index) == LUA_TSTRING) {
        std::string p = lower_text(lua_tostring(L, index));
        for (auto& c : p)
            if (c == ' ' || c == '-') c = '_';
        if (p == "top_right") return EMF_HUD_TOP_RIGHT;
        if (p == "bottom_right") return EMF_HUD_BOTTOM_RIGHT;
        if (p == "bottom_left") return EMF_HUD_BOTTOM_LEFT;
        if (!lua_isnumber(L, index))
            return luaL_error(L, "the corner is \"top_right\", \"bottom_right\" or \"bottom_left\" (or 0, 1, 2), not \"%s\"",
                              lua_tostring(L, index)), 0u;
    }
    return static_cast<uint32_t>(luaL_checkinteger(L, index));
}

// A colour: 0xRRGGBBAA, or a color setting's "#rrggbb" ("#rrggbbaa" with alpha); nothing or 0 = the usual colour.
uint32_t hud_rgba(lua_State* L, int index) {
    if (lua_isnoneornil(L, index)) return 0;
    if (lua_type(L, index) == LUA_TSTRING) {  // text is a colour ("112233" too, not the number 112233)
        std::string h = lua_tostring(L, index);
        if (h.size() > 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X'))  // "0xRRGGBBAA", as lua 1.2 read it
            return static_cast<uint32_t>(luaL_checkinteger(L, index));
        if (!h.empty() && h[0] == '#') h.erase(0, 1);
        const bool hex = !h.empty() && h.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
        if (!hex || (h.size() != 6 && h.size() != 8))
            return luaL_error(L, "a colour is \"#rrggbb\" (a color setting's value) or 0xRRGGBBAA, not \"%s\"",
                              lua_tostring(L, index)), 0u;
        const auto v = static_cast<uint32_t>(std::strtoul(h.c_str(), nullptr, 16));
        return h.size() == 6 ? (v << 8) | 0xFFu : v;
    }
    return static_cast<uint32_t>(luaL_checkinteger(L, index));
}

int l_hud(lua_State* L) {
    Script* s = script_of(L);
    const std::string id = prefixed(*s, luaL_checkstring(L, 1));
    const char* text = luaL_optstring(L, 2, "");
    const uint32_t place = hud_place(L, 3);
    const uint32_t rgba = hud_rgba(L, 4);
    const bool ok = s->host->api->hud_set && s->host->api->hud_set(id.c_str(), text, place, rgba);
    if (text[0]) s->hud.insert(id);
    else s->hud.erase(id);
    lua_pushboolean(L, ok);
    return 1;
}

// `key` names one of the mod's keybind settings (its value counts, read again at every look) or a key ("G", "F7").
bool key_arg(const Script& s, const std::string& key, std::string& setting, uint32_t& vk) {
    auto t = s.setting_types.find(key);
    if (t != s.setting_types.end() && t->second == core::SettingType::Key) {
        setting = key;
        return true;
    }
    vk = emf::keys::parse_key(wide(key));
    return vk != 0;
}

// The key a watch currently stands for (0: none, e.g. a keybind setting the player cleared).
uint32_t current_vk(const Script& s, const std::string& setting, uint32_t vk) {
    if (setting.empty()) return vk;
    const EmfApi* a = s.host->api;
    if (!a->setting_text) return 0;
    char buf[64] = {};
    a->setting_text(s.id.c_str(), setting.c_str(), buf, sizeof buf);
    return buf[0] ? emf::keys::parse_key(wide(buf)) : 0;
}

bool held(const Script& s, uint32_t vk) { return vk && s.host->key_down && s.host->key_down(vk); }

// emf.key_down(key): true while the key (or the key in one of your keybind settings) is held, with the game in front
int l_key_down(lua_State* L) {
    Script* s = script_of(L);
    const std::string key = luaL_checkstring(L, 1);
    std::string setting;
    uint32_t vk = 0;
    if (!key_arg(*s, key, setting, vk)) return luaL_error(L, "%s is not a key or one of your keybind settings", key.c_str());
    lua_pushboolean(L, held(*s, current_vk(*s, setting, vk)));
    return 1;
}

// emf.on_key(key, fn [, "server"/"client"]): fn(world) once per press, after the next step of the server world when
// this game hosts one (else of the client world), or of the world kind given.
int l_on_key(lua_State* L) {
    Script* s = script_of(L);
    const std::string key = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    KeyWatch w;
    if (!lua_isnoneornil(L, 3)) {
        const std::string k = luaL_checkstring(L, 3);
        if (k == "server") w.kind = EMF_WORLD_SERVER;
        else if (k == "client") w.kind = EMF_WORLD_CLIENT;
        else return luaL_error(L, "the world kind is \"server\" or \"client\", not \"%s\"", k.c_str());
    }
    if (!key_arg(*s, key, w.setting, w.vk)) return luaL_error(L, "%s is not a key or one of your keybind settings", key.c_str());
    w.was_down = held(*s, current_vk(*s, w.setting, w.vk));  // a key held while the mod (re)loads is not a press
    lua_pushvalue(L, 2);
    w.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    s->keys.push_back(w);
    return 0;
}

int l_on_tick(lua_State* L) {
    Script* s = script_of(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    s->ticks.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}

int bus_kind_arg(lua_State* L, int index) {
    if (lua_isnoneornil(L, index)) return EMF_WORLD_SERVER;
    const std::string k = luaL_checkstring(L, index);
    if (k == "server") return EMF_WORLD_SERVER;
    if (k == "client") return EMF_WORLD_CLIENT;
    return luaL_error(L, "the world kind is \"server\" or \"client\", not \"%s\"", k.c_str()), EMF_WORLD_SERVER;
}

bool bus_topic_ok(const std::string& topic) {
    if (topic.empty() || topic.size() > kBusMaxTopic) return false;
    for (unsigned char ch : topic)
        if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

// Length-aware: an embedded NUL in a topic must be rejected, not silently truncate it.
std::string bus_topic_arg(lua_State* L, int index) {
    size_t n = 0;
    const char* text = luaL_checklstring(L, index, &n);
    return std::string(text, n);
}

// A rejected publication is logged once until that publisher gets a successful publish again.
// Validation refusals use the same rule as queue-limit refusals and do not pause the mod.
bool bus_refuse(Script& s, const char* reason) {
    Host::Impl& h = *s.host;
    bool warn = false;
    {
        std::lock_guard<std::mutex> lock(h.m);
        warn = h.bus_refusal_logged.insert(s.id).second;
    }
    if (warn) h.api->log(s.id.c_str(), "emf.bus.publish refused: %s", reason);
    return false;
}

// emf.bus.subscribe(topic, fn [, "server"/"client"]): fn(world, source_mod_id, payload).
// During the initial load all mods can receive startup publications, regardless of load order.
// After startup, subscriptions only receive future publications (not a pre-reload backlog).
int l_bus_subscribe(lua_State* L) {
    Script* s = script_of(L);
    const std::string topic = bus_topic_arg(L, 1);
    if (!bus_topic_ok(topic)) return luaL_error(L, "bus topic must be 1..96 printable bytes");
    luaL_checktype(L, 2, LUA_TFUNCTION);

    BusSubscription sub;
    sub.kind = bus_kind_arg(L, 3);
    {
        std::lock_guard<std::mutex> lock(s->host->m);
        sub.first_sequence = (!s->host->load_done && s->loads == 1) ? 0 : s->host->bus_next_sequence + 1;
    }
    sub.token = ++s->next_bus_subscription;
    lua_pushvalue(L, 2);
    sub.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    s->bus_subs[topic].push_back(sub);
    if (sub.kind >= 0 && sub.kind < 3) ++s->bus_kind_subs[sub.kind];
    lua_pushboolean(L, true);
    return 1;
}

// Remove all callbacks for this topic, in both worlds. Messages published before a subsequent
// subscribe are ignored by the replacement callback, including messages drained by this tick.
int l_bus_unsubscribe(lua_State* L) {
    Script* s = script_of(L);
    const std::string topic = bus_topic_arg(L, 1);
    if (!bus_topic_ok(topic)) return luaL_error(L, "bus topic must be 1..96 printable bytes");
    auto it = s->bus_subs.find(topic);
    const bool found = it != s->bus_subs.end();
    if (found) {
        for (const auto& sub : it->second) {
            luaL_unref(L, LUA_REGISTRYINDEX, sub.ref);
            if (sub.kind >= 0 && sub.kind < 3) --s->bus_kind_subs[sub.kind];
        }
        s->bus_subs.erase(it);
    }
    lua_pushboolean(L, found);
    return 1;
}

// emf.bus.publish(topic, payload [, "server"/"client"]): copy bytes to a bounded FIFO.
// A publication is delivered only on the next step of that world, never during its callback.
int l_bus_publish(lua_State* L) {
    Script* s = script_of(L);
    const std::string topic = bus_topic_arg(L, 1);
    size_t n = 0;
    const char* payload = luaL_checklstring(L, 2, &n);
    const int kind = bus_kind_arg(L, 3);
    if (!bus_topic_ok(topic)) {
        lua_pushboolean(L, bus_refuse(*s, "topic must be 1..96 printable bytes"));
        return 1;
    }
    if (n > kBusMaxPayload) {
        lua_pushboolean(L, bus_refuse(*s, "payload exceeds 65536 bytes"));
        return 1;
    }

    Host::Impl& h = *s->host;
    const char* reason = nullptr;
    bool warn = false;
    {
        std::lock_guard<std::mutex> lock(h.m);
        const auto it = h.bus_queued_by_source.find(s->id);
        const size_t own = it == h.bus_queued_by_source.end() ? 0 : it->second;
        if (own >= kBusMaxPerMod) reason = "per-mod queue is full (256 messages)";
        else if (h.bus_outbox.size() >= kBusMaxPending) reason = "global queue is full (4096 messages)";
        if (reason) {
            warn = h.bus_refusal_logged.insert(s->id).second;
        } else {
            BusMessage msg;
            msg.source = s->id;
            msg.topic = topic;
            msg.payload.assign(payload, n);
            msg.kind = kind;
            msg.sequence = ++h.bus_next_sequence;
            msg.source_epoch = h.bus_source_epoch[s->id];
            h.bus_outbox.push_back(std::move(msg));
            h.bus_queued_by_source[s->id] = own + 1;
            h.bus_refusal_logged.erase(s->id);
        }
    }
    if (reason && warn) h.api->log(s->id.c_str(), "emf.bus.publish refused: %s", reason);
    lua_pushboolean(L, !reason);
    return 1;
}

struct CommandSlot {
    Host::Impl* host;
    std::string name;
};

void command_cb_body(EmfWorld* w, int argc, const char* const* argv, void* user) {
    auto* slot = static_cast<CommandSlot*>(user);
    Host::Impl* h = slot->host;
    std::string owner;
    {
        std::lock_guard<std::mutex> l(h->m);
        auto it = h->command_owner.find(slot->name);
        if (it != h->command_owner.end()) owner = it->second;
    }
    Script* s = owner.empty() ? nullptr : h->find(owner);
    if (!s) {
        h->api->command_reply("the script that made this command is not loaded");
        return;
    }
    std::lock_guard<std::recursive_mutex> l(s->m);
    auto ref = s->commands.find(slot->name);
    if (!s->L || s->paused || ref == s->commands.end()) {
        h->api->command_reply("the script that made this command is not loaded%s",
                              s->paused ? " (it is paused: fix its error, the file reloads by itself)" : "");
        return;
    }
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, ref->second);
    push_world(s->L, *s, w);
    lua_createtable(s->L, argc > 1 ? argc - 1 : 0, 0);
    for (int i = 1; i < argc; ++i) {
        lua_pushstring(s->L, argv[i]);
        lua_rawseti(s->L, -2, i);
    }
    call_lua(*s, 2);
    ++s->generation;
}

// Called by EMF: nothing may escape into the game (a Lua error is a C++ exception).
void command_cb(EmfWorld* w, int argc, const char* const* argv, void* user) {
    try {
        command_cb_body(w, argc, argv, user);
    } catch (...) {
    }
}

// The core's rule for command names: 1..32 of [A-Za-z0-9_.-].
bool command_name_ok(const std::string& n) {
    if (n.empty() || n.size() > 32) return false;
    for (char c : n)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == '-'))
            return false;
    return true;
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Registers (or, for the same slot, re-declares: a reload may change the flags) a command with the core. The lua mod
// needs API v17 (mods/lua/mod.json), so register_command_ex is always there.
bool register_with_core(Host::Impl* h, const std::string& name, const char* help, void* slot, bool players) {
    const EmfApi* api = h->api;
    EmfCommandDef d{};
    d.size = sizeof d;
    d.flags = players ? EMF_COMMAND_PLAYERS : 0u;
    d.name = name.c_str();
    d.help = help;
    d.cb = command_cb;
    d.user = slot;
    return api->register_command_ex(&d);
}

// emf.command(name, help, fn [, {players = true}]): true when added; false (logged) for a name the console cannot take.
// players (lua 1.4, API v17): any player may run it from the game's chat, not only the [chat] admins.
int l_command(lua_State* L) {
    Script* s = script_of(L);
    const std::string name = prefixed(*s, luaL_checkstring(L, 1));
    const char* help = luaL_optstring(L, 2, "");
    luaL_checktype(L, 3, LUA_TFUNCTION);
    bool players = false;
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);
        lua_getfield(L, 4, "players");
        players = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    Host::Impl* h = s->host;
    if (!command_name_ok(name)) {  // the rest of the mod still runs
        h->api->log(s->id.c_str(),
                    "emf.command: %s (%zu characters) is not added: command names are 1-32 letters, digits, _ . or -, "
                    "counting the \"%s.\" in front",
                    name.c_str(), name.size(), s->id.c_str());
        if (h->api->notify) h->api->notify("Lua mod %s: the command %s is not added (see the log)", s->id.c_str(), name.c_str());
        lua_pushboolean(L, false);
        return 1;
    }
    const std::string key = lower(name);  // the console ignores case: "Greet" after a reload is still "greet"
    {
        std::lock_guard<std::mutex> l(h->m);
        auto it = h->command_owner.find(key);
        if (it != h->command_owner.end() && it->second != s->id)
            return luaL_error(L, "the command %s belongs to the mod %s", name.c_str(), it->second.c_str());
        if (it == h->command_owner.end()) {
            auto* slot = new CommandSlot{h, key};  // lives for the whole process (commands are never removed)
            if (!register_with_core(h, name, help, slot, players)) {
                delete slot;
                return luaL_error(L, "the command %s could not be added (taken by another mod)", name.c_str());
            }
            h->command_slots[key] = slot;
        } else if (auto slot = h->command_slots.find(key); slot != h->command_slots.end()) {
            register_with_core(h, name, help, slot->second, players);  // again after a reload: help and flags
        }
        h->command_owner[key] = s->id;
    }
    lua_pushvalue(L, 3);
    auto old = s->commands.find(key);
    if (old != s->commands.end()) luaL_unref(L, LUA_REGISTRYINDEX, old->second);
    s->commands[key] = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushboolean(L, true);
    return 1;
}

int l_local_player(lua_State* L) {
    Script* s = script_of(L);
    lua_pushinteger(L, s->host->api->local_player ? s->host->api->local_player() : 0);
    return 1;
}

// emf.sender(): who typed the running command, {chat, entity, account, admin} (account as "0x" + 16 hex digits), or nil
// outside a command
int l_sender(lua_State* L) {
    Script* s = script_of(L);
    EmfCommandSender who{};
    who.size = sizeof who;
    const EmfApi* api = s->host->api;
    const bool has = api->size >= offsetof(EmfApi, command_sender) + sizeof(api->command_sender) && api->command_sender;
    if (!has || !api->command_sender(&who)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 4);
    lua_pushboolean(L, who.from_chat != 0);
    lua_setfield(L, -2, "chat");
    lua_pushboolean(L, who.admin != 0);  // may run every command (lua 1.4)
    lua_setfield(L, -2, "admin");
    lua_pushinteger(L, who.entity);
    lua_setfield(L, -2, "entity");
    char hex[24];
    std::snprintf(hex, sizeof hex, "0x%016llx", static_cast<unsigned long long>(who.account));
    lua_pushstring(L, hex);
    lua_setfield(L, -2, "account");
    return 1;
}

// emf.on_world_data(fn): fn(world, text) once per hosted session after its save loaded (and again after a reload).
// Registered after this load already got the text (from a callback): fn gets it on the next server tick.
int l_on_world_data(lua_State* L) {
    Script* s = script_of(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    const bool had_handler = s->world_cb != LUA_NOREF;
    if (had_handler) luaL_unref(L, LUA_REGISTRYINDEX, s->world_cb);
    lua_pushvalue(L, 1);
    s->world_cb = luaL_ref(L, LUA_REGISTRYINDEX);
    // the first handler of this load gets text that was read before it existed; a handler registered again
    // (from a callback) does not get it replayed
    if (!had_handler && s->world_delivered == s->loads) s->world_pending = true;
    return 0;
}

// The time a write took since `before`, given back to the running call (up to kSaveCreditMs per call).
void credit_write(Script* s, uint64_t before) {
    const uint64_t after = clock_ms(*s);
    if (s->call_start && after > before && s->call_credit < kSaveCreditMs) {
        const uint64_t add = (std::min)(after - before, kSaveCreditMs - s->call_credit);
        s->call_start += add;
        s->call_credit += add;
    }
}

// emf.save_world_data(text): true when written (or unchanged); false before this load of the script got the running
// world's data (a reloaded script would otherwise save its fresh defaults over it) or while no hosted world runs.
// The time the write takes (a busy disk, a virus scanner) does not count toward the script's time limit, up to
// kSaveCreditMs per call (a loop of saves still ends).
int l_save_world_data(lua_State* L) {
    Script* s = script_of(L);
    size_t n = 0;
    const char* text = luaL_checklstring(L, 1, &n);
    const uint64_t before = clock_ms(*s);
    const bool saved = s->file && s->world_delivered == s->loads && s->file->save(std::string(text, n));
    credit_write(s, before);
    lua_pushboolean(L, saved);
    return 1;
}

// An account argument: "0x" and 1-16 hex digits (as world:account and on_player_joined give it). The all-zero account
// (the console's, the host's own emf.net messages') is taken: no player has it, so it reads nil and saves false.
uint64_t account_arg(lua_State* L, int i) {
    size_t n = 0;
    const char* t = luaL_checklstring(L, i, &n);
    bool ok = n > 2 && n <= 18 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X');
    uint64_t v = 0;
    for (size_t k = 2; ok && k < n; ++k) {
        const char c = t[k];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        ok = d >= 0;
        v = v * 16 + static_cast<uint64_t>(d < 0 ? 0 : d);
    }
    if (!ok) luaL_argerror(L, i, "an account: \"0x\" and up to 16 hex digits, as world:account gives it");
    return v;
}

// emf.player_data(account) (lua 1.20): the player's text in the world being played ("" when none); nil before this load
// of the script got the world's data (non-nil: a save would be taken), where no hosted world runs, for the all-zero
// account, and while the world's players file cannot be read.
int l_player_data(lua_State* L) {
    Script* s = script_of(L);
    const uint64_t account = account_arg(L, 1);
    const auto text =
        s->file && account && s->world_delivered == s->loads ? s->file->player(account) : std::nullopt;
    if (text) lua_pushlstring(L, text->data(), text->size());
    else lua_pushnil(L);
    return 1;
}

// emf.save_player_data(account, text) (lua 1.20): as save_world_data, for one player ("" removes them); false also over
// 16 KiB per player or 2 MiB for the world's file.
int l_save_player_data(lua_State* L) {
    Script* s = script_of(L);
    const uint64_t account = account_arg(L, 1);
    size_t n = 0;
    const char* text = luaL_checklstring(L, 2, &n);
    const uint64_t before = clock_ms(*s);
    const bool saved =
        s->file && s->world_delivered == s->loads && s->file->save_player(account, std::string(text, n));
    credit_write(s, before);
    lua_pushboolean(L, saved);
    return 1;
}

int l_session(lua_State* L) {
    Script* s = script_of(L);
    lua_pushinteger(L, s->host->api->server_session ? s->host->api->server_session() : 0);
    return 1;
}

// emf.session_token() (lua 1.4): "<this run of the game>-<session>". Sessions restart at 1 in every run of the game
// (and entity ids are renumbered), so data saved with emf.session() can name another entity after a restart; this
// token cannot: its first part is the game process's start time.
int l_session_token(lua_State* L) {
    Script* s = script_of(L);
    static const unsigned long long run = [] {
        FILETIME created{}, exited{}, kernel{}, user{};
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
        const unsigned long long t = (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        return t ^ (static_cast<unsigned long long>(GetCurrentProcessId()) << 48);
    }();
    char text[48];
    std::snprintf(text, sizeof text, "%llx-%u", run, s->host->api->server_session ? s->host->api->server_session() : 0u);
    lua_pushstring(L, text);
    return 1;
}

int l_setting(lua_State* L) {
    Script* s = script_of(L);
    const std::string key = luaL_checkstring(L, 1);
    auto t = s->setting_types.find(key);
    if (t == s->setting_types.end()) {
        lua_pushnil(L);
        return 1;
    }
    const EmfApi* a = s->host->api;
    switch (t->second) {
        case core::SettingType::Bool: lua_pushboolean(L, a->setting_int(s->id.c_str(), key.c_str(), 0) != 0); break;
        case core::SettingType::Int:
        case core::SettingType::Choice:
            lua_pushinteger(L, a->setting_int(s->id.c_str(), key.c_str(), 0));
            break;
        case core::SettingType::Float: lua_pushnumber(L, a->setting_float(s->id.c_str(), key.c_str(), 0.0)); break;
        default: {
            char buf[512];
            a->setting_text(s->id.c_str(), key.c_str(), buf, sizeof buf);
            lua_pushstring(L, buf);
        }
    }
    return 1;
}

int l_set_setting(lua_State* L) {
    Script* s = script_of(L);
    const std::string key = luaL_checkstring(L, 1);
    std::string value;
    if (lua_isboolean(L, 2)) value = lua_toboolean(L, 2) ? "1" : "0";
    else value = to_text(L, 2);
    lua_pushboolean(L, s->host->api->setting_set(s->id.c_str(), key.c_str(), value.c_str()));
    return 1;
}

int l_on_setting_changed(lua_State* L) {
    Script* s = script_of(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    s->setting_cbs.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}

struct SettingSlot {
    Host::Impl* host;
    std::string id;
};

void setting_cb_body(EmfWorld* w, const char*, const char* key, void* user) {
    auto* slot = static_cast<SettingSlot*>(user);
    Script* s = slot->host->find(slot->id);
    if (!s) return;
    std::lock_guard<std::recursive_mutex> l(s->m);
    if (!s->L || s->paused) return;
    const std::vector<int> cbs = s->setting_cbs;
    for (int ref : cbs) {
        lua_rawgeti(s->L, LUA_REGISTRYINDEX, ref);
        lua_pushstring(s->L, key);
        push_world(s->L, *s, w);
        if (!call_lua(*s, 2)) break;
    }
    ++s->generation;
}

// Called by EMF: nothing may escape into the game (a Lua error is a C++ exception).
void setting_cb(EmfWorld* w, const char*, const char* key, void* user) {
    try {
        setting_cb_body(w, nullptr, key, user);
    } catch (...) {
    }
}

}  // namespace

std::string utf8(const std::wstring& w) {
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

Script* script_of(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kScriptKey);
    auto* s = static_cast<Script*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return s;
}

std::string to_text(lua_State* L, int index) {
    size_t n = 0;
    const char* p = luaL_tolstring(L, index, &n);
    std::string out(p, n);
    lua_pop(L, 1);
    return out;
}

bool call_lua(Script& s, int nargs) {
    const uint64_t outer = s.call_start;
    if (!outer) {  // the outermost call sets the clock
        s.call_start = clock_ms(s);
        s.call_credit = 0;
    }
    const bool armed = s.mem.armed;
    s.mem.armed = true;  // an allocation past the limit fails in here, where Lua can raise it as an error
    const int r = lua_pcall(s.L, nargs, 0, 0);
    s.mem.armed = armed;
    if (!outer) {
        s.call_start = 0;
        s.timeout.clear();
        lua_pushnil(s.L);  // the callback's world is gone: emf.item outside a callback must not find it
        lua_setfield(s.L, LUA_REGISTRYINDEX, "emf.world_now");
    }
    if (r == LUA_OK) return true;
    std::string msg = lua_isstring(s.L, -1) ? lua_tostring(s.L, -1) : "an error without a message";
    lua_pop(s.L, 1);
    if (r == LUA_ERRMEM)
        msg += " (a Lua mod may use up to " + std::to_string((s.mem.limit + (1u << 20) - 1) >> 20) + " MB)";
    s.host->pause(s, msg);
    return false;
}

Script* Host::Impl::find(const std::string& id) {
    for (auto& s : scripts)
        if (s->id == id) return s.get();
    return nullptr;
}

void Host::Impl::drop_sections(const std::string& id) {
    std::lock_guard<std::mutex> l(m);
    for (auto& [name, sections] : wheel_groups)
        sections.erase(std::remove_if(sections.begin(), sections.end(),
                                      [&](const WheelSection& x) { return x.script == id; }),
                       sections.end());
}

void Host::Impl::drop_bus(const std::string& id) {
    std::lock_guard<std::mutex> lock(m);
    // Also invalidate publications already drained for this step but not yet delivered.
    ++bus_source_epoch[id];
    for (auto it = bus_outbox.begin(); it != bus_outbox.end();) {
        if (it->source == id) it = bus_outbox.erase(it);
        else ++it;
    }
    bus_queued_by_source.erase(id);
    bus_refusal_logged.erase(id);
}

void Host::Impl::pause(Script& s, const std::string& why) {
    drop_bus(s.id);
    // The state is kept for error reporting, but no callback from it may run again.
    // Dropping its subscriptions also releases Lua registry references immediately.
    for (const auto& [topic, subs] : s.bus_subs)
        for (const auto& sub : subs)
            if (s.L) luaL_unref(s.L, LUA_REGISTRYINDEX, sub.ref);
    s.bus_subs.clear();
    for (auto& n : s.bus_kind_subs) n = 0;
    drop_sections(s.id);  // a paused mod's wheel section is not shown (its state stays: the refs go with it later)
    drop_hit_rules(*this, s);  // its hit rules stop too: they come back when the fixed file loads
    s.paused = true;
    s.error = why;
    api->log(s.id.c_str(), "%s (the mod is paused until one of its files changes)", why.c_str());
    if (api->notify) api->notify("Lua mod %s: %s", s.id.c_str(), why.c_str());
}

bool Host::Impl::load(Script& s) {
    ++s.loads;
    s.paused = false;
    s.error.clear();
    s.mtimes.clear();
    scan(s.folder, s.mtimes);
    // settings' types from mod.json (the core declared them from the same file)
    s.setting_types.clear();
    std::string manifest;
    if (read_file(s.folder + L"\\mod.json", manifest)) {
        auto r = core::parse_manifest(manifest);
        if (!r.manifest) {  // edited while the game runs and saved with a mistake: say so, not a puzzling nil later
            pause(s, "mod.json: " + r.error + " (fix it and save: the mod reloads)");
            return false;
        }
        for (const auto& d : r.manifest->settings) s.setting_types[d.key] = d.type;
    }
    lua_State* L = luaL_newstate();  // its panic and warning functions; then the counting allocator (same heap)
    s.L = L;
    s.mem.armed = false;
    s.mem.limit = memory_limit;
    s.mem.used = static_cast<size_t>(lua_gc(L, LUA_GCCOUNT, 0)) * 1024 + static_cast<size_t>(lua_gc(L, LUA_GCCOUNTB, 0));
    lua_setallocf(L, capped_alloc, &s.mem);
    luaL_openlibs(L);
    lua_pushlightuserdata(L, &s);
    lua_setfield(L, LUA_REGISTRYINDEX, kScriptKey);
    lua_sethook(L, count_hook, LUA_MASKCOUNT, 10000);
    if (luaL_loadbufferx(L, kSandbox, std::strlen(kSandbox), "=sandbox", "t") != LUA_OK ||
        (lua_pushcfunction(L, l_deadline), lua_pcall(L, 1, 0, 0)) != LUA_OK) {
        pause(s, std::string("the Lua sandbox could not be set up: ") + (lua_isstring(L, -1) ? lua_tostring(L, -1) : "?"));
        lua_pop(L, 1);
        return false;
    }
    const std::string folder = utf8(s.folder);
    lua_getglobal(L, "package");
    lua_pushstring(L, (folder + "\\?.lua;" + folder + "\\?\\init.lua").c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
    // the emf table
    static const luaL_Reg fns[] = {{"log", l_log},
                                   {"notify", l_notify},
                                   {"reply", l_reply},
                                   {"hud", l_hud},
                                   {"on_tick", l_on_tick},
                                   {"command", l_command},
                                   {"local_player", l_local_player},
                                   {"sender", l_sender},
                                   {"session", l_session},
                                   {"session_token", l_session_token},
                                   {"on_world_data", l_on_world_data},
                                   {"save_world_data", l_save_world_data},
                                   {"player_data", l_player_data},
                                   {"save_player_data", l_save_player_data},
                                   {"setting", l_setting},
                                   {"set_setting", l_set_setting},
                                   {"on_setting_changed", l_on_setting_changed},
                                   {"on_key", l_on_key},
                                   {"key_down", l_key_down},
                                   {nullptr, nullptr}};
    luaL_newlib(L, fns);
    static const luaL_Reg bus_fns[] = {{"subscribe", l_bus_subscribe},
                                       {"publish", l_bus_publish},
                                       {"unsubscribe", l_bus_unsubscribe},
                                       {nullptr, nullptr}};
    luaL_newlib(L, bus_fns);
    lua_setfield(L, -2, "bus");
    push_json_table(L);
    lua_setfield(L, -2, "json");
    lua_pushstring(L, s.id.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, folder.c_str());
    lua_setfield(L, -2, "dir");
    open_bindings(L, s);
    lua_setglobal(L, "emf");
    lua_pushcfunction(L, l_log);
    lua_setglobal(L, "print");
    // main.lua
    std::string code;
    if (!read_file(s.folder + L"\\" + wide(s.main), code)) {
        pause(s, "cannot read " + s.main);
        return false;
    }
    const std::string chunk = "@" + s.id + "/" + s.main;
    if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK) {
        const std::string msg = lua_tostring(L, -1);
        lua_pop(L, 1);
        pause(s, msg);
        return false;
    }
    if (!call_lua(s, 0)) return false;
    ++s.generation;
    return true;
}

void Host::Impl::unload(Script& s) {
    if (!s.L) return;
    if (!s.paused) {
        // raw lookup: a metatable on _G (strict globals) must not run outside a protected call
        lua_rawgeti(s.L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        lua_pushstring(s.L, "on_unload");
        lua_rawget(s.L, -2);
        lua_remove(s.L, -2);
        if (lua_isfunction(s.L, -1)) call_lua(s, 0);
        else lua_pop(s.L, 1);
    }
    for (const auto& id : s.hud)
        if (api->hud_set) api->hud_set(id.c_str(), "", 0, 0);
    s.hud.clear();
    // its open window closes too (an id is only kept after a v24 core opened it); a copy: the CLOSED edits the set
    const std::set<std::string> windows(s.windows.begin(), s.windows.end());
    s.windows.clear();
    for (const auto& id : windows)
        if (api->window_close) api->window_close(id.c_str());
    queue_script_offers(*this, s);  // its offers come off on the next server step (only a world call may)
    drop_hit_rules(*this, s);       // its hit rules leave the core
    drop_bus(s.id);                 // also drops messages published from on_unload
    drop_sections(s.id);  // before the state (and its pick functions) goes
    lua_close(s.L);
    s.L = nullptr;
    s.world_cb = LUA_NOREF;  // its registry went with the state; the next load hands the text over again
    s.world_pending = false;
    s.ticks.clear();
    s.setting_cbs.clear();
    s.keys.clear();
    s.chat_cbs.clear();
    for (auto& v : s.player_cbs) v.clear();
    s.hit_cbs.clear();
    s.hit_cb_kinds.clear();
    s.hit_cb_count = 0;
    s.hit_cb_kinds_any = 0;
    s.player_intro.clear();
    s.ws_cbs.clear();
    s.bus_subs.clear();
    for (auto& n : s.bus_kind_subs) n = 0;
    s.commands.clear();  // the command names stay registered; they answer "not loaded" until registered again
    s.events.clear();
    for (auto& n : s.event_kind_subs) n = 0;
    ++s.generation;
}

Host::Host(const EmfApi* api, std::wstring mods_dir) : impl_(std::make_unique<Impl>()) {
    impl_->api = api;
    impl_->mods_dir = std::move(mods_dir);
    impl_->key_down = [](uint32_t vk) { return emf::keys::down(vk); };
    impl_->now_ms = [] { return GetTickCount64(); };
    impl_->perf_since = GetTickCount64();
}

std::vector<std::string> Host::perf_report(double* seconds) {
    const uint64_t now = GetTickCount64();
    const double covered = static_cast<double>(now - impl_->perf_since.exchange(now)) / 1000.0;
    if (seconds) *seconds = covered;
    return emf::perf_lines(impl_->perf.take(), covered);
}

void Host::set_perf_clock(double (*clock_ms)()) { impl_->perf_clock = clock_ms; }

Host::~Host() {
    for (auto& s : impl_->scripts) {
        std::lock_guard<std::recursive_mutex> l(s->m);
        if (s->L) {
            lua_close(s->L);
            s->L = nullptr;
        }
    }
}

void Host::load_all() {
    Impl& h = *impl_;
    for (uint32_t i = 0;; ++i) {
        EmfModInfo info{};
        info.size = sizeof info;
        if (!h.api->mod_info_at || !h.api->mod_info_at(i, &info)) break;
        if (info.status != EMF_MOD_SCRIPT) continue;
        std::string main = info.main;
        if (main.size() < 4 || _stricmp(main.c_str() + main.size() - 4, ".lua") != 0) continue;  // another host's
        auto s = std::make_unique<Script>();
        s->host = &h;
        s->id = info.id;
        s->main = main;
        std::wstring source = wide(info.source);  // UTF-8 from the core
        while (!source.empty() && (source.back() == L'\\' || source.back() == L'/')) source.pop_back();
        s->folder = h.mods_dir + L"\\" + source;
        h.scripts.push_back(std::move(s));
    }
    const std::wstring config = h.mods_dir.substr(0, h.mods_dir.find_last_of(L"\\/")) + L"\\config\\";
    for (auto& s : h.scripts) {
        s->file = std::make_unique<emf::WorldFile>(s->id);
        s->file->set_dir(config + std::wstring(s->id.begin(), s->id.end()));
        if (h.file_clock) s->file->set_clock(h.file_clock);
        s->file->attach(h.api);  // false without on_load: save_world_data then always answers false
    }
    for (auto& s : h.scripts) {
        auto* slot = new SettingSlot{&h, s->id};  // lives for the process
        if (h.api->on_setting_changed) h.api->on_setting_changed(s->id.c_str(), setting_cb, slot);
        std::lock_guard<std::recursive_mutex> l(s->m);
        if (h.load(*s)) h.api->log(s->id.c_str(), "running (%s)", s->main.c_str());
    }
    finish_load(h);
    h.load_done = true;
}

void Host::tick(EmfWorld* world) {
    try {
        tick_body(world);
    } catch (...) {
    }
}

void Host::Impl::recheck() {
    std::map<std::string, uint32_t> status;  // id -> what the core made of it after every DLL loaded
    for (uint32_t i = 0;; ++i) {
        EmfModInfo info{};
        info.size = sizeof info;
        if (!api->mod_info_at || !api->mod_info_at(i, &info)) break;
        if (!status.count(info.id) || info.status == EMF_MOD_SCRIPT) status[info.id] = info.status;
    }
    for (auto& s : scripts) {
        auto it = status.find(s->id);
        if (it == status.end() || it->second == EMF_MOD_SCRIPT || !s->not_run.empty()) continue;
        std::lock_guard<std::recursive_mutex> l(s->m);
        unload(*s);
        s->not_run = "the core did not load it (a dependency is switched off or failed; see the mods command)";
        api->log(s->id.c_str(), "stopped: %s", s->not_run.c_str());
    }
}

void Host::tick_body(EmfWorld* world) {
    Impl& h = *impl_;
    const int kind = h.api->world_kind ? h.api->world_kind(world) : EMF_WORLD_SERVER;
    // Each world kind has one delivery order even if its steps arrive on different worker threads.
    const int lock_kind = kind >= EMF_WORLD_SERVER && kind <= EMF_WORLD_CLIENT ? kind : EMF_WORLD_SERVER;
    std::lock_guard<std::mutex> step_lock(h.world_step_m[lock_kind]);
    // Freeze the publish boundary at the START of this step. Reload callbacks can publish
    // before we drain the queue below, but those publications must wait for the NEXT step.
    uint64_t ready_sequence;
    {
        std::lock_guard<std::mutex> l(h.m);
        ready_sequence = h.bus_next_sequence;
    }
    if (!h.rechecked.exchange(true)) h.recheck();
    const uint64_t now = GetTickCount64();
    const bool server = kind == EMF_WORLD_SERVER;
    if (server) h.server_seen = now;
    const uint64_t server_at = h.server_seen.load();
    // a server step this second (one stored after this step read its clock counts too)
    const bool hosting_now = server_at != 0 && (now < server_at || now - server_at < 1000);
    uint64_t last = h.last_check.load();
    // The worlds tick on their own threads: only the one that moves the clock on checks. The check takes every
    // script's lock, so while this process hosts a server world only that world checks: a client step (the game's
    // frame waits for it) must not wait for a script busy in the server world's step.
    if ((server || !hosting_now) && now > last && now - last >= 1000 &&
        h.last_check.compare_exchange_strong(last, now)) {
        const double start = perf_now(h);
        check_reload_now();
        static const std::string kReload = "reload check (once a second)";
        h.perf.add(kReload, perf_now(h) - start);
    }
    // Drain this world's FIFO once. A callback's publication stays queued until its next step.
    // Sequence watermarks invalidate pre-reload and pre-subscribe messages without stealing
    // the same publication from other mods that are still subscribed.
    std::vector<BusMessage> bus;
    {
        std::lock_guard<std::mutex> l(h.m);
        for (auto it = h.bus_outbox.begin(); it != h.bus_outbox.end();) {
            if (it->kind == kind && it->sequence <= ready_sequence) {
                auto count = h.bus_queued_by_source.find(it->source);
                if (count != h.bus_queued_by_source.end() && --count->second == 0)
                    h.bus_queued_by_source.erase(count);
                bus.push_back(std::move(*it));
                it = h.bus_outbox.erase(it);
            } else {
                ++it;
            }
        }
    }
    double lock_wait = 0;  // time this step waited for scripts busy in another world's step (perf_report)
    for (auto& s : h.scripts) {
        std::unique_lock<std::recursive_mutex> l(s->m, std::defer_lock);
        // A client (or prediction) step does not wait for a script busy in the server world's step on another thread:
        // the game's frame waits for the client world, and a server step's Lua work (a search through every entity)
        // would become a hitch. The script is left out of this step (its keys stay for the next one), at most
        // kMaxBusySkips steps in a row; a step that delivers bus messages waits for the scripts that listen on this
        // world (the messages are this step's; a callback added while the script was busy would not get them anyway).
        uint32_t& skips = s->busy_skips[lock_kind];
        const bool bus_for_it = !bus.empty() && s->bus_kind_subs[lock_kind].load() != 0;
        if (server || bus_for_it || !l.try_lock()) {
            if (!server && !bus_for_it && ++skips <= kMaxBusySkips) {
                static const std::string kLeftOut[3] = {"", "a script busy in another world, left out (prediction)",
                                                        "a script busy in another world, left out (client)"};
                h.perf.add(kLeftOut[lock_kind], 0.0);
                continue;
            }
            const double wait_start = perf_now(h);
            l.lock();
            lock_wait += perf_now(h) - wait_start;
        }
        skips = 0;
        if (!s->L || s->paused) continue;
        const ScriptStepTimer step_timer(h, *s, kind);
        if (server && s->file) {  // the world's data: once per session after its save loaded, again after a reload
            std::string text;
            bool hand = s->file->poll([&](const std::string& t) { text = t; });
            if (!hand && s->file->ready() && (s->world_delivered != s->loads || s->world_pending)) {
                text = s->file->text();  // after a reload, or to an on_world_data registered after the text came
                hand = true;
            }
            if (hand) {
                s->world_delivered = s->loads;
                s->world_pending = false;
                if (s->world_cb != LUA_NOREF) {
                    lua_rawgeti(s->L, LUA_REGISTRYINDEX, s->world_cb);
                    push_world(s->L, *s, world);
                    lua_pushlstring(s->L, text.data(), text.size());
                    if (!call_lua(*s, 2)) continue;
                }
            }
        }
        if (server && !s->player_intro.empty()) {  // after the world's data: the players already there (lua 1.20)
            deliver_player_intro(h, *s, world);
            if (!s->L || s->paused) continue;
        }
        bool bus_stopped = false;
        for (const auto& msg : bus) {
            auto it = s->bus_subs.find(msg.topic);
            if (it == s->bus_subs.end()) continue;
            // A sender may have stopped after this message was drained. Do not deliver
            // that old instance's publications to callbacks later in this same step.
            {
                std::lock_guard<std::mutex> l(h.m);
                if (h.bus_source_epoch[msg.source] != msg.source_epoch) continue;
            }
            const std::vector<BusSubscription> subs = it->second;  // callbacks may subscribe again
            for (const auto& sub : subs) {
                if (sub.kind != kind || msg.sequence < sub.first_sequence) continue;
                // unsubscribe() from an earlier callback may already have unref'd this function.
                const auto live = s->bus_subs.find(msg.topic);
                if (live == s->bus_subs.end() ||
                    std::none_of(live->second.begin(), live->second.end(),
                                 [&](const BusSubscription& x) { return x.token == sub.token; })) continue;
                lua_rawgeti(s->L, LUA_REGISTRYINDEX, sub.ref);
                push_world(s->L, *s, world);
                lua_pushlstring(s->L, msg.source.data(), msg.source.size());
                lua_pushlstring(s->L, msg.payload.data(), msg.payload.size());
                if (!call_lua(*s, 3)) {
                    bus_stopped = true;
                    break;
                }
            }
            if (bus_stopped) break;
        }
        if (bus_stopped || !s->L || s->paused) continue;

        // emf.on_key: every step of any world looks at the keys; a press goes to the world it is meant for
        bool stopped = false;
        // The clock is read under the script's lock, so a press noted by the other world's thread is never "later"
        // than this one's now; differences still saturate at 0.
        const uint64_t key_now = s->keys.empty() ? 0 : GetTickCount64();
        const auto since = [&](uint64_t t) { return key_now > t ? key_now - t : 0; };
        const uint64_t seen = h.server_seen.load();
        const bool hosting = seen != 0 && since(seen) < 1000;  // a server world stepped within the last second
        for (size_t i = 0; i < s->keys.size() && !stopped; ++i) {
            KeyWatch& k = s->keys[i];
            const bool down = held(*s, current_vk(*s, k.setting, k.vk));
            if (down && !k.was_down) k.pressed_at = key_now ? key_now : 1;
            k.was_down = down;
            if (!k.pressed_at) continue;
            if (since(k.pressed_at) > 1000) {  // no world of its kind stepped since (a client of someone else's game)
                k.pressed_at = 0;
                continue;
            }
            const int want = k.kind >= 0 ? k.kind : hosting ? EMF_WORLD_SERVER : EMF_WORLD_CLIENT;
            if (kind != want) continue;
            k.pressed_at = 0;
            const int ref = k.ref;  // the function may add keys (the vector may move)
            lua_rawgeti(s->L, LUA_REGISTRYINDEX, ref);
            push_world(s->L, *s, world);
            stopped = !call_lua(*s, 1) || !s->L || s->paused;
        }
        if (stopped) continue;
        const std::vector<int> ticks = s->ticks;
        for (int ref : ticks) {
            lua_rawgeti(s->L, LUA_REGISTRYINDEX, ref);
            push_world(s->L, *s, world);
            if (!call_lua(*s, 1)) break;
        }
        ++s->generation;
    }
    // after the scripts' ticks: a reloaded script that offered again kept its entities; the rest come off
    if (lock_wait > 0.01) h.perf.add(std::string("waiting for scripts busy in another world (") + world_name(kind) + ")", lock_wait);
    if (server) run_offer_drops(h, world);
}

void Host::check_reload_now() {
    Impl& h = *impl_;
    // Also stop a mod switched off after startup. It must not retain bus subscriptions.
    h.recheck();
    for (auto& s : h.scripts) {
        std::map<std::wstring, uint64_t> now;
        scan(s->folder, now);
        std::lock_guard<std::recursive_mutex> l(s->m);
        if (now == s->mtimes || !s->not_run.empty()) continue;
        h.unload(*s);
        if (h.load(*s)) {
            h.api->log(s->id.c_str(), "reloaded (its files changed)");
            if (h.api->notify) h.api->notify("Lua mod %s reloaded", s->id.c_str());
        }
    }
}

void Host::set_memory_limit(size_t bytes) { impl_->memory_limit = bytes; }

void Host::set_key_reader(std::function<bool(uint32_t)> down) { impl_->key_down = std::move(down); }

void Host::set_clock(std::function<uint64_t()> now_ms) { impl_->now_ms = std::move(now_ms); }

void Host::set_file_clock(uint64_t (*now_ms)()) {
    impl_->file_clock = now_ms;
    for (auto& s : impl_->scripts)
        if (s->file) s->file->set_clock(now_ms ? now_ms : &GetTickCount64);
}

size_t Host::script_count() const { return impl_->scripts.size(); }

std::vector<std::string> Host::ids() const {
    std::vector<std::string> out;
    for (const auto& s : impl_->scripts) out.push_back(s->id);
    return out;
}

std::string Host::status(std::string_view id) const {
    Script* s = impl_->find(std::string(id));
    if (!s) return "not loaded";
    std::lock_guard<std::recursive_mutex> l(s->m);
    if (!s->not_run.empty()) return "not run: " + s->not_run;
    if (s->paused) return "paused: " + s->error;
    return s->L ? "running" : "not loaded";
}

}  // namespace emf::lua
