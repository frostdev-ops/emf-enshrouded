-- Beastmaster on the game of a player who joined, the player's side of the beastmaster/ui channel (wire.lua): it shows
-- what the server's Beastmaster sends (notes, the Beasts window) and decides nothing itself: a click goes back to the
-- server, which acts and sends the window again. It also tells the server which stables its client world shows (the
-- server has no render models), and asks for the window when the stable key is pressed near one.
-- M.new(env) where env has: now(world), log(text).
local wire = require("wire")
local window_mod = require("beasts_window")
local stable_mod = require("stable")
local beast_line = require("beast_line")

local OPEN_NEAR_M = 6  -- m: the stable key asks the server for the window with a stable this close (the server checks)
local WHEEL_WAIT_S = 2  -- s: the wheel's items from the server open the wheel this long after the key was pressed
local OPEN_WAIT_S = 5   -- s: a window the server opens shows this long after a press of E or the stable key
local OPEN_AT_STABLE_M = 10  -- m: or while this game's player is this close to a stable it shows (the game's prompt,
                             -- by any key or a controller) ...
local REOPEN_S = 3           -- s: ... but not this soon after a window closed here
local NOT_SHOWN = "The server's Beasts window was not shown here: open it at a stable (E, or your stable key)."
local JUMP_M = 100      -- m: moved this far between two searches (fast travel): the world streams in again, wait again
local NO_SERVER_WHEEL = "The server's Beastmaster does not answer (it needs 1.15 or newer): type /beastmaster in the chat."
local LINE_STATES = { out = true, aside = true, rest = true }


local M = {}

local C = {}
C.__index = C

-- What each kind of message from the server does: fn(self, world, msg).
local on = {}

function M.new(env)
	-- portraits: species id -> picture handle (0: none); reporter: the client world's stables (stable.lua); opening:
	-- counts the windows opened here (a late event of an earlier one is not the open one's); free_open: the server may
	-- still open one window without a key press (once per game: beastmaster.as open from the console, for tests)
	local c = setmetatable({ env = env, portraits = {}, reporter = stable_mod.new_reporter(), opening = 0,
		free_open = true }, C)
	c:reset()
	return c
end

function C:reset()
	self.win_open = false  -- the server's Beasts window is open here
	self.win_n = nil       -- its number (the server's)
	self.since = nil       -- this game's time when the server's Beastmaster first answered in this session
	self.sent = nil        -- the stables last reported to the server in this session
	self.asked = nil       -- this game's time when the wheel key asked the server for the items
	self.line = nil        -- the server's beast line: {head, parts, at (this game's time when it came)}
	self.drawn = nil       -- the line's text on screen now
	self.pressed_at = nil  -- this game's time of the last press of E or the stable key (one window per press)
	self.closed_at = nil   -- this game's time when a window closed here last
	self.win_def = nil     -- the open window as it came, when it has countdowns (counted down here)
	self.win_at = nil      -- this game's time when it came
	self.win_el = nil      -- the whole seconds counted down on screen now
end

-- E or the stable key on the client world: the server may open a window here for a moment.
function C:on_open_key(world)
	self.pressed_at = self.env.now(world)
end

-- Whether this game's player is within `range` m of a stable this game's last search found.
function C:near_stable(world, range)
	local me = emf.local_player()
	local px, py, pz
	if me and me ~= 0 then px, py, pz = world:position(me) end
	if not px then return false end
	for _, e in ipairs(self.reporter.last or {}) do
		if math.sqrt((e.x - px) ^ 2 + (e.y - py) ^ 2 + (e.z - pz) ^ 2) <= range then return true end
	end
	return false
end

