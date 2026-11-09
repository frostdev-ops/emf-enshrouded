-- Beastmaster's screens for players who joined, the server's side of the beastmaster/ui channel (wire.lua): notes
-- instead of chat lines. The server decides everything; a player's game only shows what it is sent and sends back
-- what was clicked, and every message from it is checked here as the chat commands are checked.
-- M.new(env) where env has: now(world), log(text).
local wire = require("wire")

local M = {}

local R = {}
R.__index = R

-- What each kind of message from a player's game does: fn(self, world, player, account, msg).
local on = {}

function M.new(env)
	local r = setmetatable({ env = env }, R)
	r:reset()
	return r
end

-- A new world: no player's screen.
function R:reset()
end

-- Whether `player` gets Beastmaster's screens on their own game: a player who joined (not this game's own) whose game
-- has the channel at this version.
function R:is_remote(world, player)
	if not player or player == 0 or player == emf.local_player() then return false end
	return wire.peer(world, player)
end

-- A note on `player`'s screen; false when their game does not take it (they get a chat line instead).
function R:note(world, player, text)
	return self:is_remote(world, player) and wire.send_to(world, player, { t = "note", text = text })
end

-- A message from a player's game (the core's handler, on the server world).
function R:on_message(world, from, data)
	local account = from and from.account or "?"
	local msg, why = wire.decode(data)
	if not msg then
		wire.log_once(string.format("a message from %s was dropped: %s", account, why))
		return
	end
	local player = from.player
	if not player or player == 0 or world:account(player) ~= account then
		wire.log_once(string.format("a message from %s was dropped: that player is not in the world", account))
		return
	end
	local fn = on[msg.t]
	if not fn then
		wire.log_once(string.format("a message from %s was dropped: an unknown kind (%s)", account, msg.t))
		return
	end
	fn(self, world, player, account, msg)
end

return M
