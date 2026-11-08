# Services on the message bus

A **service** is a Lua mod that other Lua mods call over `emf.bus` (a world database, navigation, combat tracking).
Callers send a *request* and wait for a *reply*. If every service uses the same envelope, a client written for one
author's service works the same way with another's. This page is that agreement. It needs Lua host 1.18
(`"depends": {"lua": ">=1.18"}`): `emf.bus` is in [lua.md](lua.md) ("Messages between Lua mods") and `emf.json` in
"JSON (1.18)". A working pair is in `examples/lua/bus_service` and `examples/lua/bus_client`.

## Topics

Name topics `<service>.<what>`, with your service's name as the prefix (`notes.request`). Pick a name that is unlikely
to clash (`mymod_notes` rather than `notes`) and keep it fixed: it is your API.

| Topic | Who publishes | Payload |
|---|---|---|
| `<service>.request` | clients | a request (below) |
| `<service>.reply` | the service | a reply (below) |
| `<service>.hello` | the service | `{service, v, ops}`: "I am here", at start and in answer to a ping |
| `<service>.ping` | clients | anything (`""`): "announce yourself" |

Everybody subscribed to a topic gets every message, so all clients see all replies: each client picks out its own.

## The envelope

Payloads are JSON text made with `emf.json.encode`. The service decodes, answers, and never trusts the shape.

```lua
-- request
{v = 1, id = "mymod:12", op = "get", args = {key = "apple"}, from = "mymod"}
-- reply, success
{v = 1, id = "mymod:12", to = "mymod", ok = true, result = "red"}
-- reply, failure
{v = 1, id = "mymod:12", to = "mymod", ok = false, error = "unknown op nope"}
```

- `v`: the service's API version, a whole number. A service answers a `v` it does not know with an error. Raise it only
  for changes that break old clients; adding an `op` or an optional argument does not.
- `id`: text, unique per caller (`<your mod id>:<counter>` works). The reply repeats it.
- `op` and `args`: what to do and its arguments (a table). `result` is any JSON value; use `emf.json.null` for "nothing".
- `from`: the caller's mod id, copied to `to` in the reply so the right client picks it up. It only routes the answer:
  anyone can write anything in it. The `source` argument of a bus callback is set by EMF and cannot be faked, so use
  `source` for every decision about who is asking (who may write, whose data to return). Replies share one topic and
  reach every subscriber, so never put a secret in a reply.
- Errors are short plain sentences a player could read. "Not found" is usually a normal result, not an error.

## Writing a service

```lua
local SERVICE, VERSION = "mymod_notes", 1

local function answer(to, id, ok, value)
  local reply = {v = VERSION, id = id, to = to, ok = ok}
  if ok then reply.result = value else reply.error = value end
  local text = emf.json.encode(reply) or emf.json.encode({v = VERSION, id = id, to = to, ok = false, error = "reply not encodable"})
  if #text > 65536 then                       -- the bus refuses more: send a short error, so the caller does not just time out
    emf.log("reply too big:", #text, "bytes")
    text = emf.json.encode({v = VERSION, id = id, to = to, ok = false, error = "reply too big"})
  end
  if not emf.bus.publish(SERVICE .. ".reply", text) then emf.log("reply refused: the bus queue is full") end
end

emf.bus.subscribe(SERVICE .. ".request", function(world, source, payload)
  local req = emf.json.decode(payload)
  if type(req) ~= "table" or type(req.id) ~= "string" then return end   -- nobody to answer
  local to = type(req.from) == "string" and req.from or source
  if req.v ~= VERSION then return answer(to, req.id, false, "unsupported version") end
  local op = ops[req.op]                                                 -- ops: your table of functions
  if not op then return answer(to, req.id, false, "unknown op") end
  local ok, result, err = pcall(op, type(req.args) == "table" and req.args or {})
  if not ok then return answer(to, req.id, false, "the service failed") end
  if result == nil then return answer(to, req.id, false, err or "failed") end
  answer(to, req.id, true, result)
end)

local function hello()
  emf.bus.publish(SERVICE .. ".hello", emf.json.encode({service = SERVICE, v = VERSION, ops = {"get", "set"}}))
end
-- Answer pings at most once a second: 256 pings in one step would fill your own queue.
local hello_due, steps, last = false, 0, 0
emf.bus.subscribe(SERVICE .. ".ping", function() hello_due = true end)
emf.on_tick(function(world)
  if not world:is_server() then return end
  steps = steps + 1
  if hello_due and steps - last >= 60 then hello_due, last = false, steps; hello() end
end)
hello()
```

