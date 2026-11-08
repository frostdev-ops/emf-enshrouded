-- Loyalty: how long a beast has fought beside its owner, 1 to 10. Points come from the kills it takes part in; from
-- loyalty L to L + 1 takes 10 x L points (fewer with a faster pace: the loyalty_pace setting). At loyalty L a beast has
-- (L - 1) / 9 x 20 % more health and damage and is (L - 1) / 9 x 25 % bigger (x1.2 and x1.25 at loyalty 10).
local M = {}
M.MAX = 10

local function pace_of(pace)
	return (type(pace) == "number" and pace > 0) and pace or 1
end

-- The points a beast needs in all to reach `level`.
function M.points_for(level, pace)
	local p = pace_of(pace)
	local total = 0
	for l = 1, math.min(level, M.MAX) - 1 do total = total + math.ceil(10 * l / p) end
	return total
end

-- The loyalty of a beast with `points`.
function M.level(points, pace)
	local l = 1
	while l < M.MAX and (points or 0) >= M.points_for(l + 1, pace) do l = l + 1 end
	return l
end

-- Health and damage at `level`, against loyalty 1.
function M.factor(level)
	return 1 + (level - 1) / 9 * 0.2
end

-- Size at `level`, against loyalty 1.
function M.size(level)
	return 1 + (level - 1) / 9 * 0.25
end

-- The components a beast of kind `sp` with `points` is spawned with (emf.spawn's): its size from loyalty on top of its
-- kind's (the game draws a creature at the size it is made with); nil at loyalty 1.
function M.spawn_size(sp, points, pace)
	local lvl = M.level(points, pace)
	if lvl <= 1 then return nil end
	return { ScaleComponent = { uniformScale = (sp.scale or 1) * M.size(lvl) } }
end

return M
