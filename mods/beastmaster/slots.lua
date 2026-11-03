-- Active slots: how many beasts a player can have out at once (and, once the world has a stable, which ones can be
-- called). One at the start; slot 2, 3 and 4 open at the character levels the settings slot2_level, slot3_level and
-- slot4_level give (10, 20 and 30; 0 turns that slot off).
local M = {}
M.MAX = 4
local DEFAULT = { [2] = 10, [3] = 20, [4] = 30 }

-- The level that opens slot n (0 for slot 1; nil when that slot is off).
function M.level_for(n, setting)
	if n == 1 then return 0 end
	local v = setting("slot" .. n .. "_level")
	if v == nil then v = DEFAULT[n] end
	if not v or v <= 0 then return nil end
	return v
end

-- The slots open at `level`.
function M.unlocked(level, setting)
	local n = 1
	for i = 2, M.MAX do
		local l = M.level_for(i, setting)
		if l and (level or 0) >= l then n = n + 1 end
	end
	return n
end

-- The next slot to open above `level`: its number and level, or nil when none is left.
function M.next(level, setting)
	for i = 2, M.MAX do
		local l = M.level_for(i, setting)
		if l and (level or 0) < l then return i, l end
	end
	return nil
end

return M
