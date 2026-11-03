-- The Beasts window (the host presses E at a stable): the active slots and the stable (every beast of the collection),
-- with Make active, Store and Release for the selected beast. Release asks for a second click within CONFIRM_S. The
-- window is drawn again after every click, and once a second when what it shows changed (a beast fell, rests, came).
-- M.new(env) where env has: setting(key), now(world), beasts() (the roster), level(world, player), state(beast, account)
-- ("out", "at home", "rests 1:20"), and the stable's actions, each returning what it did: make_active(world, account,
-- player, beast), store(world, account, beast), release(world, account, beast).
local species = require("species")
local loyalty = require("loyalty")
local slots = require("slots")

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

-- The window's rows for its account.
function W:rows(world)
	local env = self.env
	local list = env.beasts():list(self.account)
	local active = self:active_list()
	local open = self:open_slots(world)
	local sel = self:selection()
	local rows = { { heading = string.format("Active %d/%d", #active, open) } }
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
	rows[#rows + 1] = { button = "make_active", text = "Make active", disabled = not (sel and not sel.active) }
	rows[#rows + 1] = { button = "store", text = "Store", same_line = true, disabled = not (sel and sel.active) }
	rows[#rows + 1] = { button = "release", text = "Release", same_line = true, disabled = not sel,
		tooltip = "Let the beast go for good (click twice)" }
	if self.note then rows[#rows + 1] = { text = self.note } end
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
function W:select(b)
	self.selected = b and b.name or nil
	self.confirm = nil
	self.note = nil
end

function W:release(world, b)
	local now = self.env.now(world)
	local c = self.confirm
	if c and c.name == b.name and now - c.at <= CONFIRM_S then
		self.selected, self.confirm = nil, nil
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
	local sel = self:selection()
	if slot then
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
	end
	self:refresh(world, true)
end

return M
