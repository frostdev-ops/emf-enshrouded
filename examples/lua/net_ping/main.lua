-- net_ping: the mod messages live check. The same mod runs on the player's game and on the server.
--   net_ping.send <text>   this game -> the server (the server answers "pong <text>")
--   net_ping.big <n>       n bytes of text (fragments)
--   net_ping.bin           every byte 0..255 (base64url)
--   net_ping.peer          the server's channel version as this game sees it
--   net_ping.all <text>    the server -> every player with the channel
local function short(s) return (s:gsub("[^%g ]", "?")):sub(1, 60) end

local ok = emf.net.channel("ping", {
  version = 1, to = "both", max = 16384, per_10s = 100,
  on_server = function(world, from, data)
    emf.log(string.format("NETPING server got %d bytes from player %s account %s admin %s local %s v%s: %s",
      #data, tostring(from.player), tostring(from.account), tostring(from.admin), tostring(from.is_local),
      tostring(from.version), short(data)))
    local sent = emf.net.send_to(world, from.player, "ping", "pong " .. data)
    emf.log("NETPING server reply sent: " .. tostring(sent))
  end,
  on_player = function(world, from, data)
    local all = 0
    for i = 1, #data do all = all + data:byte(i) end
    emf.log(string.format("NETPING player got %d bytes (sum %d): %s", #data, all, short(data)))
  end,
})
emf.log("NETPING channel registered: " .. tostring(ok))

-- On a player's game: once the server has greeted it (the server's channel version shows), three test messages two
-- seconds apart: text, 3000 bytes (several chat lines) and every byte 0..255. Console commands do not run in someone
-- else's game, so this is how a joined game starts by itself.
local step, ticks = 0, 0
emf.on_tick(function(world)
  if not world:is_client() or step >= 4 then return end
  ticks = ticks + 1
  if step == 0 then
    local v = emf.net.peer(nil, 0, "ping")
    if v == 0 then return end
    emf.log("NETPING the server greeted this game (its channel version " .. tostring(v) .. ")")
    step, ticks = 1, 0
  end
  if ticks < 120 then return end  -- about two seconds
  ticks = 0
  if step == 1 then
    emf.log("NETPING auto send text: " .. tostring(emf.net.send("ping", "hello-from-client")))
  elseif step == 2 then
    emf.log("NETPING auto send 3000 bytes: " .. tostring(emf.net.send("ping", string.rep("abcdefghij", 300))))
  else
    local t = {}
    for i = 0, 255 do t[#t + 1] = string.char(i) end
    emf.log("NETPING auto send 256 bytes 0..255: " .. tostring(emf.net.send("ping", table.concat(t))))
  end
  step = step + 1
end)

local function text(args) return table.concat(args, " ") end
emf.command("send", " <text>: this game -> the server", function(world, args)
  emf.reply("send: " .. tostring(emf.net.send("ping", text(args))))
end)
emf.command("big", " <n>: n bytes of text to the server", function(world, args)
  local n = tonumber(args[1]) or 3000
  local s = string.rep("abcdefghij", math.ceil(n / 10)):sub(1, n)
  emf.reply("big " .. n .. ": " .. tostring(emf.net.send("ping", s)))
end)
emf.command("bin", ": every byte 0..255 to the server", function(world, args)
  local t = {}
  for i = 0, 255 do t[#t + 1] = string.char(i) end
  emf.reply("bin: " .. tostring(emf.net.send("ping", table.concat(t))))
end)
emf.command("peer", ": the server's channel version", function(world, args)
  emf.reply("peer: " .. tostring(emf.net.peer(nil, 0, "ping")))
end)
emf.command("all", " <text>: the server -> every player", function(world, args)
  emf.reply("all: " .. tostring(emf.net.send_all(world, "ping", text(args))))
end)
