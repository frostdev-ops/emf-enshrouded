-- The beastmaster/ui channel (mod messages, Lua 1.19): what Beastmaster on the server and Beastmaster on a joined
-- player's game send each other (remote.lua on the server, client.lua on the player's game). A message is a JSON object
-- whose `t` names its kind. Nothing goes to a game that does not have the channel at this version (wire.peer), so a
-- player without EMF, with Beastmaster off or with another version keeps the chat commands.
local M = {}

M.CHANNEL = "ui"
M.VERSION = 1
M.MAX = 16384  -- bytes: a whole Beasts window (16 beasts, a beast's talents) fits

local told = {}

-- A line in the log, once per `why` (a flood of bad messages writes one line).
function M.log_once(why)
	if told[why] then return end
	told[why] = true
	emf.log("beastmaster/ui: " .. why)
end

-- `msg` as the channel's text, or nil (logged once): not writable as JSON, or over M.MAX.
function M.encode(msg)
	if not emf.json then return nil end
	local text, err = emf.json.encode(msg)
	if not text then
		M.log_once("a message could not be written: " .. tostring(err))
		return nil
	end
	if #text > M.MAX then
		M.log_once(string.format("a %s message of %d bytes is over the channel's %d", tostring(msg.t), #text, M.MAX))
		return nil
	end
	return text
end

-- The message in `data`, or nil and why: not JSON, or not an object with a text `t`.
function M.decode(data)
	if type(data) ~= "string" or not emf.json then return nil, "not text" end
	local v = emf.json.decode(data)
	if type(v) ~= "table" then return nil, "not a JSON object" end
	if type(v.t) ~= "string" then return nil, "no kind (t)" end
	return v
end

-- To player `player`'s game (the server). True when the core took it.
function M.send_to(world, player, msg)
	if not (emf.net and emf.net.send_to) then return false end
	local text = M.encode(msg)
	if not text then return false end
	local ok, sent = pcall(emf.net.send_to, world, player, M.CHANNEL, text)
	return ok and sent == true
end

-- To the server (a joined player's game). True when the core took it.
function M.send(msg)
	if not (emf.net and emf.net.send) then return false end
	local text = M.encode(msg)
	if not text then return false end
	local ok, sent = pcall(emf.net.send, M.CHANNEL, text)
	return ok and sent == true
end

-- Whether player `player`'s game has the channel at this version (the server asks).
function M.peer(world, player)
	if not (emf.net and emf.net.peer) or not player or player == 0 then return false end
	local ok, v = pcall(emf.net.peer, world, player, M.CHANNEL)
	return ok and v == M.VERSION
end

-- Whether the server has the channel at this version (a joined player's game asks).
function M.server_peer()
	if not (emf.net and emf.net.peer) then return false end
	local ok, v = pcall(emf.net.peer, nil, 0, M.CHANNEL)
	return ok and v == M.VERSION
end

return M
