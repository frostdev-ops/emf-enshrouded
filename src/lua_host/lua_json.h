#pragma once
// emf.json (Lua host 1.18): JSON text <-> Lua values for mods that send tables over emf.bus.
struct lua_State;

namespace emf::lua {

// Pushes the emf.json table (encode, decode, null) onto the stack.
void push_json_table(lua_State* L);

}  // namespace emf::lua
