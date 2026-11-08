// emf.json (Lua host 1.18): a script checks itself and logs FAIL lines; the test reads the log.
#include <windows.h>

#include <doctest.h>

#include <string>

#include "lua_host/host.h"

using emf::lua::Host;

#include "lua_test_fake.h"

namespace {
const char* kJsonPrelude = R"LUA(
local J = emf.json
local function eq(a, b, name) if a ~= b then emf.log('FAIL', name, tostring(a), tostring(b)) end end
local function same(a, b)
  if type(a) ~= type(b) then return false end
  if type(a) ~= 'table' then return a == b and math.type(a) == math.type(b) end
  for k, v in pairs(a) do if not same(v, b[k]) then return false end end
  for k in pairs(b) do if a[k] == nil then return false end end
  return true
end
local function bad(v, part, name)
  local s, err = J.encode(v)
  if s ~= nil or type(err) ~= 'string' or not err:find(part, 1, true) then emf.log('FAIL', name, tostring(s), tostring(err)) end
end
local function badd(text, part, name)
  local v, err = J.decode(text)
  if v ~= nil or type(err) ~= 'string' or not err:find(part, 1, true) then emf.log('FAIL', name, tostring(v), tostring(err)) end
end
)LUA";

void json_run(const wchar_t* name, const std::string& body) {
    Rig r(name);
    add_script(r.dir, "js", std::string(kJsonPrelude) + body + "\nemf.log('JSON_DONE')\n");
    Host host(&r.api, r.dir);
    host.load_all();
    CHECK(host.status("js") == "running");
    CHECK(logged("JSON_DONE"));
    CHECK_FALSE(logged("FAIL"));
}
}  // namespace

TEST_CASE("lua host: emf.json encodes scalars, sorted objects, arrays and the empty table") {
    json_run(L"json_encode_118", R"LUA(
eq(J.encode(false), 'false', 'false')
eq(J.encode(true), 'true', 'true')
eq(J.encode(12), '12', 'int')
eq(J.encode(-7), '-7', 'negint')
eq(J.encode(0.5), '0.5', 'float')
eq(J.encode(1.0), '1.0', 'whole float stays float')
eq(J.encode(1e300), '1e+300', 'big float')
eq(J.encode(math.maxinteger), '9223372036854775807', 'maxint')
eq(J.encode(0.1), '0.1', 'shortest')
eq(J.encode('hi'), '"hi"', 'string')
eq(J.encode({}), '{}', 'empty table')
eq(J.encode({1, 2, 3}), '[1,2,3]', 'array')
eq(J.encode({b = 1, a = 2, c = {x = true}}), '{"a":2,"b":1,"c":{"x":true}}', 'sorted keys')
eq(J.encode({{}, {1}}), '[{},[1]]', 'nested')
eq(J.encode(J.null), 'null', 'null')
eq(J.encode({J.null, 1}), '[null,1]', 'null in array')
eq(J.encode({a = J.null}), '{"a":null}', 'null in object')
eq(J.encode('a"b\\c\n\t\1'), '"a\\"b\\\\c\\n\\t\\u0001"', 'escapes')
eq(J.encode('h\xC3\xA9 \xF0\x9F\x98\x80'), '"h\xC3\xA9 \xF0\x9F\x98\x80"', 'utf8 passes through')
eq(J.encode('a\0b'), '"a\\u0000b"', 'nul')
)LUA");
}

