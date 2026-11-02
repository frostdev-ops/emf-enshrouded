-- Every player's collection of tamed beasts and their mode, kept with the world (one line per beast, one per mode):
--   b <account> <species> <rest seconds left> <out 0|1> <name>
--   m <account> <passive|defending|aggressive>
-- account is world:account's text ("0x" + 16 hex digits); the name is last and may hold spaces. A player without an m
-- line is defending.
local M = {}
local R = {}
R.__index = R

M.MODES = { "passive", "defending", "aggressive" }
local MODE = { passive = true, defending = true, aggressive = true }

function M.new()
	return setmetatable({ by = {}, modes = {} }, R)
end

-- How a player's beasts fight: passive, defending (the first) or aggressive.
function R:mode(account)
	return self.modes[account] or "defending"
end

-- Sets it; false for a word that is not a mode.
function R:set_mode(account, mode)
	if not MODE[mode] then return false end
	self.modes[account] = mode ~= "defending" and mode or nil
	return true
end

function R:list(account)
	return self.by[account] or {}
end

local function taken(list, name)
	for _, b in ipairs(list) do
		if b.name:lower() == name:lower() then return true end
	end
	return false
end

-- Adds a beast; nil and why when the collection holds `max` already.
function R:add(account, species_id, base_name, max)
	local list = self.by[account] or {}
	if #list >= max then return nil, string.format("the collection is full (%d beasts)", max) end
	local name, n = base_name, 1
	while taken(list, name) do
		n = n + 1
		name = base_name .. " " .. n
	end
	local beast = { species = species_id, name = name, rest = 0, out = false }
	list[#list + 1] = beast
	self.by[account] = list
	return beast
end

-- By position in the list (1..n) or by name (any case).
function R:find(account, key)
	local list = self.by[account] or {}
	local n = tonumber(key)
	if n then return list[n] end
	if not key then return nil end
	for _, b in ipairs(list) do
		if b.name:lower() == tostring(key):lower() then return b end
	end
	return nil
end

function R:remove(account, beast)
	local list = self.by[account] or {}
	for i, b in ipairs(list) do
		if b == beast then
			table.remove(list, i)
			return true
		end
	end
	return false
end

function R:accounts()
	local out = {}
	for account in pairs(self.by) do out[#out + 1] = account end
	table.sort(out)
	return out
end

function R:serialize()
	local lines = {}
	local moded = {}
	for account in pairs(self.modes) do moded[#moded + 1] = account end
	table.sort(moded)
	for _, account in ipairs(moded) do lines[#lines + 1] = string.format("m %s %s", account, self.modes[account]) end
	for _, account in ipairs(self:accounts()) do
		for _, b in ipairs(self.by[account]) do
			lines[#lines + 1] = string.format("b %s %s %d %d %s", account, b.species, math.max(0, math.ceil(b.rest)),
				b.out and 1 or 0, b.name)
		end
	end
	return table.concat(lines, "\n")
end

-- species saved by an earlier version that this one does not have: the beast loads as the species it was made from
-- (Beastmaster 1.1's panther was a small sabretooth)
local RETIRED = { panther = "sabretooth" }

function M.parse(text)
	local r = M.new()
	for line in (text or ""):gmatch("[^\r\n]+") do
		local m_account, mode = line:match("^m (%S+) (%S+)$")
		if m_account then r:set_mode(m_account, mode) end
		local account, species, rest, out, name = line:match("^b (%S+) (%S+) (%d+) ([01]) (.+)$")
		if account then
			local list = r.by[account] or {}
			species = RETIRED[species] or species
			list[#list + 1] = { species = species, name = name, rest = tonumber(rest), out = out == "1" }
			r.by[account] = list
		end
	end
	return r
end

return M
