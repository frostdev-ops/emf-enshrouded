-- The bonus for slaying a wild rare beast (a kind with `bonus` in species.lua: the rare snakes). The player whose hit
-- was the last one on it gets it; with no player's hit (a beast or a fall killed it) the nearest player within 60 m.
-- Into their inventory: runes (between the kind's two numbers), by chance a tier-2 rune, and by chance one gear item of
-- the kind's rarity at the slayer's level. What does not fit, or a put the game refuses, drops at their feet.
-- Items by their debug names (`items rune` in the console).
local species = require("species")

local M = {}
local R = {}
R.__index = R

M.RUNE = "Material_T1_Token_WeaponUpgrade_Rune01"
M.RUNE2 = "Material_T2_Token_WeaponUpgrade_Rune02"
M.GEAR = "Weapon_T1_1H_SwordEpic_01"

local PLAYER_ALLY = 11
local NEAREST_M = 60

function M.new()
	return setmetatable({ last = {} }, R)
end

-- A new world (or a reload): ids from before mean nothing now.
function R:reset()
	self.last = {}
end

-- The wild kind with a bonus `id` is (alive or just dead), or nil.
local function kind(world, id)
	if world:get("PlayerState", id) then return nil end
	local f = world:get("Faction", id)
	if not f or f.faction == PLAYER_ALLY then return nil end
	local g = world:template("AnimationGraphResourceComponent2", id)
	if not g then return nil end
	local sc = world:get("ScaleComponent", id)
	local sp = species.of(g.animationGraph, f.faction, sc and sc.uniformScale or nil)
	return sp and sp.bonus and sp or nil
end

function R:on_hit(world, source, target)
	if source ~= 0 and world:get("PlayerState", source) and kind(world, target) then self.last[target] = source end
end

local function nearest_player(world, id)
	local x, y, z = world:position(id)
	if not x then return nil end
	local best, best_d = nil, NEAREST_M
	for _, p in ipairs(world:entities()) do
		if world:get("PlayerState", p) then
			local px, py, pz = world:position(p)
			if px then
				local d = math.sqrt((px - x) ^ 2 + (py - y) ^ 2 + (pz - z) ^ 2)
				if d <= best_d then best, best_d = p, d end
			end
		end
	end
	return best
end

local function give(world, player, name, count, opts)
	local item = emf.item(name)
	if not item then
		emf.log("rare beasts: the game has no item named " .. name)
		return
	end
	local queued = emf.put(world, player, item, count, function(w, added)
		if added < count then emf.give(w, player, item, count - added) end
	end, opts)
	if not queued then emf.give(world, player, item, count) end
end

-- What a slayer of `sp` gets: { {name, count, opts}, ... } (math.random decides).
function M.bonus(sp, level)
	local b = sp.bonus
	local out = { { M.RUNE, math.random(b.runes[1], b.runes[2]) } }
	if math.random() < b.rune2 then out[#out + 1] = { M.RUNE2, 1 } end
	if math.random() < b.gear then out[#out + 1] = { M.GEAR, 1, { level = level, rarity = b.gear_rarity } } end
	return out
end

function R:on_died(world, id)
	local slayer = self.last[id]
	self.last[id] = nil
	local sp = kind(world, id)
	if not sp then return end
	if not slayer or not world:position(slayer) then slayer = nearest_player(world, id) end
	if not slayer then return end
	local l = world:get("Level", slayer)
	local level = l and l.dataStorage[1] or 1
	for _, it in ipairs(M.bonus(sp, level)) do give(world, slayer, it[1], it[2], it[3]) end
	emf.log(string.format("rare beasts: player %d slew %s (entity %d)", slayer, species.a(sp), id))
end

return M
