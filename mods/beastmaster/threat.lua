-- Threat: how the monsters in a fight split between a player and their beasts out.
local species = require("species")
local talents = require("talents")

local M = {}

-- The share of the monsters in a fight `beast` draws (0..1): a tank's (boar, bear, aurochs) or a damage beast's
-- setting, and Menace on top; the settings and caps for all of a player's beasts are applied when a monster picks.
function M.share(beast, setting)
	local sp = species.by_id(beast.species)
	local pct = (sp and sp.tank) and setting("tank_share_pct") or setting("damage_share_pct")
	return math.min(1, (pct or 0) / 100 + talents.menace(beast))
end

return M
