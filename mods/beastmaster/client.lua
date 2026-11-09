-- Beastmaster on the game of a player who joined, the player's side of the beastmaster/ui channel (wire.lua): it shows
-- what the server's Beastmaster sends (notes, the Beasts window) and decides nothing itself: a click goes back to the
-- server, which acts and sends the window again.
-- M.new(env) where env has: now(world), log(text).
local wire = require("wire")
local window_mod = require("beasts_window")

local M = {}

local C = {}
C.__index = C

-- What each kind of message from the server does: fn(self, world, msg).
local on = {}

function M.new(env)
	local c = setmetatable({ env = env, portraits = {} }, C)  -- portraits: species id -> picture handle (0: none)
	c:reset()
	return c
end

function C:reset()
	self.win_open = false  -- the server's Beasts window is open here
	self.win_n = nil       -- its number (the server's)
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