-- Whether a window the server opens may show now: within OPEN_WAIT_S after a press of E or the stable key; at a stable
-- this game shows (the game's prompt, whatever key or controller opened it), not within REOPEN_S of a close here; or
-- the one free opening. A server cannot open it again and again (it takes the mouse and keyboard).
function C:may_open(world)
	local t = self.env.now(world)
	local at = self.pressed_at
	if at and t - at >= 0 and t - at <= OPEN_WAIT_S then return true end
	local closed = self.closed_at
	local just_closed = closed and t - closed >= 0 and t - closed < REOPEN_S
	if not just_closed and self:near_stable(world, OPEN_AT_STABLE_M) then return true end
	if self.free_open then
		self.free_open = false
		return true
	end
	return false
end

-- The window open here goes (closed, or it could not be drawn again): closed on screen, the server hears it.
function C:drop_window(world)
	local n = self.win_n
	self.win_open, self.win_n, self.win_def = false, nil, nil
	self.closed_at = self.env.now(world)
	self.opening = self.opening + 1  -- its late "closed" is no later window's
	pcall(emf.window_close, window_mod.ID)
	if n then wire.send({ t = "closed", n = n }) end
end

-- Keeps window `def` (as drawn) to count its rests down here, when it has any.
function C:count_from(world, def)
	self.win_def = nil
	for _, r in ipairs(def.rows) do
		if type(r.text) == "string" and window_mod.has_countdown(r.text) then
			self.win_def, self.win_at, self.win_el = def, self.env.now(world), 0
			return
		end
	end
end

-- The open window's countdowns, drawn again when a whole second went.
function C:count_window(world, t)
	local def = self.win_def
	if not (self.win_open and def) then return end
	local el = math.floor(t - self.win_at)
	if el == self.win_el or el < 0 then return end
	self.win_el = el
	local rows = {}
	for i, r in ipairs(def.rows) do
		if type(r.text) == "string" and window_mod.has_countdown(r.text) then
			local row = {}
			for k, v in pairs(r) do row[k] = v end
			row.text = window_mod.count_down(r.text, el)
			rows[i] = row
		else
			rows[i] = r
		end
	end
	local shown = {}
	for k, v in pairs(def) do shown[k] = v end
	shown.rows = rows
	local ok, drawn = pcall(emf.window_update, window_mod.ID, shown)
	if not (ok and drawn) then self:drop_window(world) end
end

-- The wheel key (on the client world): the server is asked for this player's items (not again while its answer is on
-- the way: a pick must be from the wheel it answers last).
function C:on_wheel_key(world)
	if not wire.server_peer() then
		emf.notify(NO_SERVER_WHEEL)
		return
	end
	local t = self.env.now(world)
	if self.asked and t - self.asked >= 0 and t - self.asked <= WHEEL_WAIT_S then return end
	if wire.send({ t = "wheel" }) then self.asked = t end
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
		self.line = nil          -- another server, or none: its line goes
		self.reporter:reset()    -- and a search of the next one's world at once (its clock may be behind)
		self.searched_at = nil
	end
	-- the beast line, its rests counted down by this game's clock (drawn when its text changes)
	local text = self.line and beast_line.text(self.line.head, self.line.parts, t - self.line.at) or ""
	if text ~= (self.drawn or "") then
		emf.hud("out", text, "bottom_left")
		self.drawn = text
	end
	self:count_window(world, t)
	local me = emf.local_player()
	local list = up and self.reporter:step(world, t, me) or nil  -- no search while the server's Beastmaster is silent
	if list and up then  -- a jump (fast travel): the stables there may still be streaming in
		local x, y, z
		if me and me ~= 0 then x, y, z = world:position(me) end
		local at = self.searched_at
		if x and at and math.sqrt((x - at.x) ^ 2 + (y - at.y) ^ 2 + (z - at.z) ^ 2) >= JUMP_M then self.since = t end
		self.searched_at = x and { x = x, y = y, z = z } or nil
	end
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
	self:on_open_key(world)
	if not wire.server_peer() then return end
	if self:near_stable(world, OPEN_NEAR_M) then wire.send({ t = "open" }) end
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
		local n = self.win_n
		self.win_open, self.win_n, self.win_def = false, nil, nil
		self.closed_at = self.env.now(world)
		if n then wire.send({ t = "closed", n = n }) end
	end
end

-- Each handler answers "bad" when the message's fields are not what its kind has (logged once per kind).
function on.note(self, world, msg)
	if type(msg.text) ~= "string" then return "bad" end
	emf.notify(msg.text)
