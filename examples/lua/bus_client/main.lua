-- bus_client: console commands that call the "notes" service of bus_service over emf.bus (an EMF example mod in Lua).
--   bus_client.set <key> <text>    bus_client.get <key>    bus_client.list    bus_client.ping
-- The point: the answer may never come (the service is missing, reloading or busy), so every call has a timeout and
-- the caller always hears one result: an answer or a timeout.
-- See docs/modding/services.md for the pattern.

local SERVICE, VERSION = "notes", 1
local TIMEOUT_STEPS = 3 * 60  -- 3 seconds of server steps (60 a second)

local steps = 0     -- server steps so far: our clock
local next_id = 0
local pending = {}  -- request id -> {deadline = step, done = fn(ok, result_or_error)}

-- Ask the service. done(ok, value) runs exactly once: with its answer, with its error, or with a timeout.
local function call(op, args, done)
  next_id = next_id + 1
  local id = emf.id .. ":" .. next_id  -- unique across mods
  local text = emf.json.encode({v = VERSION, id = id, op = op, args = args, from = emf.id})
  if not text or not emf.bus.publish(SERVICE .. ".request", text) then
    return done(false, "the bus refused the request (queue full or too big): try again later")
  end
  pending[id] = {deadline = steps + TIMEOUT_STEPS, done = done}
end

emf.bus.subscribe(SERVICE .. ".reply", function(world, source, payload)
  local reply = emf.json.decode(payload)
  if type(reply) ~= "table" or reply.to ~= emf.id then return end  -- not an answer to us
  local p = pending[reply.id]
  if not p then return end  -- a late answer to a request that already timed out
  pending[reply.id] = nil
  if reply.ok then p.done(true, reply.result) else p.done(false, reply.error) end
end)

emf.bus.subscribe(SERVICE .. ".hello", function(world, source, payload)
  local hello = emf.json.decode(payload)
  if type(hello) ~= "table" then return end
  if hello.v ~= VERSION then emf.log("the", SERVICE, "service from", source, "speaks version", hello.v, "- expected", VERSION) end
  emf.log("service", tostring(hello.service), "is here (from " .. source .. ")")
end)

emf.on_tick(function(world)
  if not world:is_server() then return end
  steps = steps + 1
  for id, p in pairs(pending) do
    if steps >= p.deadline then
      pending[id] = nil
      p.done(false, "timeout: no answer from the " .. SERVICE .. " service (is bus_service installed?)")
    end
  end
end)

-- What the player sees: a note on screen and a line in the log.
local function say(text)
  emf.log(text)
  emf.notify(text)
end

local function show(what)
  return function(ok, value)
    if not ok then return say(what .. " failed: " .. tostring(value)) end
    if value == emf.json.null then return say(what .. ": (nothing)") end
    if type(value) == "table" then value = "[" .. table.concat(value, ", ") .. "]" end
    say(what .. ": " .. tostring(value))
  end
end

emf.command("set", "<key> <text>: save a note in the notes service", function(world, args)
  if not args[1] or not args[2] then return emf.reply("usage: set <key> <text>") end
  call("set", {key = args[1], value = table.concat(args, " ", 2)}, show("set " .. args[1]))
end)

emf.command("get", "<key>: read a note from the notes service", function(world, args)
  if not args[1] then return emf.reply("usage: get <key>") end
  call("get", {key = args[1]}, show("get " .. args[1]))
end)

emf.command("list", ": the keys of all notes", function(world, args)
  call("list", {}, show("notes"))
end)

emf.command("ping", ": ask the notes service to announce itself (see the log)", function(world, args)
  emf.bus.publish(SERVICE .. ".ping", "")
end)
