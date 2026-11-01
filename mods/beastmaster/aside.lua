-- Summons that step aside: they leave the world for a while and come back beside their owner, free, when it ends.
-- While their owner builds: the game keeps a no-build zone around every creature that runs the enemy AI (the program
-- add_no_buildzones_enemy, docs/ecs_programs.md), and summons do, so one at your side blocks building and placing
-- there. And after the aside command, until it is typed again or the owner calls up a summons.
-- (A copy of mods/necromancer/aside.lua: each mod ships on its own.)
-- M.new(env) where env has setting(key); the setting "make_room" switches stepping aside while building on and off.
local M = {}
local A = {}
A.__index = A

local GRACE_S = 3  -- they come back this long after their owner stopped building (the hammer goes back and forth)

function M.new(env)
	local a = setmetatable({ env = env }, A)
	a:reset()
	return a
end

-- A new world (or a reload): no one is aside.
function A:reset()
	self.manual = {}  -- account -> true: stepped aside by the command
	self.seen = {}    -- account -> when its player was last seen building
	self.told = {}    -- account -> true: told why once
end

-- The player holds something to build or place: the game's building cursor has an item (ServerCursor, which the
-- building programs write on the server; NetworkCursor is its copy for the clients).
function M.building(world, player)
	local c = world:get("ServerCursor", player)
	if c and c.currentBuildingItemInstance.itemId ~= 0 then return true end
	local n = world:get("NetworkCursor", player)
	return n ~= nil and n.currentBuildingItemId ~= 0
end

-- True while `account`'s player (entity `player`) builds, or stopped less than GRACE_S ago (make_room on).
function A:building(world, account, player, now)
	if not self.env.setting("make_room") then
		self.seen[account] = nil
		return false
	end
	if player and M.building(world, player) then self.seen[account] = now end
	local t = self.seen[account]
	if t and (now < t or now - t > GRACE_S) then
		self.seen[account] = nil  -- stopped long enough ago, or the clock restarted
		return false
	end
	return t ~= nil
end

-- Whether `account`'s summons stay aside now, and why: "build" or "command".
function A:check(world, account, player, now)
	if self:building(world, account, player, now) then return true, "build" end
	if self.manual[account] then return true, "command" end
	return false
end

-- The aside command: true when the summons step aside now, false when they come back.
function A:toggle(account)
	self.manual[account] = not self.manual[account] or nil
	return self.manual[account] == true
end

-- A call, raise or gather brings them back: true when they were aside by the command.
function A:release(account)
	local was = self.manual[account] == true
	self.manual[account] = nil
	return was
end

-- Aside by the command, or while building (as the last check saw it).
function A:is_aside(account)
	return self.manual[account] == true or self.seen[account] ~= nil
end

-- True the first time for `account` (to say why once).
function A:first(account)
	if self.told[account] then return false end
	self.told[account] = true
	return true
end

return M
