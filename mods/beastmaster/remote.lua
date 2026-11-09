-- Beastmaster's screens for players who joined, the server's side of the beastmaster/ui channel (wire.lua): notes
-- instead of chat lines, and the Beasts window (one per player, built here as the host's is and drawn on their game).
-- The server decides everything; a player's game only shows what it is sent and sends back what was clicked, and every
-- message from it is checked here as the chat commands are checked.
-- M.new(env) where env has: now(world), log(text), present() (account -> player entity, the players in the world;
-- nil before the first check), loaded() (the world's data is here), make_window(screen) (a beasts_window drawn on
-- `screen`), stable_key(world, player) (a stable without the game's prompt is under that player's crosshair: their
-- stable key opens the window there), report(world, player, list) (the stables their game shows), wheel_items(world,
-- player) -> labels, entries (that player's wheel), run(world, player, account, entry) -> text (a wheel pick done),
-- tell(world, player, text), line(world, account) -> head, parts (beast_line.lua).
local wire = require("wire")
local beast_line = require("beast_line")

local M = {}

local R = {}
R.__index = R

-- What each kind of message from a player's game does: fn(self, world, player, account, msg).
local on = {}

local REPORT_EVERY_S = 2  -- s: a player's stables reports are taken at most this often (a game sends one on a change)
local CLOSE_TRIES = 10    -- a close that does not go is tried again once a second this often

function M.new(env)
	-- n, seq: the last window's and wheel's numbers (they count up, kept over resets); closing: account -> {player, n,
	-- tries}, a window to close on that player's game (its update did not go); pending: account -> {player, list}, the
	-- last stables report that came before the world's data (taken once it is here)
	local r = setmetatable({ env = env, n = 0, seq = 0, closing = {}, pending = {} }, R)
	r:reset()
	return r
end

-- A new world: no player's screen.
function R:reset()
	self.windows = {}  -- account -> {w (the window), n (its number), player}
	self.wheels = {}   -- account -> {seq, labels, entries, player}: the wheel sent last, until a pick from it
	self.lines = {}    -- account -> the beast line sent last (beast_line.snapshot)
	self.reported_at = {}  -- account -> when their last stables report was taken (one every REPORT_EVERY_S)
end

-- Whether `player` gets Beastmaster's screens on their own game: a player who joined (not this game's own) whose game
-- has the channel at this version.
function R:is_remote(world, player)
	if not player or player == 0 or player == emf.local_player() then return false end
	return wire.peer(world, player)
end

-- A note on `player`'s screen; false when their game does not take it (they get a chat line instead).
function R:note(world, player, text)
	return self:is_remote(world, player) and wire.send_to(world, player, { t = "note", text = text })
end

-- The screen of window `n` on `player`'s game: messages. Its pictures go by name (beastmaster.<species id>): each game
-- draws its own (a picture handle is this game's only).
local function screen(player, n)
	local s = {}
	local function win(def, open)
		return { t = "win", n = n, open = open, title = def.title, width = def.width, place = def.place, rows = def.rows }
	end
	function s.open(world, def)
		return wire.send_to(world, player, win(def, true))
	end
	function s.update(world, def)
		return wire.send_to(world, player, win(def, nil))
	end
	function s.picture(sp)
		return sp and { portrait = sp.id } or nil
	end
	s.counts_down = true  -- the player's game counts the rests down (a change of them alone sends nothing)
	return s
end

-- Opens the Beasts window on `player`'s game (a new one: one opened before is replaced there), on beast `name`'s tab
-- when given. False when the world's data is not here, the player has no account, or the message did not go.
function R:open_window(world, player, name)
	if not self.env.loaded() then return false end
	local account = world:account(player)
	if not account then return false end
	self.n = self.n + 1
	local n = self.n
	local w = self.env.make_window(screen(player, n))
	if not w:show(world, player, name) then return false end
	self.windows[account] = { w = w, n = n, player = player }
	self.closing[account] = nil  -- the new one replaces it there
	return true
end

-- Once a second on the server world: a window whose player left (or came back as another entity) or that closed is
-- forgotten (one whose update did not go is closed on that player's game); the others are drawn again when what they
-- show changed. A stables report that came before the world's data is taken now.
function R:step(world)
	local known = self.env.present()
	local present = known or {}
	for account, s in pairs(self.windows) do
		if present[account] ~= s.player then
			self.windows[account] = nil
		elseif not s.w.open then
			self.windows[account] = nil
			self.closing[account] = { player = s.player, n = s.n, tries = 0 }
		else
			s.w:refresh(world)
		end
	end
	for account, c in pairs(self.closing) do
		if present[account] ~= c.player or wire.send_to(world, c.player, { t = "close", n = c.n }) then
			self.closing[account] = nil
		else
			c.tries = c.tries + 1
			if c.tries >= CLOSE_TRIES then self.closing[account] = nil end
		end
	end
	if known and self.env.loaded() then
		for account, p in pairs(self.pending) do
			self.pending[account] = nil
			if present[account] == p.player then
				self.reported_at[account] = self.env.now(world)
				self.env.report(world, p.player, p.list)
			end
		end
	end
	for account, s in pairs(self.wheels) do
		if present[account] ~= s.player then self.wheels[account] = nil end
	end
	-- each joined player's beast line, when it changed (the first only when it shows something)
	local now = self.env.now(world)
	for account, player in pairs(present) do
		if self:is_remote(world, player) then
			local head, parts = self.env.line(world, account)
			local sent = self.lines[account]
			if (sent or #parts > 0) and beast_line.changed(sent, head, parts, now) then
				if wire.send_to(world, player, { t = "line", head = head, parts = parts }) then
					self.lines[account] = beast_line.snapshot(head, parts, now)
				end
			end
		else
			self.lines[account] = nil  -- not (or no longer) a peer: a new hello gets the line again
		end
	end
	for account in pairs(self.lines) do
		if not present[account] then self.lines[account] = nil end
	end
end

-- A click in window `n` (only the window open now, on that player's own game, counts). Each handler answers "bad"
-- when the message's fields are not what its kind has (logged once per kind).
function on.click(self, world, player, account, msg)
	if type(msg.n) ~= "number" or type(msg.key) ~= "string" or #msg.key > 80 then return "bad" end
	local s = self.windows[account]
	if not (s and s.n == msg.n and s.player == player) then return end
	s.w:on_event(world, { kind = "click", key = msg.key })
end

-- Window `n` was closed on the player's game (or could not open there).
function on.closed(self, world, player, account, msg)
	if type(msg.n) ~= "number" then return "bad" end
	local s = self.windows[account]
	if not (s and s.n == msg.n) then return end
	s.w:on_event(world, { kind = "closed" })
	self.windows[account] = nil
end

-- The stable key on the player's game near a stable it shows: the window, when the server sees them at one without
-- the game's prompt (with the prompt, the prompt opens it).
function on.open(self, world, player, account, msg)
	if self.env.stable_key(world, player) then self:open_window(world, player) end
end

-- The wheel key on the player's game: their wheel's items (as the host's are made, for their account).
function on.wheel(self, world, player, account, msg)
	local labels, entries = self.env.wheel_items(world, player)
	self.seq = self.seq + 1
	self.wheels[account] = { seq = self.seq, labels = labels, entries = entries, player = player }
	wire.send_to(world, player, { t = "wheel", seq = self.seq, labels = labels })
end

-- A pick on that wheel: item i, as it was shown (the label must be the one sent there); done once.
function on.pick(self, world, player, account, msg)
	local i = msg.i
	if type(msg.seq) ~= "number" or type(i) ~= "number" or i ~= math.floor(i) or type(msg.label) ~= "string" then
		return "bad"
	end
	local s = self.wheels[account]
	if not (s and s.player == player and msg.seq == s.seq) then return end
	if s.labels[i] == nil or s.labels[i] ~= msg.label then return end
	self.wheels[account] = nil
	local text = self.env.run(world, player, account, s.entries[i])
	if text then self.env.tell(world, player, text) end
end

-- The stables the player's game shows (the server has no render models). One before the world's data waits for it.
function on.stables(self, world, player, account, msg)
	if type(msg.list) ~= "table" then return "bad" end
	if not self.env.loaded() then
		self.pending[account] = { player = player, list = msg.list }
		return
	end
	self.pending[account] = nil  -- this one is newer
	local t, last = self.env.now(world), self.reported_at[account]
	if last and t >= last and t - last < REPORT_EVERY_S then return end
	self.reported_at[account] = t
	self.env.report(world, player, msg.list)
end

-- A message from a player's game (the core's handler, on the server world).
function R:on_message(world, from, data)
	local account = from and from.account or "?"
	local msg, why = wire.decode(data)
	if not msg then
		wire.log_once("dropped: " .. why, string.format("a message from %s was dropped: %s", account, why))
		return
	end
	local player = from.player
	if not player or player == 0 or world:account(player) ~= account then
		wire.log_once("dropped: not in the world",
			string.format("a message from %s was dropped: that player is not in the world", account))
		return
	end
	local fn = on[msg.t]
	if not fn then
		wire.log_once("dropped: an unknown kind", string.format("a message from %s was dropped: an unknown kind (%s)",
			account, msg.t:sub(1, 16):gsub("[^%w_]", "?")))
		return
	end
	if fn(self, world, player, account, msg) == "bad" then
		wire.log_once("dropped: bad fields in " .. msg.t,
			string.format("a message from %s was dropped: bad fields in a %s message", account, msg.t))
	end
end

return M
