-- bus_service: a tiny "notes" service on emf.bus (an EMF example mod in Lua). Other mods ask it with JSON requests:
--   notes.request  {v = 1, id = "...", op = "set", args = {key = "k", value = "text"}, from = "caller_mod_id"}
--   notes.reply    {v = 1, id = "...", to = "caller_mod_id", ok = true, result = ...}  or  ok = false, error = "..."
--   notes.hello    {service = "notes", v = 1, ops = {...}}  on start and whenever someone sends notes.ping
-- Save this file while the game runs: EMF reloads the mod (the notes are in memory, so a reload empties them).
-- See docs/modding/services.md for the pattern.

local SERVICE, VERSION = "notes", 1
local MAX_NOTES, MAX_VALUE = 200, 1000
local notes = {}
local count = 0

local ops = {}

function ops.set(args)
  if type(args.key) ~= "string" or args.key == "" then return nil, "args.key must be a non-empty text" end
  if type(args.value) ~= "string" or #args.value > MAX_VALUE then return nil, "args.value must be a text of up to 1000 bytes" end
  if notes[args.key] == nil then
    if count >= MAX_NOTES then return nil, "full: at most 200 notes" end
    count = count + 1
  end
  notes[args.key] = args.value
  return true
end

function ops.get(args)
  if type(args.key) ~= "string" then return nil, "args.key must be a text" end
  local v = notes[args.key]
  if v == nil then return emf.json.null end  -- no such note: a normal answer, not an error
  return v
end

function ops.delete(args)
  if type(args.key) ~= "string" then return nil, "args.key must be a text" end
  if notes[args.key] ~= nil then count = count - 1 end
  notes[args.key] = nil
  return true
end

function ops.list()
  local keys = {}
  for k in pairs(notes) do keys[#keys + 1] = k end
  table.sort(keys)
  return keys
end

local function names()
  local out = {}
  for name in pairs(ops) do out[#out + 1] = name end
  table.sort(out)
  return out
end

local function hello()
  emf.bus.publish(SERVICE .. ".hello", emf.json.encode({service = SERVICE, v = VERSION, ops = names()}))
end

local function answer(to, id, ok, value)
  local reply = {v = VERSION, id = id, to = to, ok = ok}
  if ok then reply.result = value else reply.error = value end
  local text, err = emf.json.encode(reply)
  if not text then text = emf.json.encode({v = VERSION, id = id, to = to, ok = false, error = "reply not encodable: " .. err}) end
  if not emf.bus.publish(SERVICE .. ".reply", text) then
    emf.log("could not answer", to, "(bus queue full): the caller will time out")
  end
end

emf.bus.subscribe(SERVICE .. ".request", function(world, source, payload)
  local req = emf.json.decode(payload)
  if type(req) ~= "table" or type(req.id) ~= "string" then return end  -- no id, nobody to answer
  local to = type(req.from) == "string" and req.from or source
  if req.v ~= VERSION then return answer(to, req.id, false, "unsupported version " .. tostring(req.v)) end
  local op = ops[req.op]
  if not op then return answer(to, req.id, false, "unknown op " .. tostring(req.op)) end
  local ok, result, err = pcall(op, type(req.args) == "table" and req.args or {})
  if not ok then return answer(to, req.id, false, "the service failed: " .. tostring(result)) end
  if result == nil then return answer(to, req.id, false, err or "failed") end
  answer(to, req.id, true, result)
end)

emf.bus.subscribe(SERVICE .. ".ping", function() hello() end)
hello()  -- announce ourselves: mods that load later still get this (before the first world step)