end

-- Window n from the server: drawn again when it is open here (one that cannot be drawn again goes), opened when the
-- server opens it (`open`) and this game asked for it (may_open); one that cannot open here (another window or the
-- wheel is open) or was not asked for is reported closed.
function on.win(self, world, msg)
	if type(msg.n) ~= "number" or type(msg.rows) ~= "table" then return "bad" end
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
			self:count_from(world, def)
			return
		end
		self:drop_window(world)
	end
	if msg.open ~= true then return end
	if not self:may_open(world) then
		wire.log_once("unasked window", "a window the server opened without a key press here was not shown")
		if not self.told_not_shown then
			self.told_not_shown = true
			emf.notify(NOT_SHOWN)
		end
		wire.send({ t = "closed", n = msg.n })
		return
	end
	self.pressed_at = nil  -- one press, one window
	self.opening = self.opening + 1
	local opening = self.opening
	local ok, opened = pcall(emf.window, window_mod.ID, def, function(w, ev)
		if opening == self.opening then self:on_window(w, ev) end
	end)
	if ok and opened then
		self.win_open, self.win_n = true, msg.n
		self:count_from(world, def)
	else
		if not ok then wire.log_once("the server's window could not be drawn: " .. tostring(opened)) end
		wire.send({ t = "closed", n = msg.n })
	end
end

-- The wheel's items: the wheel opens while the key that asked for them is still held (a late answer, or one that
-- came twice, opens none); a pick goes to the server with the label shown.
function on.wheel(self, world, msg)
	if type(msg.labels) ~= "table" then return "bad" end
	local labels = {}
	for i = 1, math.min(#msg.labels, 64) do
		if type(msg.labels[i]) ~= "string" then return "bad" end
		labels[i] = msg.labels[i]
	end
	local asked = self.asked
	self.asked = nil
	if not asked or self.env.now(world) - asked > WHEEL_WAIT_S then return end
	if not emf.key_down("wheel_key") then return end
	if #labels == 0 then return end
	local seq = msg.seq
	emf.wheel("Beasts", labels, emf.setting("wheel_key"), function(w, choice)
		if choice and labels[choice] then wire.send({ t = "pick", seq = seq, i = choice, label = labels[choice] }) end
	end)
end

-- The beast line (checked: a head, at most 16 parts of a name and a state, a rest of 0..3600 s).
function on.line(self, world, msg)
	if type(msg.head) ~= "string" or #msg.head > 64 or type(msg.parts) ~= "table" then return "bad" end
	local parts = {}
	for i = 1, math.min(#msg.parts, 16) do
		local p = msg.parts[i]
		if type(p) ~= "table" or type(p.name) ~= "string" or #p.name > 40 or not LINE_STATES[p.state] then return "bad" end
		local rest
		if p.state == "rest" then
			if type(p.rest) ~= "number" or p.rest ~= p.rest or p.rest < 0 or p.rest > 3600 then return "bad" end
			rest = p.rest
		end
		parts[i] = { name = p.name, state = p.state, rest = rest }
	end
	self.line = { head = msg.head, parts = parts, at = self.env.now(world) }
end

-- The server closes window n (its update did not go there).
function on.close(self, world, msg)
	if type(msg.n) ~= "number" then return "bad" end
	if not (self.win_open and msg.n == self.win_n) then return end
	self.win_open, self.win_n, self.win_def = false, nil, nil
	self.closed_at = self.env.now(world)
	self.opening = self.opening + 1  -- its late "closed" is no later window's
	pcall(emf.window_close, window_mod.ID)
end

-- A message from the server (the core's handler, on the client world).
function C:on_message(world, from, data)
	local msg, why = wire.decode(data)
	if not msg then
		wire.log_once("dropped (server): " .. why, "a message from the server was dropped: " .. why)
		return
	end
	local fn = on[msg.t]
	if fn and fn(self, world, msg) == "bad" then
		wire.log_once("dropped (server): bad fields in " .. msg.t,
			string.format("a message from the server was dropped: bad fields in a %s message", msg.t))
	end
end

return M
