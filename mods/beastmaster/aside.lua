-- Summons that step aside: they leave the world for a while and come back beside their owner, free, when it ends.
-- While a player builds next to them (their owner, or another player within NEAR_M): the game most likely keeps a
-- no-build zone around every creature that runs the enemy AI, whatever its faction (the program
-- add_no_buildzones_enemy, docs/ENGINE_MAP.md; not seen in game), so one at your side would block building and placing
-- there. And after the aside command, until it is typed again, a summons is called up, or none is left.
-- (A copy of mods/necromancer/aside.lua: each mod ships on its own.)
-- M.new(env) where env has setting(key); the setting "make_room" switches stepping aside while building on and off.
local M = {}
local A = {}
A.__index = A

local GRACE_S = 3   -- they come back this long after the building stopped (the hammer goes back and forth)
local NEAR_M = 20   -- another player building this close to the summons, or to their owner, counts too

function M.new(env)
	local a = setmetatable({ env = env }, A)
	a:reset()
	return a
end

-- A new world (or a reload): no one is aside.
function A:reset()
	self.manual = {}   -- account -> true: stepped aside by the command
	self.seen = {}     -- player entity -> when it was last seen building
	self.players = {}  -- the player entities present at the last check (their cursors are read every step)
	self.away = {}     -- account -> why its summons are aside ("build", "near", "command"), as of the last check
	self.told = {}     -- "account why" -> true: told once
end

-- The player holds something to build or place: the game's building cursor has an item (ServerCursor, which the
-- building programs write on the server; NetworkCursor is its copy for the clients).
function M.building(world, player)
	local c = world:get("ServerCursor", player)
	if c and c.currentBuildingItemInstance.itemId ~= 0 then return true end
	local n = world:get("NetworkCursor", player)
	return n ~= nil and n.currentBuildingItemId ~= 0
end

local function recent(t, now)
	return t ~= nil and now >= t and now - t <= GRACE_S
end

-- Every step: reads the building cursors of the players present at the last check (two component reads each). True
-- when one of them started building just now: the caller then checks at once instead of at the next second.
function A:watch(world, now)
	if not self.env.setting("make_room") then
		self.seen = {}
		return false
	end
	local started = false
	for _, p in ipairs(self.players) do
		if M.building(world, p) then
			if not recent(self.seen[p], now) then started = true end
			self.seen[p] = now
		end
	end
	return started
end

-- At each check: the players present now (account -> entity), whose cursors watch reads.
function A:set_players(present)
	local list, here = {}, {}
	for _, e in pairs(present) do
		list[#list + 1] = e
		here[e] = true
	end
	self.players = list
	for e in pairs(self.seen) do
		if not here[e] then self.seen[e] = nil end
	end
end

local function within(world, a, x, y, z)
	local ax, ay, az = world:position(a)
	return ax ~= nil and math.sqrt((ax - x) ^ 2 + (ay - y) ^ 2 + (az - z) ^ 2) <= NEAR_M
end

-- Whether `account`'s summons stay aside now, and why: "build" (their owner builds, or stopped less than GRACE_S ago),
-- "near" (another player does, within NEAR_M of the owner, where they come back, or of one of `spots`, the summons'
-- entity ids in the world), "command" (the aside command).
function A:check(world, account, owner, now, spots)
	local why
	if self.env.setting("make_room") and owner then
		if M.building(world, owner) then self.seen[owner] = now end
		if recent(self.seen[owner], now) then
			why = "build"
		else
			for _, p in ipairs(self.players) do
				if p ~= owner and recent(self.seen[p], now) then
					local x, y, z = world:position(p)
					local close = x ~= nil and within(world, owner, x, y, z)
					for _, id in ipairs(x and not close and spots or {}) do
						if within(world, id, x, y, z) then
							close = true
							break
						end
					end
					if close then
						why = "near"
						break
					end
				end
			end
		end
	end
	if not why and self.manual[account] then why = "command" end
	self.away[account] = why
	return why ~= nil, why
end

-- Building keeps them aside now ("build" or "near"), whatever the command says: a call or raise would only go aside.
function A:building(world, account, owner, now, spots)
	local _, why = self:check(world, account, owner, now, spots)
	return why == "build" or why == "near", why
end

-- The aside command: true when the summons step aside now, false when the command's aside ends.
function A:toggle(account)
	self.manual[account] = not self.manual[account] or nil
	if not self.manual[account] and self.away[account] == "command" then self.away[account] = nil end
	return self.manual[account] == true
end

-- A call, raise, gather, the Come back item, or no summons left ends the command's aside: true when it was on.
function A:release(account)
	local was = self.manual[account] == true
	self.manual[account] = nil
	if self.away[account] == "command" then self.away[account] = nil end
	return was
end

-- Aside by the command (until it ends).
function A:by_command(account)
	return self.manual[account] == true
end

-- Aside by the command, or while building (as the last check saw it).
function A:is_aside(account)
	return self.manual[account] == true or self.away[account] ~= nil
end

-- True the first time `account`'s summons step aside for `why` (to say why once).
function A:first(account, why)
	local key = account .. " " .. tostring(why)
	if self.told[key] then return false end
	self.told[key] = true
	return true
end

return M
