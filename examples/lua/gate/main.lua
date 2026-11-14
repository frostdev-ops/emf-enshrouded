-- gate: open, close and lock the doors around you (an EMF example mod in Lua).
--   /gate.open [front|back]   opens the doors within 10 m of you
--   /gate.close               closes them
--   /gate.lock [off]          locks them: nobody gets the E prompt on them ("off" unlocks)
-- Doors came in Lua 1.30 (EMF API v38). They change on the server world only, so these are commands for the host: the
-- console, emf\commands.txt, the host's own chat, or an admin's chat on a dedicated server.

local RADIUS = 10  -- metres, measured on the ground

-- Whose doors? The player who typed the command in the chat, else the host. Their entity, or nil and what to answer.
local function asker()
  local who = emf.sender()  -- {chat, entity, account, admin}
  if who and who.chat then
    if who.entity == 0 then return nil, "EMF could not tell which player you are" end
    return who.entity
  end
  local me = emf.local_player()  -- 0 on a dedicated server, which has no local player
  if me == 0 then return nil, "The console has no player here: ask from the game's chat" end
  return me
end

-- The doors within RADIUS of `id`: {id, state, locked, logic}. There is no "doors near me" call: walk the entities near
-- enough and ask emf.door, which gives nil for anything that is not a door (player doors and gates are doors).
local function doors_near(world, id)
  local x, _, z = world:position(id)
  if not x then return nil end
  local list = {}
  for _, e in ipairs(world:entities()) do
    local ex, _, ez = world:position(e)
    if ex and (ex - x) ^ 2 + (ez - z) ^ 2 <= RADIUS ^ 2 then
      local state, flags = emf.door(world, e)  -- "open" or "closed", {locked, logic}; nil: not a door
      if state then list[#list + 1] = {id = e, state = state, locked = flags.locked, logic = flags.logic} end
    end
  end
  return list
end

-- Runs change(world, door) on each door around the asker and answers with one line. change returns true (it changes
-- at the next server step), "already" (nothing to do) or false (refused: the core logged why).
local function each_door(world, done_word, change)
  local id, why = asker()
  if not id then emf.reply(why) return end
  local list = doors_near(world, id)
  if not list then emf.reply("Your position is not known yet") return end
  if #list == 0 then emf.reply("No door within " .. RADIUS .. " m of you") return end
  local done, already, switch, refused = 0, 0, 0, 0
  for _, door in ipairs(list) do
    if door.logic then
      switch = switch + 1  -- a switch drives it: EMF leaves it alone (and would refuse)
    else
      local result = change(world, door)
      if result == "already" then already = already + 1
      elseif result then done = done + 1
      else refused = refused + 1 end
    end
  end
  local text = done_word .. " " .. done .. " of " .. #list .. " door(s) within " .. RADIUS .. " m"
  if already > 0 then text = text .. ", " .. already .. " already so" end
  if switch > 0 then text = text .. ", " .. switch .. " driven by a switch" end
  if refused > 0 then text = text .. ", " .. refused .. " refused (see the log)" end
  emf.reply(text)
end

emf.command("open", "[front|back]: open the doors within 10 m of you (host)", function(world, args)
  local side = args[1]  -- which way it swings; nil lets the game choose
  if side ~= nil and side ~= "front" and side ~= "back" then emf.reply("usage: gate.open [front|back]") return end
  each_door(world, "Opened", function(w, door)
    if door.state == "open" then return "already" end
    return emf.door_open(w, door.id, side)
  end)
end)

emf.command("close", ": close the doors within 10 m of you (host)", function(world)
  each_door(world, "Closed", function(w, door)
    if door.state == "closed" then return "already" end
    return emf.door_close(w, door.id)
  end)
end)

emf.command("lock", "[off]: lock the doors within 10 m of you, so nobody can open them with E; off unlocks (host)",
  function(world, args)
    if args[1] ~= nil and args[1] ~= "off" then emf.reply("usage: gate.lock [off]") return end
    local lock = args[1] ~= "off"
    each_door(world, lock and "Locked" or "Unlocked", function(w, door)
      if door.locked == lock then return "already" end
      return emf.door_lock(w, door.id, lock)  -- kept with the world (EMF 0.6.8); open and close still work
    end)
  end)
