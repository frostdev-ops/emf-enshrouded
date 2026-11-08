// emf.json: a dedicated encoder and decoder, not src/core/json.cpp: that reader turns every number into a double (an
// int64 above 2^53 would lose digits) and has no writer. The rules are in docs/modding/lua.md ("JSON (1.18)").
#include "lua_host/lua_json.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "lauxlib.h"
#include "lua.h"

namespace emf::lua {
namespace {

constexpr int kMaxDepth = 64;
constexpr size_t kMaxText = 1u << 20;  // input text and output text, bytes

char g_null_tag;  // emf.json.null is a light userdata at this address
void* null_sentinel() { return &g_null_tag; }

// ---------------- UTF-8 ----------------
// Length of the valid UTF-8 sequence at s[i] (1..4), or 0. Rejects overlongs, surrogates and above U+10FFFF.
int utf8_len(std::string_view s, size_t i) {
    const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned c = b(i);
    if (c < 0x80) return 1;
    auto cont = [&](size_t k) { return k < s.size() && (b(k) & 0xC0) == 0x80; };
    if (c >= 0xC2 && c <= 0xDF) return cont(i + 1) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        if (!cont(i + 1) || !cont(i + 2)) return 0;
        const unsigned c1 = b(i + 1);
        if (c == 0xE0 && c1 < 0xA0) return 0;
        if (c == 0xED && c1 >= 0xA0) return 0;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (!cont(i + 1) || !cont(i + 2) || !cont(i + 3)) return 0;
        const unsigned c1 = b(i + 1);
        if (c == 0xF0 && c1 < 0x90) return 0;
        if (c == 0xF4 && c1 >= 0x90) return 0;
        return 4;
    }
    return 0;
}

void put_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// ---------------- encoder ----------------
struct Encoder {
    lua_State* L;
    std::string out;
    std::string error;

    bool fail(std::string msg) {
        if (error.empty()) error = std::move(msg);
        return false;
    }
    bool room(size_t more) {
        if (out.size() + more > kMaxText) return fail("the JSON text would be longer than 1 MiB");
        return true;
    }

    bool string(const char* p, size_t n) {
        if (!room(n + 2)) return false;
        out += '"';
        for (size_t i = 0; i < n;) {
            const unsigned char c = static_cast<unsigned char>(p[i]);
            if (c < 0x80) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    default:
                        if (c < 0x20 || c == 0x7f) {
                            char buf[8];
                            std::snprintf(buf, sizeof buf, "\\u%04x", c);
                            out += buf;
                        } else out += static_cast<char>(c);
                }
                ++i;
            } else {
                const int len = utf8_len(std::string_view(p, n), i);
                if (len == 0) return fail("a string is not valid UTF-8");
                out.append(p + i, static_cast<size_t>(len));
                i += static_cast<size_t>(len);
            }
            if (out.size() > kMaxText) return fail("the JSON text would be longer than 1 MiB");
        }
        out += '"';
        return room(0);
    }

    bool number(double d) {
        if (d != d || d - d != 0) return fail("NaN and infinity cannot be written as JSON");
        char buf[64];
        const auto r = std::to_chars(buf, buf + sizeof buf, d);  // the shortest text that reads back as the same double
        std::string t(buf, r.ptr);
        if (t.find_first_of(".eE") == std::string::npos) t += ".0";  // a float stays a float after a round trip
        if (!room(t.size())) return false;
        out += t;
        return true;
    }

    // The value at the stack top (left in place).
    bool value(int depth) {
        switch (lua_type(L, -1)) {
            case LUA_TBOOLEAN:
                if (!room(5)) return false;
                out += lua_toboolean(L, -1) ? "true" : "false";
                return true;
            case LUA_TNUMBER:
                if (lua_isinteger(L, -1)) {
                    if (!room(24)) return false;
                    out += std::to_string(static_cast<long long>(lua_tointeger(L, -1)));
                    return true;
                }
                return number(static_cast<double>(lua_tonumber(L, -1)));
            case LUA_TSTRING: {
                size_t n = 0;
                const char* p = lua_tolstring(L, -1, &n);
                return string(p, n);
            }
            case LUA_TLIGHTUSERDATA:
                if (lua_touserdata(L, -1) == null_sentinel()) {
                    if (!room(4)) return false;
                    out += "null";
                    return true;
                }
                return fail("a userdata cannot be written as JSON (only emf.json.null)");
            case LUA_TTABLE: return table(depth + 1);
            case LUA_TNIL: return fail("nil cannot be written as JSON (use emf.json.null for null)");
            default: return fail(std::string("a ") + luaL_typename(L, -1) + " cannot be written as JSON");
        }
    }

    bool table(int depth) {
        if (depth > kMaxDepth) return fail("tables nest deeper than 64 levels (or refer to themselves)");
        if (!lua_checkstack(L, 6)) return fail("the table is too deep");
        const int t = lua_gettop(L);
        // Classify: an array has the keys 1..n and nothing else; an object has only string keys.
        size_t count = 0;
        lua_Integer max_key = 0;
        bool all_int = true, all_str = true;
        std::string bad;
        lua_pushnil(L);
        while (lua_next(L, t) != 0) {
            lua_pop(L, 1);
            ++count;
            const int kt = lua_type(L, -1);
            if (kt == LUA_TNUMBER && lua_isinteger(L, -1)) {
                all_str = false;
                const lua_Integer k = lua_tointeger(L, -1);
                if (k < 1) all_int = false;
                else max_key = std::max(max_key, k);
            } else if (kt == LUA_TSTRING) {
                all_int = false;
            } else {
                all_int = all_str = false;
                if (bad.empty()) bad = std::string("a ") + luaL_typename(L, -1) + " key";
            }
        }
        if (count == 0) {
            if (!room(2)) return false;
            out += "{}";
            return true;
        }
        if (all_int && static_cast<lua_Integer>(count) == max_key) {  // keys are distinct, so 1..count are all present
            out += '[';
            for (lua_Integer i = 1; i <= max_key; ++i) {
                if (i > 1) out += ',';
                lua_rawgeti(L, t, i);
                const bool ok = value(depth);
                lua_pop(L, 1);
                if (!ok) return false;
            }
            out += ']';
            return room(0);
        }
        if (!all_str) {
            if (bad.empty()) bad = "an integer key that is not part of 1..n";
            return fail("a table with " + bad +
                        " cannot be written as JSON (an array has the keys 1..n, an object only string keys)");
        }
        std::vector<std::string> keys;
        keys.reserve(count);
        lua_pushnil(L);
        while (lua_next(L, t) != 0) {
            lua_pop(L, 1);
            size_t n = 0;
            const char* p = lua_tolstring(L, -1, &n);
            keys.emplace_back(p, n);
        }
        std::sort(keys.begin(), keys.end());
        out += '{';
        bool first = true;
        for (const auto& k : keys) {
            if (!first) out += ',';
            first = false;
            if (!string(k.data(), k.size())) return false;
            out += ':';
            lua_pushlstring(L, k.data(), k.size());
            lua_rawget(L, t);
            const bool ok = value(depth);
            lua_pop(L, 1);
            if (!ok) return false;
        }
        out += '}';
        return room(0);
    }
};

