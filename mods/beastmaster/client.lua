-- Beastmaster on the game of a player who joined, the player's side of the beastmaster/ui channel (wire.lua): it shows
-- what the server's Beastmaster sends (notes, the Beasts window) and decides nothing itself: a click goes back to the
-- server, which acts and sends the window again. It also tells the server which stables its client world shows (the
-- server has no render models), and asks for the window when the stable key is pressed near one.
-- M.new(env) where env has: now(world), log(text).
local wire = require("wire")
local window_mod = require("beasts_window")
local stable_mod = require("stable")

local OPEN_NEAR_M = 6  -- m: the stable key asks the server for the window with a stable this close (the server checks)
local WHEEL_WAIT_S = 2  -- s: the wheel's items from the server open the wheel this long after the key was pressed
local NO_SERVER_WHEEL = "The server's Beastmaster does not answer (it needs 1.15 or newer): type /beastmaster in the chat."


local M = {}

local C = {}
C.__index = C

-- What each kind of message from the server does: fn(self, world, msg).
local on = {}

function M.new(env)
	-- portraits: species id -> picture handle (0: none); reporter: the client world's stables (stable.lua)
	local c = setmetatable({ env = env, portraits = {}, reporter = stable_mod.new_reporter() }, C)
	c:reset()
	return c
end

function C:reset()
	self.win_open = false  -- the server's Beasts window is open here
	self.win_n = nil       -- its number (the server's)
	self.since = nil       -- this game's time when the server's Beastmaster first answered in this session
	self.sent = nil        -- the stables last reported to the server in this session
	self.asked = nil       -- this game's time when the wheel key asked the server for the items
end

-- The wheel key (on the client world): the server is asked for this player's items.
function C:on_wheel_key(world)
	if not wire.server_peer() then
		emf.notify(NO_SERVER_WHEEL)
		return
	end
	if wire.send({ t = "wheel" }) then self.asked = self.env.now(world) end
end

-- Every step of the client world while this game is in someone else's world: the stables its client world shows go to
-- the server when they changed (the first list in a session in any case), once the server's Beastmaster answers and
-- this game has been in the world SETTLE_S (one still streaming in would report stables gone).
function C:step(world)
	local t = self.env.now(world)
	local up = wire.server_peer()
	if up then
		self.since = self.since or t
	else
		self.since, self.sent = nil, nil
	end
	local list = self.reporter:step(world, t, emf.local_player())
	if list and up and t - self.since >= stable_mod.SETTLE_S and not (self.sent and stable_mod.same_list(list, self.sent)) then
		if wire.send({ t = "stables", list = list }) then
			self.sent = list
			self.env.log(string.format("stables reported to the server: %d", #list))
		end
	end
end

-- The stable key (on the client world): the server is asked for the Beasts window when this game shows a stable within
-- OPEN_NEAR_M (the server checks the crosshair and that the stable has no prompt of its own).
function C:on_stable_key(world)
	if not wire.server_peer() then return end
	local me = emf.local_player()
	local px, py, pz
	if me and me ~= 0 then px, py, pz = world:position(me) end
	if not px then return end
	for _, e in ipairs(self.reporter.last or {}) do
		if math.sqrt((e.x - px) ^ 2 + (e.y - py) ^ 2 + (e.z - pz) ^ 2) <= OPEN_NEAR_M then
			wire.send({ t = "open" })
			return
		end
	end
end

-- This game's picture of species `id` (EMF drew it from this game at the start), or nil.
function C:picture(id)
	if type(id) ~= "string" or not id:match("^[%w_]+$") or not emf.image then return nil end
	local h = self.portraits[id]
	if h == nil then
		h = emf.image("portrait:beastmaster." .. id) or 0
		self.portraits[id] = h
	end
	return h ~= 0 and h or nil
end

-- The server's rows as this game draws them: a picture row's name becomes this game's picture (none: the row goes);
-- a picture handle the server named is never used.
function C:rows(list)
	local out = {}
	for _, r in ipairs(list) do
		if type(r) == "table" then
			local row = {}
			for k, v in pairs(r) do
				if type(k) == "string" and k ~= "portrait" and k ~= "image" and k ~= "icon" then row[k] = v end
			end
			if r.portrait ~= nil then
				row.image = self:picture(r.portrait)
				if row.image then out[#out + 1] = row end
			else
				out[#out + 1] = row
			end
		end
	end
	return out
end

-- The window's events (on the client world): a click and a close go to the server.
function C:on_window(world, ev)
	if ev.kind == "click" then
		if self.win_open then wire.send({ t = "click", n = self.win_n, key = ev.key }) end
	elseif ev.kind == "closed" then
		self.win_open = false
		if self.win_n then wire.send({ t = "closed", n = self.win_n }) end
	end
end

function on.note(self, world, msg)
	if type(msg.text) == "string" then emf.notify(msg.text) end
end

-- Window n from the server: drawn again when it is open here, opened when the server opens it (`open`); one that
-- cannot open here (another window or the wheel is open) is reported closed.
function on.win(self, world, msg)
	if type(msg.n) ~= "number" or type(msg.rows) ~= "table" then return end
	local def = {
		title = type(msg.title) == "string" and msg.title or "Beasts",
		width = type(msg.width) == "number" and msg.width or nil,
		place = type(msg.place) == "string" and msg.place or nil,
		close_key = emf.setting("stable_key") or "",
		world = "client",
		rows = self:rows(msg.rows),
	}
	if self.win_open then
		local ok, drawn = pcall(emf.window_update, window_mod.ID, def)
		if ok and drawn then
			self.win_n = msg.n
			return
		end
		self.win_open = false
	end
	if msg.open ~= true then return end
	local ok, opened = pcall(emf.window, window_mod.ID, def, function(w, ev) self:on_window(w, ev) end)
	if ok and opened then
		self.win_open, self.win_n = true, msg.n
	else
		if not ok then wire.log_once("the server's window could not be drawn: " .. tostring(opened)) end
		wire.send({ t = "closed", n = msg.n })
	end
end

-- The wheel's items: the wheel opens while the key that asked for them is still held (a late answer, or one that
-- came twice, opens none); a pick goes to the server with the label shown.
function on.wheel(self, world, msg)
	local asked = self.asked
	self.asked = nil
	if not asked or self.env.now(world) - asked > WHEEL_WAIT_S or type(msg.labels) ~= "table" then return end
	if not emf.key_down("wheel_key") then return end
	local labels = {}
	for i = 1, math.min(#msg.labels, 64) do
		if type(msg.labels[i]) ~= "string" then return end
		labels[i] = msg.labels[i]
	end
	if #labels == 0 then return end
	local seq = msg.seq
	emf.wheel("Beasts", labels, emf.setting("wheel_key"), function(w, choice)
		if choice and labels[choice] then wire.send({ t = "pick", seq = seq, i = choice, label = labels[choice] }) end
	end)
end

function on.close(self, world, msg)
	if self.win_open and msg.n == self.win_n then emf.window_close(window_mod.ID) end
end

-- A message from the server (the core's handler, on the client world).
function C:on_message(world, from, data)
	local msg = wire.decode(data)
	if not msg then return end
	local fn = on[msg.t]
	if fn then fn(self, world, msg) end
end

return M
