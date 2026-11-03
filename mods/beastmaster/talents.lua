-- Talents: a beast gets a talent point for each loyalty above 1 (9 at loyalty 10) and spends them at a stable. The
-- numbers are all in LIST below: change them there.
--   strength, toughness, swiftness: per point, +damage, +health, +speed (on top of its role and loyalty)
--   second_wind: a killing blow leaves it at `heal` of its health; then `cooldown_s` before it works again
--   ambush: per point, its first hit in a fight deals `bonus` more (0.5: +50 %)
--   guard: it takes no damage for the first `guard_s[points]` seconds of a fight, at most once every `cooldown_s`
-- A beast's points spent are kept with the world in the roster's t lines (roster.lua): "strength:2,guard:1".
local loyalty = require("loyalty")

local M = {}

M.LIST = {
	{ id = "strength", name = "Strength", max = 5, damage = 0.04 },
	{ id = "toughness", name = "Toughness", max = 5, health = 0.05 },
	{ id = "swiftness", name = "Swiftness", max = 3, speed = 0.04 },
	{ id = "second_wind", name = "Second Wind", max = 1, heal = 0.20, cooldown_s = 300 },
	{ id = "ambush", name = "Ambush", max = 2, bonus = 0.50 },
	{ id = "guard", name = "Guard", max = 2, guard_s = { 3, 5 }, cooldown_s = 30 },
}

-- A fight starts with the first hit to or from a beast after this many seconds without any.
M.FIGHT_GAP_S = 10

-- Swiftness multiplies these entries of the creature's Movement storage (Lua indices): 2 is Movement's multiplier (1
-- normally; the taming hold sets it to 0 to keep a beast still). The speeds per gait are 4 (walk) and 5 (run), 3 sneak,
-- 6 sprint, 7 combat.
M.SPEED_ENTRIES = { 2 }

local by_id = {}
for _, t in ipairs(M.LIST) do by_id[t.id] = t end

local function key_of(word)
	return (tostring(word or ""):lower():gsub("[%s_]", ""))
end

-- A talent by its id or name, any case, spaces and underscores ignored ("second wind", "Second_Wind"); nil when none.
function M.find(word)
	local k = key_of(word)
	if k == "" then return nil end
	for _, t in ipairs(M.LIST) do
		if key_of(t.id) == k or key_of(t.name) == k then return t end
	end
	return nil
end