int l_encode(lua_State* L) {
    luaL_checkany(L, 1);
    lua_settop(L, 1);
    Encoder e{L, {}, {}};
    if (!e.value(0)) {
        lua_pushnil(L);
        lua_pushlstring(L, e.error.data(), e.error.size());
        return 2;
    }
    lua_pushlstring(L, e.out.data(), e.out.size());
    return 1;
}

// ---------------- decoder (builds the Lua values directly on the stack) ----------------
struct Decoder {
    lua_State* L;
    std::string_view s;
    size_t i = 0;
    std::string error;

    bool fail(const std::string& msg) {
        if (error.empty()) {
            size_t line = 1, col = 1;
            for (size_t k = 0; k < i && k < s.size(); ++k) {
                if (s[k] == '\n') {
                    ++line;
                    col = 1;
                } else ++col;
            }
            error = msg + " (line " + std::to_string(line) + ", column " + std::to_string(col) + ")";
        }
        return false;
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool lit(std::string_view word) {
        if (s.substr(i, word.size()) != word) return fail("unexpected text");
        i += word.size();
        return true;
    }

    bool hex4(uint32_t& v) {
        if (i + 4 > s.size()) return fail("a \\u escape needs four hex digits");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i + static_cast<size_t>(k)];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("a \\u escape needs four hex digits");
        }
        i += 4;
        return true;
    }

    bool string(std::string& out) {  // at the opening quote
        ++i;
        while (true) {
            if (i >= s.size()) return fail("the string is not closed");
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '"') {
                ++i;
                return true;
            }
            if (c < 0x20) return fail("a control character inside a string must be escaped");
            if (c == '\\') {
                ++i;
                if (i >= s.size()) return fail("the string is not closed");
                const char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!hex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            uint32_t lo = 0;
                            if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u')
                                return fail("a high surrogate needs a \\u low surrogate after it");
                            i += 2;
                            if (!hex4(lo)) return false;
                            if (lo < 0xDC00 || lo > 0xDFFF) return fail("a high surrogate needs a \\u low surrogate after it");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            return fail("a low surrogate without its high surrogate");
                        }
                        put_utf8(out, cp);
                        break;
                    }
                    default:
                        --i;
                        return fail("unknown escape");
                }
                continue;
            }
            if (c < 0x80) {
                out += static_cast<char>(c);
                ++i;
                continue;
            }
            const int len = utf8_len(s, i);
            if (len == 0) return fail("the text is not valid UTF-8");
            out.append(s.data() + i, static_cast<size_t>(len));
            i += static_cast<size_t>(len);
        }
    }

    bool number() {
        const size_t start = i;
        bool is_float = false;
        if (s[i] == '-') ++i;
        if (i >= s.size()) return fail("a number needs digits");
        if (s[i] == '0') ++i;
        else if (s[i] >= '1' && s[i] <= '9') {
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        } else return fail("a number needs digits");
        if (i < s.size() && s[i] == '.') {
            is_float = true;
            ++i;
            const size_t d = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            if (i == d) return fail("a number needs digits after the point");
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            is_float = true;
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            const size_t d = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
            if (i == d) return fail("a number needs digits in its exponent");
        }
        const char* b = s.data() + start;
        const char* e = s.data() + i;
        if (!is_float) {
            long long v = 0;
            const auto r = std::from_chars(b, e, v);
            if (r.ec == std::errc() && r.ptr == e) {
                lua_pushinteger(L, static_cast<lua_Integer>(v));
                return true;
            }
        }
        double d = 0;
        const auto r = std::from_chars(b, e, d);
        if (r.ec != std::errc() || r.ptr != e || d - d != 0) {
            i = start;
            return fail("the number is out of range");
        }
        lua_pushnumber(L, static_cast<lua_Number>(d));
        return true;
    }

    // Pushes exactly one value.
    bool value(int depth) {
        ws();
        if (i >= s.size()) return fail("the text ends where a value is expected");
        const char c = s[i];
        if (c == '{' || c == '[') {
            if (depth + 1 > kMaxDepth) return fail("nested deeper than 64 levels");
            if (!lua_checkstack(L, 8)) return fail("nested too deep");
            return c == '{' ? object(depth + 1) : array(depth + 1);
        }
        if (c == '"') {
            std::string str;
            if (!string(str)) return false;
            lua_pushlstring(L, str.data(), str.size());
            return true;
        }
        if (c == 't') {
            if (!lit("true")) return false;
            lua_pushboolean(L, 1);
            return true;
        }
        if (c == 'f') {
            if (!lit("false")) return false;
            lua_pushboolean(L, 0);
            return true;
        }
        if (c == 'n') {
            if (!lit("null")) return false;
            lua_pushlightuserdata(L, null_sentinel());
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        return fail("unexpected character");
    }

    bool array(int depth) {
        ++i;
        lua_newtable(L);
        ws();
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        for (lua_Integer n = 1;; ++n) {
            if (!value(depth)) return false;
            lua_rawseti(L, -2, n);
            ws();
            if (i >= s.size()) return fail("the array is not closed");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == ']') {
                ++i;
                return true;
            }
            return fail("expected , or ] in an array");
        }
    }

    bool object(int depth) {
        ++i;
        lua_newtable(L);
        ws();
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        while (true) {
            ws();
            if (i >= s.size() || s[i] != '"') return fail("expected a string as the key of an object member");
            std::string key;
            if (!string(key)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') return fail("expected : after an object key");
            ++i;
            lua_pushlstring(L, key.data(), key.size());
            if (!value(depth)) return false;
            lua_rawset(L, -3);  // a repeated key keeps its last value
            ws();
            if (i >= s.size()) return fail("the object is not closed");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == '}') {
                ++i;
                return true;
            }
            return fail("expected , or } in an object");
        }
    }
};

int l_decode(lua_State* L) {
    size_t n = 0;
    const char* text = luaL_checklstring(L, 1, &n);
    lua_settop(L, 1);
    auto bad = [&](const std::string& msg) {
        lua_settop(L, 0);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data(), msg.size());
        return 2;
    };
    if (n > kMaxText) return bad("the JSON text is longer than 1 MiB");
    Decoder d{L, std::string_view(text, n)};
    if (!d.value(0)) return bad(d.error);
    d.ws();
    if (d.i != n) {
        d.fail("unexpected text after the value");
        return bad(d.error);
    }
    return 1;  // the value is on top
}

}  // namespace

void push_json_table(lua_State* L) {
    static const luaL_Reg fns[] = {{"encode", l_encode}, {"decode", l_decode}, {nullptr, nullptr}};
    luaL_newlib(L, fns);
    lua_pushlightuserdata(L, null_sentinel());
    lua_setfield(L, -2, "null");
}

}  // namespace emf::lua