Rules of thumb:

- **Always answer** a request that has an `id`, with an error if need be. A client that waits for silence has only its
  timeout to rely on.
- **Cap what you store and return.** Limit the length of keys and values you accept, so a reply can never pass 64 KiB.
- **Keep replies small.** One message is at most 64 KiB, and a mod may have 256 messages waiting. Return a page
  (`args.after`, `args.limit`) instead of everything.
- **Do not block a world step.** Work through large jobs over several steps and answer when done (the caller's timeout
  must be long enough, or answer early with `{pending = true}` and send the result as another topic).
- **Server or client world.** `emf.bus.subscribe(topic, fn)` and `publish` default to the `"server"` world, which a
  dedicated server and a hosted game both run. The bus does not cross the network: a client mod's `"client"` messages stay
  on that player's machine. A service that serves players' own machines must run there and publish with
  `"client"`; one that holds shared world state runs on the server.
- **Reloads.** When your mod reloads, its subscriptions and queued messages are gone and main.lua runs again. Subscribe
  at the top level of main.lua (as above) and announce yourself with `hello()`. Requests that arrived while you were
  reloading are lost: callers see a timeout and may ask again.

## Writing a client

```lua
local TIMEOUT_STEPS = 3 * 60   -- 3 seconds: server steps are 60 a second
local steps, counter, pending = 0, 0, {}

local function call(op, args, done)               -- done(ok, value_or_error) runs exactly once
  counter = counter + 1
  local id = emf.id .. ":" .. counter
  local text = emf.json.encode({v = 1, id = id, op = op, args = args, from = emf.id})
  if not text or not emf.bus.publish("mymod_notes.request", text) then
    return done(false, "request refused")          -- too big, or the bus is full: do not retry in a loop
  end
  pending[id] = {deadline = steps + TIMEOUT_STEPS, done = done}
end

emf.bus.subscribe("mymod_notes.reply", function(world, source, payload)
  local reply = emf.json.decode(payload)
  if type(reply) ~= "table" or reply.to ~= emf.id then return end
  local p = pending[reply.id]
  if not p then return end                         -- late: its timeout already ran
  pending[reply.id] = nil
  if reply.ok then p.done(true, reply.result) else p.done(false, reply.error) end
end)

emf.on_tick(function(world)
  if not world:is_server() then return end         -- count one world's steps only
  steps = steps + 1
  local expired = {}                               -- collect first: done() may call call() again, and adding
  for id, p in pairs(pending) do                   -- a key while pairs() walks the table breaks the walk
    if steps >= p.deadline then expired[#expired + 1] = id end
  end
  for _, id in ipairs(expired) do
    local p = pending[id]
    pending[id] = nil
    p.done(false, "timeout")
  end
end)
```

- **Match by `id`** and ignore replies with another `to`. Replies for several clients share one topic.
- **Always set a timeout.** The reply may never come: the service may not be installed, be reloading, or be busy. Count
  steps in `emf.on_tick` (60 a second); do not wait in a loop.
  Give every `call` one `done` that handles both outcomes, and make it safe to run exactly once.
- **Publishing can fail.** `emf.bus.publish` returns false when the topic or payload is invalid, or when your 256 (or
  the 4,096 overall) message slots are full. Fail the call at once, or retry on a later step; do not publish in a tight loop.
- **Retry only what is safe to repeat.** A timed-out `set` may or may not have happened. Reads are safe to repeat; for
  writes, let the service accept a client-chosen `id` or key so a repeat does nothing twice.
- **Find the service.** Subscribe to `<service>.hello`: it arrives when the service starts (also if you load before it)
  and again when it reloads. Publish `<service>.ping` to ask for one now. Check its `v` against what you speak, and
  show a plain message ("needs the notes mod") when it never arrives instead of failing silently.
- **A callback runs on the next world step.** A reply therefore arrives at least two steps after the request (one for
  the service to get it, one for you to get the answer). Do not expect an answer in the same call.