-- "Wolf 2 second wind" -> "Wolf 2", second_wind: the talent named by the last one or two words, and the rest (the
-- beast's name or number); the whole text and nil when it ends with no talent.
function M.split(text)
	local words = {}
	for w in tostring(text or ""):gmatch("%S+") do words[#words + 1] = w end
	for n = 2, 1, -1 do
		if #words > n then
			local t = M.find(table.concat(words, " ", #words - n + 1))
			if t then return table.concat(words, " ", 1, #words - n), t end
		end
	end
	return table.concat(words, " "), nil
end

function M.rank(b, id)
	return b.talents and b.talents[id] or 0
end

-- The points loyalty gives: one for each loyalty above 1.
function M.points(b, pace)
	return loyalty.level(b.points, pace) - 1
end

function M.spent(b)
	local n = 0
	for _, t in ipairs(M.LIST) do n = n + M.rank(b, t.id) end
	return n
end

function M.left(b, pace)
	return math.max(0, M.points(b, pace) - M.spent(b))
end

-- Points spent beyond what loyalty gives now (loyalty set lower, or a slower pace): they stay until a reset.
function M.over(b, pace)
	return math.max(0, M.spent(b) - M.points(b, pace))
end

function M.health(b)
	return 1 + M.rank(b, "toughness") * by_id.toughness.health
end

function M.damage(b)
	return 1 + M.rank(b, "strength") * by_id.strength.damage
end

function M.speed(b)
	return 1 + M.rank(b, "swiftness") * by_id.swiftness.speed
end

-- Ambush's bonus share for its first hit in a fight (0: none).
function M.ambush(b)
	return M.rank(b, "ambush") * by_id.ambush.bonus
end

-- Guard's seconds at the start of a fight (0: none).
function M.guard_s(b)
	local n = M.rank(b, "guard")
	return n > 0 and by_id.guard.guard_s[math.min(n, #by_id.guard.guard_s)] or 0
end

-- Seconds after Guard worked before it can work again.
function M.guard_cooldown_s()
	return by_id.guard.cooldown_s or 0
end

-- Second Wind's entry when the beast has it, else nil.
function M.second_wind(b)
	return M.rank(b, "second_wind") > 0 and by_id.second_wind or nil
end

local function plural(n, word)
	return string.format("%d %s%s", n, word, n == 1 and "" or "s")
end

local function pct(x)
	return math.floor(x * 100 + 0.5)
end

-- What `n` points of talent `t` do; with 0, what a point does.
function M.effect(t, n)
	if t.damage or t.health or t.speed then
		local per, what = t.damage or t.health or t.speed, t.damage and "damage" or t.health and "health" or "speed"
		if n == 0 then return string.format("+%d %% %s a point", pct(per), what) end
		return string.format("+%d %% %s", pct(per * n), what)
	elseif t.heal then
		return string.format("a killing blow leaves it at %d %% health, then %d min before it works again", pct(t.heal),
			math.floor(t.cooldown_s / 60 + 0.5))
	elseif t.bonus then
		if n == 0 then return string.format("+%d %% damage a point on its first hit in a fight", pct(t.bonus)) end
		return string.format("+%d %% damage on its first hit in a fight", pct(t.bonus * n))
	elseif t.guard_s then
		local s = t.guard_s[math.max(1, math.min(n, #t.guard_s))]
		local text = string.format("no damage for the first %d s of a fight", s)
		if n == 0 and #t.guard_s > 1 then
			text = text .. string.format(" (%d s with %d points)", t.guard_s[#t.guard_s], #t.guard_s)
		end
		if (t.cooldown_s or 0) > 0 then text = text .. string.format(", once every %d s", t.cooldown_s) end
		return text
	end
	return ""
end

-- "Strength 2/5: +8 % damage"
function M.row_text(b, t)
	local n = M.rank(b, t.id)
	return string.format("%s %d/%d: %s", t.name, n, t.max, M.effect(t, n))
end

-- "3 points (1 left)", "1 point (2 too many: reset to choose again)", "no points yet (one for each loyalty above 1)"
function M.summary(b, pace)
	local points, spent = M.points(b, pace), M.spent(b)
	if points <= 0 and spent == 0 then return "no points yet (one for each loyalty above 1)" end
	local over = M.over(b, pace)
	if over > 0 then return string.format("%s (%d too many: reset to choose again)", plural(points, "point"), over) end
	return string.format("%s (%d left)", plural(points, "point"), M.left(b, pace))
end

-- Spends a point of talent `t` on `b`: true and what it does now, or false and why not.
function M.spend(b, t, pace)
	local n = M.rank(b, t.id)
	if n >= t.max then return false, string.format("%s's %s is full (%d/%d).", b.name, t.name, n, t.max) end
	local over = M.over(b, pace)
	if over > 0 then
		return false, string.format("%s has %s spent too many: reset to choose again.", b.name, plural(over, "talent point"))
	end
	if M.left(b, pace) <= 0 then
		return false, string.format("%s has no talent point left (%d of %d spent).", b.name, M.spent(b), M.points(b, pace))
	end
	b.talents = b.talents or {}
	b.talents[t.id] = n + 1
	return true, string.format("%s: %s %d/%d, %s (%s left).", b.name, t.name, n + 1, t.max, M.effect(t, n + 1),
		plural(M.left(b, pace), "point"))
end

-- Every point back; returns how many were spent.
function M.reset(b)
	local n = M.spent(b)
	b.talents = nil
	return n
end

-- The points spent as a roster line's field ("strength:2,guard:1", in LIST order), or nil when none.
function M.format(b)
	local parts = {}
	for _, t in ipairs(M.LIST) do
		local n = M.rank(b, t.id)
		if n > 0 then parts[#parts + 1] = t.id .. ":" .. n end
	end
	return #parts > 0 and table.concat(parts, ",") or nil
end

-- A roster line's field back: talents this version does not know are skipped, a rank over a talent's max is cut to it.
-- nil when nothing is left.
function M.parse(text)
	local out
	for id, n in tostring(text or ""):gmatch("([%w_]+):(%d+)") do
		local t = by_id[id]
		n = tonumber(n)
		if t and n and n > 0 then
			out = out or {}
			out[id] = math.min(n, t.max)
		end
	end
	return out
end

return M
