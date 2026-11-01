-- The beasts a Beastmaster can tame and call.
--   template:   the game template a call spawns (it runs the enemy AI, which follows StartGuardingEvent)
--   graph:      the animation graph a wild creature of that kind runs (its template data on the server world:
--               world:template("AnimationGraphResourceComponent2", id).animationGraph; render models are client-side)
--   also:       another graph that is this kind too (the bear pack 2's bear runs its own graph copy, the older
--               pack's ran the wolf's)
--   factions:   when several kinds share a graph: the factions that make it this kind (the wolf's graph is also the
--               scavengers' hound's)
--               a beast on a skeleton of its own (the beasts pack's snake) runs its own copy of a game graph: its
--               graph alone tells it apart
--   min_scale / max_scale: when kinds share a graph and a faction, the creature's ScaleComponent tells them apart
--               (the bear pack's bear runs the wolf's graph at 1.4x: seen in game, game wolves have no ScaleComponent;
--               the beasts pack's aurochs runs the boar's graph at 1.3x).
--               M.of takes the first that fits, in list order
--   pack:       the content pack (an EML data patch) the beast comes from, named for notes; a game without that pack
--               has no such template: the call fails, a wild one never comes
--   health_mul: times the game's base health (health.lua); the pack's template sets size, damage and speed
-- Baits are settings ("bait_<id>"), read when used.
local M = {}

local WILDBEAST, SCAVENGER = 6, 4
local WOLF_GRAPH = "3180813f-a50d-47fc-be92-29a2383bd47d"
local BOAR_GRAPH = "aed0ecec-8a98-4fd3-a450-ab4c508b0ad4"
local BEAR_SCALE = 1.3     -- the pack's bear is 1.4
local AUROCHS_SCALE = 1.2  -- the pack's aurochs is 1.3
local PACK = "the beasts pack (emf_beastmaster_beasts)"

M.list = {
	{ id = "wolf", name = "Wolf", template = "478381c5-585f-4264-b373-dbb1ea596eca",
		graph = WOLF_GRAPH, factions = { [WILDBEAST] = true }, max_scale = BEAR_SCALE },
	{ id = "boar", name = "Boar", template = "255fd8d9-4447-445d-8ffc-4d9dc0afdd16",
		graph = BOAR_GRAPH, max_scale = AUROCHS_SCALE },
	{ id = "sabretooth", name = "Sabretooth", template = "50b96064-3ffc-47a2-8c7d-1d592a3c459c",
		graph = "9c1b120e-4901-495c-9cc0-5de134fbe140" },
	{ id = "hound", name = "Hound", template = "87575915-bc75-4611-ab71-0cd4e79535b1",
		graph = WOLF_GRAPH, factions = { [SCAVENGER] = true } },
	-- the bear pack (tools/models/packs/beastmaster_bear.json); GUIDs fixed forever. Pack 2.0: a bear on a skeleton
	-- of its own with its own graph copy; 1.0's bear ran the wolf's graph (both at 1.4x)
	{ id = "bear", name = "Bear", template = "b3a7c2e1-5f4d-4c8b-9e21-7a0d4b6e2f02",
		model = "b3a7c2e1-5f4d-4c8b-9e21-7a0d4b6e2f01", pack = "the bear pack (emf_beastmaster_bear)", health_mul = 2.5,
		plural = "bears",
		graph = "b3a7c2e1-5f4d-4c8b-9e21-7a0d4b6e2f05", also = WOLF_GRAPH,
		factions = { [WILDBEAST] = true }, min_scale = BEAR_SCALE },
	-- the beasts pack (tools/models/packs/beastmaster_beasts.json); GUIDs fixed forever
	{ id = "aurochs", name = "Aurochs", template = "d5f2b8e3-7c4a-4e9f-8b21-6a3c8d4e0f02",
		model = "d5f2b8e3-7c4a-4e9f-8b21-6a3c8d4e0f01", pack = PACK, health_mul = 3,
		plural = "aurochs",
		graph = BOAR_GRAPH, factions = { [WILDBEAST] = true }, min_scale = AUROCHS_SCALE },
	{ id = "snake", name = "Snake", template = "d4c87337-8c53-41df-8488-8079046e1502",
		model = "d4c87337-8c53-41df-8488-8079046e1501", pack = PACK, health_mul = 2,
		plural = "snakes",
		graph = "d4c87337-8c53-41df-8488-8079046e1505", factions = { [WILDBEAST] = true } },
	-- the beasts pack's rare snakes: a wild snake is one of them by chance (rare_of, chance: wild.lua), bigger (the
	-- pack's ScaleComponent), tougher, and with a bonus for whoever slays it (rare.lua: runes, the chance of a tier-2
	-- rune, and the chance of a gear item of that rarity at the slayer's level)
	{ id = "crimson_snake", name = "Crimson snake", template = "d4c87337-8c53-41df-8488-8079046e1512",
		model = "d4c87337-8c53-41df-8488-8079046e1511", pack = PACK, health_mul = 3, plural = "crimson snakes",
		graph = "d4c87337-8c53-41df-8488-8079046e1515", factions = { [WILDBEAST] = true },
		rare_of = "snake", chance = 0.01,
		bonus = { runes = { 3, 5 }, rune2 = 0.15, gear = 0.05, gear_rarity = "epic" } },
	{ id = "golden_snake", name = "Golden king snake", template = "d4c87337-8c53-41df-8488-8079046e1522",
		model = "d4c87337-8c53-41df-8488-8079046e1521", pack = PACK, health_mul = 4, plural = "golden king snakes",
		graph = "d4c87337-8c53-41df-8488-8079046e1525", factions = { [WILDBEAST] = true },
		rare_of = "snake", chance = 0.004,
		bonus = { runes = { 4, 6 }, rune2 = 0.25, gear = 0.10, gear_rarity = "epic" } },
	{ id = "albino_snake", name = "Albino snake", template = "d4c87337-8c53-41df-8488-8079046e1532",
		model = "d4c87337-8c53-41df-8488-8079046e1531", pack = PACK, health_mul = 5, plural = "albino snakes",
		graph = "d4c87337-8c53-41df-8488-8079046e1535", factions = { [WILDBEAST] = true },
		rare_of = "snake", chance = 0.0025,
		bonus = { runes = { 6, 10 }, rune2 = 0.50, gear = 0.25, gear_rarity = "legendary" } },
}

local by_id = {}
for _, s in ipairs(M.list) do by_id[s.id] = s end

-- How a text names a kind (a species or its id): "albino snake", its name in lower case.
function M.word(sp)
	if type(sp) == "string" then sp = by_id[sp] or { name = sp } end
	return sp.name:lower()
end

-- "a wolf", "an aurochs", "an albino snake".
function M.a(sp)
	local w = M.word(sp)
	return (w:find("^[aeiou]") and "an " or "a ") .. w
end

-- The kind a new wild `sp` turns out to be for a roll in [0, 1): each of its rare kinds has its own share of the
-- rolls (chance; the rarest first), the rest stay `sp`.
function M.variant(sp, roll)
	local rare = {}
	for _, r in ipairs(M.list) do
		if r.rare_of == sp.id then rare[#rare + 1] = r end
	end
	table.sort(rare, function(a, b) return a.chance < b.chance end)
	local acc = 0
	for _, r in ipairs(rare) do
		acc = acc + r.chance
		if roll < acc then return r end
	end
	return sp
end

-- The kind of a creature running `graph` (GUID text) in `faction` (a number) at `scale` (its ScaleComponent, nil when
-- it has none: 1), or nil.
function M.of(graph, faction, scale)
	if not graph then return nil end
	graph = tostring(graph):lower()
	scale = scale or 1
	for _, s in ipairs(M.list) do
		if (s.graph == graph or s.also == graph) and (not s.factions or s.factions[faction])
			and (not s.min_scale or scale >= s.min_scale) and (not s.max_scale or scale < s.max_scale) then
			return s
		end
	end
	return nil
end

function M.by_id(id)
	return id and by_id[tostring(id):lower()] or nil
end

return M
