-- The fights of the beasts out, for their talents (talents.lua). A fight starts with the first hit to or from a beast
-- after talents.FIGHT_GAP_S without any.
--   Guard: for its first seconds the beast's health cannot go under what it had when the fight started; a foe's opening
--     hit is healed back.
--   Second Wind: while ready its health cannot go under 1; when a killing blow leaves it there, it is healed to its
--     share of the maximum and the talent waits its cooldown (kept per beast: a call does not make it ready).
--   Ambush: the beast's first damaging hit on a foe in a fight deals its bonus more, as a real hit.
-- Both floors are the game's minimum health (Health storage [2], as tame.lua's guard of a beast just tamed): the higher
-- one is held, and the game's own minimum comes back when neither holds. Nothing is kept for an entity that is no beast
-- out any more (sent home, fallen, aside): the game gives its id out again.
-- M.new(env) where env has: now(world), tell(world, player, text), owner(world, account) (the owner's entity or nil),
-- strike(world, e, id, target, amount) (Ambush's bonus as a hit from beast entity `id` of entry `e`).
local talents = require("talents")

local M = {}
local F = {}
F.__index = F

local WIND_FLOOR = 1  -- Second Wind holds health at this while ready
local PLAYER_SIDE = { [7] = true, [8] = true, [11] = true }  -- Player, AutomatedPlayer, PlayerAlly

function M.new(env)
	local f = setmetatable({ env = env }, F)
	f:reset()
	return f
end

-- A new world (or a reload): entity ids from before mean nothing now.
function F:reset()
	self.last = {}    -- entity id -> when the last hit to or from it was
	self.guard = {}   -- entity id -> {until_s, floor}: Guard in this fight
	self.ambush = {}  -- entity id -> true while its first damaging hit in this fight is still to come
	self.old = {}     -- entity id -> the game's minimum health before a floor was written
	self.wind = setmetatable({}, { __mode = "k" })  -- beast -> when Second Wind works again
end

-- `id` is no beast of ours now (or a fresh one): what was kept for it goes.
function F:forget(id)
	self.last[id], self.guard[id], self.ambush[id], self.old[id] = nil, nil, nil, nil
end

-- Seconds before Second Wind works again for `beast` (0: ready).
function F:wind_left(world, beast)
	return math.max(0, (self.wind[beast] or 0) - self.env.now(world))
end

-- The floor `beast` (entity `id`) holds now, or nil.
function F:floor_of(id, beast, now)
	local want
	local g = self.guard[id]
	if g and now < g.until_s then want = g.floor end
	if talents.second_wind(beast) and now >= (self.wind[beast] or 0) then want = math.max(want or 0, WIND_FLOOR) end
	return want
end

-- Writes the floor `beast` holds now as its minimum health, or gives the game's back.
function F:hold(world, id, beast, now)
	local want = self:floor_of(id, beast, now)
	if not want and self.old[id] == nil then return end
	local h = world:get("Health", id)
	if not (h and h.dataStorage) then return end
	if want then
		if self.old[id] == nil then self.old[id] = h.dataStorage[2] end
		if h.dataStorage[2] ~= want then h.dataStorage[2] = want end
	else
		h.dataStorage[2] = self.old[id]
		self.old[id] = nil
	end
end

-- A hit to or from beast `id`: a new fight after FIGHT_GAP_S without any (Ambush armed, Guard started). `taken` is the
-- health change the hit made to it (negative: damage; 0 for its own hit).
function F:touch(world, id, beast, now, taken)
	local last = self.last[id]
	self.last[id] = now
	if last and now - last < talents.FIGHT_GAP_S and now >= last then return end
	self.ambush[id] = talents.ambush(beast) > 0 or nil
	local secs = talents.guard_s(beast)
	if secs <= 0 then return end
	local cur, max = emf.health(world, id)
	if not cur or cur <= 0 then return end
	local floor = cur
	if taken < 0 then
		floor = max and math.min(max, cur - taken) or (cur - taken)
		if floor > cur then emf.attribute_add(world, id, "health", floor - cur) end
	end
	self.guard[id] = { until_s = now + secs, floor = math.floor(floor) }
	self:hold(world, id, beast, now)
end

-- A creature Ambush may strike: not a player, not on the players' side.
local function foe(world, id)
	if world:get("PlayerState", id) then return false end
	local f = world:get("Faction", id)
	return f ~= nil and not PLAYER_SIDE[f.faction]
end

-- A HitEvent with a beast of ours on either side (`src_e`, `tgt_e`: their entries, or nil). `credited`: the XP credit
-- has handed this foe's killing blow to an owner.
function F:on_hit(world, source, target, change, src_e, tgt_e, credited)
	if source == target then return end
	local now = self.env.now(world)
	if src_e then self:touch(world, source, src_e.beast, now, 0) end
	if tgt_e then self:touch(world, target, tgt_e.beast, now, change) end
	if not (src_e and self.ambush[source]) or change >= 0 or not foe(world, target) then return end
	self.ambush[source] = nil
	if credited then return end  -- the foe dies by its owner's hit already
	local amount = math.floor(-change * talents.ambush(src_e.beast) + 0.5)
	if amount > 0 then self.env.strike(world, src_e, source, target, amount) end
end

-- Every server step: `ids` are the beasts out (entity id -> {account, beast}). Guard ends, Second Wind gets the beast
-- up, the floors are held.
function F:step(world, ids)
	local now = self.env.now(world)
	for _, t in ipairs({ self.last, self.guard, self.ambush, self.old }) do
		for id in pairs(t) do
			if not ids[id] then t[id] = nil end
		end
	end
	for id, e in pairs(ids) do
		local beast = e.beast
		local g = self.guard[id]
		if g and now >= g.until_s then self.guard[id] = nil end
		local sw = talents.second_wind(beast)
		if sw and now >= (self.wind[beast] or 0) then
			local cur, max = emf.health(world, id)
			if cur and max and cur > 0 and cur <= WIND_FLOOR then
				local to = math.max(WIND_FLOOR + 1, math.floor(max * sw.heal + 0.5))
				emf.attribute_add(world, id, "health", to - cur)
				self.wind[beast] = now + sw.cooldown_s
				local owner = self.env.owner(world, e.account)
				if owner then
					self.env.tell(world, owner, string.format("%s gets up again: Second Wind (%d min before it works again).",
						beast.name, math.floor(sw.cooldown_s / 60 + 0.5)))
				end
			end
		end
		self:hold(world, id, beast, now)
	end
end

return M
