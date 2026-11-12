-- Minimap (sample): the game's own map around you, north up, in a panel EMF draws over the HUD (a "canvas").
-- Every client step it hands EMF the panel's drawing as a list: the map, the markers, the places found, the arrow.
-- Copy it: the parts are emf.player_view, emf.map_markers, emf.canvas and emf.canvas_draw (EMF's lua.md, "Canvases").

local shown = true
local zooms = {0.5, 1, 2, 4, 8}  -- metres per canvas unit
local zoom = 3                   -- 2 m per unit at the start
local placed = nil               -- the canvas spec last set (set again only when a setting changed)

emf.on_key("toggle_key", function() shown = not shown; placed = nil end, "client")
emf.on_key("zoom_in_key", function() zoom = math.max(1, zoom - 1) end, "client")
emf.on_key("zoom_out_key", function() zoom = math.min(#zooms, zoom + 1) end, "client")

-- The panel: where and how big (the settings), hidden when switched off with the key.
local function place()
  local size = emf.setting("size") or 220
  -- 64 units up from a bottom corner: above the game's clock
  local corner = emf.setting("corner") or "bottom_right"
  local spec = {corner = corner, x = 16, y = corner:find("bottom") and 64 or 16, w = size, h = size,
                shape = emf.setting("shape") or "rect", hidden = not shown}
  local key = spec.corner .. size .. spec.shape .. tostring(spec.hidden)
  if key ~= placed then
    emf.canvas("map", spec)
    placed = key
  end
  return size
end

emf.on_setting_changed(function() placed = nil end)

-- The player's arrow: a triangle pointing where they face (0 = north = up on the panel).
local function arrow(c, heading)
  local a = math.rad(heading)
  local fx, fy = math.sin(a), -math.cos(a)  -- forward on the panel (y grows downward)
  local sx, sy = -fy, fx                    -- to the right of it
  return {"tri", c + fx * 9, c + fy * 9, c - fx * 6 + sx * 6, c - fy * 6 + sy * 6, c - fx * 6 - sx * 6,
          c - fy * 6 - sy * 6, colour = 0xffe08aff}
end

emf.on_tick(function(world)
  if not world:is_client() then return end
  local view = emf.player_view()
  if not view then return end
  local size = place()
  if not shown then return end
  local r, m = size / 2, zooms[zoom]
  local round = emf.setting("shape") == "circle"
  local list = {
    round and {"circle", r, r, r, colour = 0x14181ce6} or {"rect", 0, 0, size, size, colour = 0x14181ce6},  -- background
    {"map", view.x, view.z, m},                 -- the game's own map, centred on the player
    {"markers", view.x, view.z, m},             -- markers mods flagged for minimaps
  }
  -- the places the game's map shows (altars, shrines, places found) as small dots
  for _, g in ipairs(emf.map_markers()) do
    if g.shown then
      local px, py = r + (g.x - view.x) / m, r - (g.z - view.z) / m
      local inside = round and (px - r) ^ 2 + (py - r) ^ 2 < (r - 4) ^ 2 or
                     (not round and px > 3 and py > 3 and px < size - 3 and py < size - 3)
      if inside then
        list[#list + 1] = {"circle", px, py, 3, colour = 0xf0f0f0d0}
      end
    end
  end
  if round then
    -- the game draws its map into the whole square: a wide dark ring paints the corners outside the circle
    list[#list + 1] = {"circle", r, r, r + r * 0.25, width = r * 0.5 + 2, colour = 0x0a0a0aff}
  end
  list[#list + 1] = arrow(r, view.heading)
  list[#list + 1] = round and {"circle", r, r, r - 1.5, width = 3, colour = 0x0a0a0aff} or
                    {"rect", 0, 0, size, size, width = 3, colour = 0x0a0a0aff}  -- the frame
  list[#list + 1] = {"text", r, 11, "N", size = 15, colour = 0xffffffff}
  emf.canvas_draw("map", list)
end)
