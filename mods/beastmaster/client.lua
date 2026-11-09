-- Beastmaster on the game of a player who joined, the player's side of the beastmaster/ui channel (wire.lua): it shows
-- what the server's Beastmaster sends (notes) and decides nothing itself.
-- M.new(env) where env has: now(world), log(text).
local wire = require("wire")

local M = {}

local C = {}
C.__index = C

-- What each kind of message from the server does: fn(self, world, msg).
local on = {}

function M.new(env)
	local c = setmetatable({ env = env }, C)
	c:reset()
	return c
end

function C:reset()
end

function on.note(self, world, msg)
	if type(msg.text) == "string" then emf.notify(msg.text) end
end

-- A message from the server (the core's handler, on the client world).
function C:on_message(world, from, data)
	local msg = wire.decode(data)
	if not msg then return end
	local fn = on[msg.t]
	if fn then fn(self, world, msg) end
end

return M
