-- The Beasts window (the host presses E at a stable), with two tabs: Stable (the active slots and every beast of the
-- collection) and the selected beast's own (its stats and talents). Selecting a beast (its row or its slot) opens its
-- tab; Stable goes back. Both have Make active, Store and Release for the selected beast; Release asks for a second click
-- within CONFIRM_S. The window is drawn again after every click, and once a second when what it shows changed (a beast
-- fell, rests, came).
-- M.new(env) where env has: setting(key), now(world), beasts() (the roster), level(world, player), state(beast, account)
-- ("out", "at home", "rests 1:20"), mode(account) (how the beasts fight), wind_left(world, beast) (seconds before Second
-- Wind works again), and the stable's
-- actions, each returning what it did: make_active(world, account, player, beast), store(world, account, beast),
-- release(world, account, beast), spend(world, account, beast, talent), reset_talents(world, account, beast).
local species = require("species")
local loyalty = require("loyalty")
local slots = require("slots")
local talents = require("talents")
local threat = require("threat")

local M = {}
M.ID = "stable"
local CONFIRM_S = 5

local W = {}
W.__index = W

function M.new(env)
	local w = setmetatable({ env = env }, W)
	w:reset()
	return w
end

-- A new world: no window.
function W:reset()
	self.open = false
	self.account, self.player = nil, nil
	self.selected = nil  -- the selected beast's name
	self.tab = "stable"  -- "stable" or "beast" (the selected beast's)
	self.confirm = nil   -- {name, at}: Release clicked once
	self.note = nil      -- the line under the buttons: what the last click did
	self.drawn = nil     -- what the window shows now (to draw it again only when that changed)
end

function W:loyalty_of(b)
	return loyalty.level(b.points, self.env.setting("loyalty_pace"))
end

-- The active beasts of the window's account, in list order.
function W:active_list()
	local out = {}
	for _, b in ipairs(self.env.beasts():list(self.account)) do
		if b.active then out[#out + 1] = b end
	end
	return out
end

function W:selection()
	return self.selected and self.env.beasts():find(self.account, self.selected) or nil
end

function W:open_slots(world)
	return slots.unlocked(self.env.level(world, self.player), self.env.setting)
end

-- The tab buttons: Stable, and the selected beast's name; the open one lit.
local function tabs(rows, sel, on_beast)
	rows[#rows + 1] = { button = "tab.stable", text = "Stable", selected = not on_beast }
	if sel then rows[#rows + 1] = { button = "tab.beast", text = sel.name, same_line = true, selected = on_beast } end
	rows[#rows + 1] = { separator = true }
end

-- Make active, Store and Release for the selection, and the line saying what the last click did.
function W:action_rows(rows, sel)
	rows[#rows + 1] = { button = "make_active", text = "Make active", disabled = not (sel and not sel.active) }
	rows[#rows + 1] = { button = "store", text = "Store", same_line = true, disabled = not (sel and sel.active) }
	rows[#rows + 1] = { button = "release", text = "Release", same_line = true, disabled = not sel,
		tooltip = "Let the beast go for good (click twice)" }
	if self.note then rows[#rows + 1] = { text = self.note } end
end

local function mmss(s)
	return string.format("%d:%02d", math.floor(s / 60), math.floor(s % 60))
end

-- The selected beast's tab: its stats (kind, level, loyalty, what its role, loyalty and talents make of its health,
-- damage and speed, where it is), its talents with a + each and Reset talents, and its actions.
function W:beast_rows(world, rows, b)
	local env = self.env
	local pace = env.setting("loyalty_pace")
	local sp = species.by_id(b.species)
	local role_health, role_damage = species.role(sp)
	local lvl = self:loyalty_of(b)
	local f = loyalty.factor(lvl)
	rows[#rows + 1] = { heading = b.name }
	local level = env.level(world, self.player)
	local kind = sp and sp.name or tostring(b.species)
	rows[#rows + 1] = { text = level > 0 and string.format("%s, level %d", kind, level) or kind }
	if lvl >= loyalty.MAX then
		rows[#rows + 1] = { text = string.format("Loyalty %d (the highest)", lvl) }
	else
		local kills = loyalty.points_for(lvl + 1, pace) - (b.points or 0)
		rows[#rows + 1] = { text = string.format("Loyalty %d: %d kill%s to loyalty %d", lvl, kills, kills == 1 and "" or "s",
			lvl + 1), tooltip = "A kill is a hostile creature it hit that dies within 30 s" }
	end
	rows[#rows + 1] = { text = string.format("Health x%.2f, damage x%.2f, speed x%.2f", role_health * f * talents.health(b),
		role_damage * f * talents.damage(b), talents.speed(b)), tooltip = "Its role, loyalty and talents together" }
	if not env.setting("threat") then
		rows[#rows + 1] = { text = "Draws no monsters (Beasts draw monsters is off)" }
	elseif env.mode(self.account) == "passive" then
		rows[#rows + 1] = { text = "Draws no monsters (passive mode)" }
	else
		rows[#rows + 1] = { text = string.format("Draws %d %% of the monsters in a fight",
			math.floor(threat.share(b, env.setting) * 100 + 0.5)),
			tooltip = "All your beasts together at most " .. env.setting("beasts_share_max_pct") .. " %, of a boss at most " ..
				env.setting("boss_share_max_pct") .. " %" }
	end
	local state = env.state(b, self.account)
	rows[#rows + 1] = { text = state:sub(1, 1):upper() .. state:sub(2) .. (b.active and ", active" or ", in the stable") }
	rows[#rows + 1] = { separator = true }
	rows[#rows + 1] = { heading = "Talents: " .. talents.summary(b, pace) }
	local left = talents.left(b, pace)
	for _, t in ipairs(talents.LIST) do
		local n = talents.rank(b, t.id)
		rows[#rows + 1] = { button = "talent." .. t.id, text = "+", disabled = n >= t.max or left <= 0,
			tooltip = "Spend a point on " .. t.name }
		local text = talents.row_text(b, t)
		if t.heal and n > 0 and b.out then
			local wait = env.wind_left(world, b)
			text = text .. (wait > 0 and string.format(" (again in %s)", mmss(wait)) or " (ready)")
		end
		rows[#rows + 1] = { text = text, same_line = true,
			tooltip = string.format("%s: %s (at most %d)", t.name, talents.effect(t, 0), t.max) }
	end
	rows[#rows + 1] = { button = "reset_talents", text = "Reset talents", disabled = talents.spent(b) == 0,
		tooltip = "Every point back, free" }
	rows[#rows + 1] = { separator = true }
	self:action_rows(rows, b)
	return rows
end

-- The window's rows for its account: the open tab.
function W:rows(world)
	local env = self.env
	local list = env.beasts():list(self.account)
	local active = self:active_list()
	local open = self:open_slots(world)
	local sel = self:selection()
	local rows = {}
	tabs(rows, sel, sel ~= nil and self.tab == "beast")
	if sel and self.tab == "beast" then return self:beast_rows(world, rows, sel) end
	rows[#rows + 1] = { heading = string.format("Active %d/%d", #active, open) }
	local shown = 0
	for i = 1, slots.MAX do
		local unlock = slots.level_for(i, env.setting)
		local row
		if i <= open then
			local b = active[i]
			if b then
				row = { slot = "active." .. i, text = b.name, count = self:loyalty_of(b), selected = b == sel,
					tooltip = string.format("%s, loyalty %d, %s", species.word(b.species), self:loyalty_of(b),
						env.state(b, self.account)) }
			else
				row = { slot = "active." .. i, text = "Empty", empty = true, tooltip = "Select a beast and Make active" }
			end
		elseif unlock then
			row = { slot = "active." .. i, text = "Level " .. unlock, locked = true,
				tooltip = string.format("Opens at character level %d", unlock) }
		end
		if row then
			shown = shown + 1
			if shown > 1 then row.same_line = true end
			rows[#rows + 1] = row
		end
	end
	rows[#rows + 1] = { separator = true }
	rows[#rows + 1] = { heading = string.format("Stable %d/%d", #list, env.setting("collection_max")) }
	if #list == 0 then
		rows[#rows + 1] = { text = "No beasts yet: tame one (beat it down, or feed it its bait).", dim = true }
	end
	for _, b in ipairs(list) do
		rows[#rows + 1] = { row = "beast." .. b.name, selected = b == sel,
			text = string.format("%s (%s, loyalty %d, %s%s)", b.name, species.word(b.species), self:loyalty_of(b),
				env.state(b, self.account), b.active and ", active" or "") }
	end
	rows[#rows + 1] = { separator = true }
	self:action_rows(rows, sel)
	return rows
end

local function key_text(key)
	return emf.setting(key) or ""
end

function W:def(world)
	return { title = "Beasts", width = 30, place = "centre", close_key = key_text("stable_key"), world = "server",
		rows = self:rows(world) }
end

-- The rows as one text: the window is drawn again only when this changed.
local function signature(rows)
	local parts = {}
	for _, r in ipairs(rows) do
		local keys = {}
		for k in pairs(r) do keys[#keys + 1] = k end
		table.sort(keys)
		for _, k in ipairs(keys) do parts[#parts + 1] = k .. "=" .. tostring(r[k]) end
		parts[#parts + 1] = "|"
	end
	return table.concat(parts, ";")
end

-- Opens the window for `player` (the host). True when it opened.
function W:show(world, player)
	local account = world:account(player)
	if not account then return false end
	self:reset()
	self.account, self.player = account, player
	local def = self:def(world)
	if not emf.window(M.ID, def, function(w, ev) self:on_event(w, ev) end) then
		self:reset()
		return false
	end
	self.open = true
	self.drawn = signature(def.rows)
	return true
end

-- Draws the window again when what it shows changed (`force`: always).
function W:refresh(world, force)
	if not self.open then return end
	local def = self:def(world)
	local sig = signature(def.rows)
	if not force and sig == self.drawn then return end
	if emf.window_update(M.ID, def) then
		self.drawn = sig
	else
		self.open = false  -- the player closed it meanwhile
	end
end

-- ---- clicks ----
-- Selects `b` and opens its tab (nil: none, the Stable tab).
function W:select(b)
	self.selected = b and b.name or nil
	self.tab = b and "beast" or "stable"
	self.confirm = nil
	self.note = nil
end

function W:release(world, b)
	local now = self.env.now(world)
	local c = self.confirm
	if c and c.name == b.name and now - c.at <= CONFIRM_S then
		self.selected, self.confirm, self.tab = nil, nil, "stable"
		self.note = self.env.release(world, self.account, b)
		return
	end
	self.confirm = { name = b.name, at = now }
	self.note = string.format("Click Release again within %d s to let %s go for good.", CONFIRM_S, b.name)
end

function W:on_event(world, ev)
	if ev.kind == "closed" then
		self.open = false
		return
	end
	if ev.kind ~= "click" or not self.open then return end
	local key = ev.key or ""
	local slot = key:match("^active%.(%d+)$")
	local name = key:match("^beast%.(.+)$")
	local talent = talents.find(key:match("^talent%.(.+)$"))
	local sel = self:selection()
	if key == "tab.stable" then
		self.tab, self.confirm = "stable", nil
	elseif key == "tab.beast" then
		if sel then self.tab, self.confirm = "beast", nil end
	elseif slot then
		local b = self:active_list()[tonumber(slot)]
		if b then self:select(b) end
	elseif name then
		local b = self.env.beasts():find(self.account, name)
		if b then self:select(b) end
	elseif sel and key == "make_active" then
		self.confirm = nil
		self.note = self.env.make_active(world, self.account, self.player, sel)
	elseif sel and key == "store" then
		self.confirm = nil
		self.note = self.env.store(world, self.account, sel)
	elseif sel and key == "release" then
		self:release(world, sel)
	elseif sel and talent then
		self.confirm = nil
		self.note = self.env.spend(world, self.account, sel, talent)
	elseif sel and key == "reset_talents" then
		self.confirm = nil
		self.note = self.env.reset_talents(world, self.account, sel)
	end
	self:refresh(world, true)
end

return M
