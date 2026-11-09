-- Beastmaster's screens for players who joined, the server's side of the beastmaster/ui channel (wire.lua): notes
-- instead of chat lines, and the Beasts window (one per player, built here as the host's is and drawn on their game).
-- The server decides everything; a player's game only shows what it is sent and sends back what was clicked, and every
-- message from it is checked here as the chat commands are checked.
-- M.new(env) where env has: now(world), log(text), present() (account -> player entity, the players in the world;
-- nil before the first check), loaded() (the world's data is here), make_window(screen) (a beasts_window drawn on
-- `screen`), stable_key(world, player) (a stable without the game's prompt is under that player's crosshair: their
-- stable key opens the window there), report(world, player, list) (the stables their game shows).
local wire = require("wire")

local M = {}

local R = {}
R.__index = R

-- What each kind of message from a player's game does: fn(self, world, player, account, msg).
local on = {}

function M.new(env)
	local r = setmetatable({ env = env, n = 0 }, R)  -- n: the last window's number (counts up, kept over resets)
	r:reset()
	return r
end

-- A new world: no player's screen.
function R:reset()
	self.windows = {}  -- account -> {w (the window), n (its number), player}
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
	if not w:show(world, player) then return false end
	self.windows[account] = { w = w, n = n, player = player }
	local b = name and w.env.beasts():find(account, name)
	if b then
		w:select(b)
		w:refresh(world, true)
	end
	return true
end

-- Once a second on the server world: a window whose player left (or came back as another entity) or that closed is
-- forgotten; the others are drawn again when what they show changed.
function R:step(world)
	local present = self.env.present() or {}
	for account, s in pairs(self.windows) do
		if present[account] ~= s.player or not s.w.open then
			self.windows[account] = nil
		else
			s.w:refresh(world)
		end
	end
end

-- A click in window `n` (only the window open now, on that player's own game, counts).
function on.click(self, world, player, account, msg)
	local s = self.windows[account]
	if not (s and s.n == msg.n and s.player == player and type(msg.key) == "string" and #msg.key <= 80) then return end
	s.w:on_event(world, { kind = "click", key = msg.key })
end

-- Window `n` was closed on the player's game (or could not open there).
function on.closed(self, world, player, account, msg)
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

-- The stables the player's game shows (the server has no render models).
function on.stables(self, world, player, account, msg)
	if type(msg.list) == "table" then self.env.report(world, player, msg.list) end
end

-- A message from a player's game (the core's handler, on the server world).
function R:on_message(world, from, data)
	local account = from and from.account or "?"
	local msg, why = wire.decode(data)
	if not msg then
		wire.log_once(string.format("a message from %s was dropped: %s", account, why))
		return
	end
	local player = from.player
	if not player or player == 0 or world:account(player) ~= account then
		wire.log_once(string.format("a message from %s was dropped: that player is not in the world", account))
		return
	end
	local fn = on[msg.t]
	if not fn then
		wire.log_once(string.format("a message from %s was dropped: an unknown kind (%s)", account, msg.t))
		return
	end
	fn(self, world, player, account, msg)
end

return M