TEST_CASE("lua host: emf.json decodes values, integers versus floats, escapes and surrogate pairs") {
    json_run(L"json_decode_118", R"LUA(
eq(J.decode('12'), 12, 'int'); eq(math.type(J.decode('12')), 'integer', 'int type')
eq(math.type(J.decode('-0')), 'integer', '-0 integer')
eq(math.type(J.decode('1.0')), 'float', '1.0 float')
eq(math.type(J.decode('1e2')), 'float', '1e2 float'); eq(J.decode('1e2'), 100.0, '1e2')
eq(J.decode('9223372036854775807'), math.maxinteger, 'maxint')
eq(math.type(J.decode('9223372036854775808')), 'float', 'beyond int64 is float')
eq(J.decode('-9223372036854775808'), math.mininteger, 'minint')
eq(#J.decode(' \n[1, 2 ,3]\t'), 3, 'whitespace')
eq(J.decode('{"a":{"b":[true,false]}}').a.b[2], false, 'nested')
eq(J.decode('null'), J.null, 'null')
eq(J.decode('[null]')[1], J.null, 'null in array')
eq(J.decode('"\\u00e9"'), '\xC3\xA9', 'u escape')
eq(J.decode('"\\ud83d\\ude00"'), '\xF0\x9F\x98\x80', 'surrogate pair')
eq(J.decode('"a\\/b\\n\\"\\\\"'), 'a/b\n"\\', 'simple escapes')
eq(J.decode('"\\u0000"'), '\0', 'nul escape')
eq(J.decode('"\xE2\x82\xAC"'), '\xE2\x82\xAC', 'raw utf8')
eq(next(J.decode('{}')), nil, 'empty object')
eq(J.decode('{"a":1,"a":2}').a, 2, 'repeated key keeps last')
)LUA");
}

TEST_CASE("lua host: emf.json round trips tables, unicode, numbers and null") {
    json_run(L"json_roundtrip_118", R"LUA(
local v = {name = 'caf\xC3\xA9 \xF0\x9F\x98\x80', list = {1, 2.5, 'x', true, {deep = {1, {2}}}}, n = 3, f = 3.0,
            big = math.maxinteger, neg = -1e-9, nothing = J.null, empty = {}, ['quote"key'] = 'v'}
local s = J.encode(v)
local back = J.decode(s)
if not same(v, back) then emf.log('FAIL', 'round trip', s) end
eq(J.encode(back), s, 'stable text')
for _, x in ipairs({0.1, 1/3, 123456789.125, 5e-324, 1.7976931348623157e308, -2.5e-7, 2^53, 2^63}) do
  local t = J.encode(x)
  if J.decode(t) ~= x or math.type(J.decode(t)) ~= 'float' then emf.log('FAIL', 'float', t) end
end
)LUA");
}

TEST_CASE("lua host: emf.json reports bad input as nil plus a message and never raises") {
    json_run(L"json_errors_118", R"LUA(
bad(0/0, 'NaN', 'nan'); bad(math.huge, 'infinity', 'inf'); bad(-math.huge, 'infinity', 'neg inf')
bad({0/0}, 'NaN', 'nan in table')
bad(function() end, 'function', 'function'); bad(coroutine.create(print), 'thread', 'thread')
bad(io.stdout or {}, 'userdata', 'userdata')
bad(nil, 'nil', 'nil')
bad({[1] = 'a', [3] = 'c'}, 'key', 'sparse array')
bad({1, 2, x = 3}, 'key', 'mixed table')
bad({[1.5] = 1}, 'number key', 'float key')
bad({[true] = 1}, 'boolean key', 'bool key')
bad({[{}] = 1}, 'table key', 'table key')
bad({[0] = 1}, 'key', 'zero key')
bad({[-1] = 1}, 'key', 'negative key')
bad('\xFF', 'UTF-8', 'invalid utf8'); bad('\xC0\x80', 'UTF-8', 'overlong'); bad('\xED\xA0\x80', 'UTF-8', 'surrogate bytes')
bad({['\xFF'] = 1}, 'UTF-8', 'invalid key')
bad({f = print}, 'function', 'function in table')
badd('', 'ends', 'empty'); badd('[1,', 'ends', 'truncated'); badd('{"a"}', ':', 'no colon')
badd('[1 2]', ',', 'no comma'); badd('{a:1}', 'string', 'bare key'); badd('01', 'after', 'leading zero')
badd('1.', 'point', 'dot'); badd('1e', 'exponent', 'exp'); badd('-', 'digits', 'minus'); badd('+1', 'unexpected', 'plus')
badd('tru', 'unexpected', 'tru'); badd('"abc', 'closed', 'open string'); badd('"\\x"', 'escape', 'bad escape')
badd('"\\u12"', 'hex', 'short u'); badd('"\\ud800"', 'surrogate', 'lone high'); badd('"\\udc00"', 'surrogate', 'lone low')
badd('"\\ud800\\u0041"', 'surrogate', 'high plus plain'); badd('"a\nb"', 'control', 'raw newline')
badd('"\xFF"', 'UTF-8', 'raw invalid utf8'); badd('1 2', 'after', 'trailing'); badd('[1,]', 'unexpected', 'trailing comma')
badd('1e999', 'range', 'overflow'); badd('{"a":1}}', 'after', 'extra brace')
local v, err = J.decode(5); if v == nil then emf.log('FAIL', 'number text decodes as text 5') end
local ok = pcall(J.decode); if ok then emf.log('FAIL', 'no argument is a usage error') end
)LUA");
}

TEST_CASE("lua host: emf.json limits: 64 levels of nesting, 1 MiB of text") {
    json_run(L"json_limits_118", R"LUA(
local function nest(n) local t = {}; local cur = t; for _ = 2, n do local c = {}; cur[1] = c; cur = c end; return t end
if not J.encode(nest(64)) then emf.log('FAIL', 'encode 64 levels') end
bad(nest(65), '64 levels', 'encode 65 levels')
local cyc = {}; cyc[1] = cyc; bad(cyc, '64 levels', 'cycle')
local cycobj = {}; cycobj.me = cycobj; bad(cycobj, '64 levels', 'object cycle')
if not J.decode(string.rep('[', 64) .. string.rep(']', 64)) then emf.log('FAIL', 'decode 64 levels') end
badd(string.rep('[', 65) .. string.rep(']', 65), '64 levels', 'decode 65 levels')
badd(string.rep('[', 100000), '64 levels', 'decode deep')
badd(string.rep(' ', 1048577) .. '1', '1 MiB', 'input over 1 MiB')
if J.decode(string.rep(' ', 1048575) .. '1') ~= 1 then emf.log('FAIL', 'input at the limit') end
bad(string.rep('a', 1048576), '1 MiB', 'output over 1 MiB')
local s = J.encode(string.rep('a', 1048570)); if not s then emf.log('FAIL', 'output just under') end
local parts = {}; for i = 1, 100 do parts[i] = string.rep('x', 20000) end
bad(parts, '1 MiB', 'array output over 1 MiB')
)LUA");
}

TEST_CASE("lua host: a mod sends a JSON table over emf.bus and gets it back equal") {
    Rig r(L"json_bus_118");
    add_script(r.dir, "echo",
               "emf.bus.subscribe('echo.request', function(w, from, payload)\n"
               "  local req, err = emf.json.decode(payload)\n"
               "  if not req then emf.log('ECHO_BAD', err) return end\n"
               "  emf.bus.publish('echo.reply', emf.json.encode({id = req.id, ok = true, result = req.args}))\n"
               "end)\n");
    add_script(r.dir, "caller",
               "local sent = {id = 7, op = 'echo', args = {names = {'a', 'caf\\xC3\\xA9'}, n = 3, f = 2.5, none = emf.json.null}}\n"
               "emf.bus.subscribe('echo.reply', function(w, from, payload)\n"
               "  local rep = emf.json.decode(payload)\n"
               "  local a = rep.result\n"
               "  emf.log('CALLER_GOT', rep.id, rep.ok, from, a.names[2], a.n, math.type(a.n), a.f, a.none == emf.json.null)\n"
               "end)\n"
               "emf.bus.publish('echo.request', emf.json.encode(sent))\n");
    Host host(&r.api, r.dir);
    host.load_all();
    host.tick(kServer);
    host.tick(kServer);
    CHECK_FALSE(logged("ECHO_BAD"));
    CHECK(logged("CALLER_GOT 7 true echo caf\xC3\xA9 3 integer 2.5 true"));
}
