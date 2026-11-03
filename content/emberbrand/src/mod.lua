-- Emberbrand: an example of new content for Enshrouded (an EML data patch; see docs/modding/content.md).
-- It adds one item, a sword cloned from the Bronze Sword with its own name and stats, and a recipe for it that the
-- crafting menu lists (own crafting, weapons; 5 wood logs, known once you have had wood logs). It uses EMF's helper,
-- emf_content.lua (a copy of content/lib/emf_content.lua next to this file).
--
-- The GUIDs below are FIXED FOREVER: a save stores items by ItemId = FNV-1a(GUID), so a new GUID would make every
-- saved Emberbrand vanish. Copy this mod for your own item and pick new GUIDs once (any GUID generator), then never
-- change them.
if not loader.features.patch then
	return
end

local content = require("emf_content")

local ITEM_GUID   = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e11"
local NAME_GUID   = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e12"
local RECIPE_GUID = "5e3a1f4c-2b7d-4e8a-9c61-7f0d2a4b8e13"
local DEBUG_NAME  = "Weapon_T1_1H_Sword_Emberbrand"   -- what EMF's "items" and "give" commands use

local BRONZE_SWORD = "1d622bd9-8170-41b1-822e-d3984b386d95" -- Weapon_T1_1H_SwordEpic_01
local WOOD_LOGS    = "3f4093ae-75ed-4e5d-bd0b-784ba83b88f5" -- Material_T1_Wood_Logs
local WEAPONS      = "2632c31c-37fe-48f6-b788-dfa649065edb" -- the own crafting menu's weapons group

local item = content.add_item{
	guid = ITEM_GUID, name_guid = NAME_GUID, debug_name = DEBUG_NAME, name = "Emberbrand",
	clone_of = BRONZE_SWORD,
	edit = function(it)
		it.damageSetup.dmgMod = 1.75                          -- the Bronze Sword: 1.0
		it.damageSetup.distribution.magicalFireDamage = 0.5   -- and some fire
	end,
}

local recipe = content.add_recipe{
	guid = RECIPE_GUID, debug_name = "Craft_" .. DEBUG_NAME,
	output = {ITEM_GUID, 1},
	inputs = {{WOOD_LOGS, 5}},
	like = "Consumable_Tool_Campfire_PK",  -- own crafting, unlocked once you have had wood logs
	menu = {group = WEAPONS},
}

content.finish()

print(string.format("emf_emberbrand: added %s (item 0x%08x, recipe 0x%08x)%s", DEBUG_NAME, item, recipe,
	loader.is_server and " on the dedicated server (no icon, name or menu entry: no UI there)" or ""))
