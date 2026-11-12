# Minimap (sample, `minimap`)

A small minimap at the top right of your screen: the game's own map around you (north up, as the game's map is), your
arrow turning with you, the places your map shows (altars, shrines, places found) as dots, and the markers mods put on
minimaps. It hides when the map, a menu or the console is open.

- **F10** shows or hides it; **Numpad +** and **Numpad -** zoom (0.5 to 8 metres per point; 2 at the start).
- Settings (Mods tab, EMF.exe): the corner, the size (120 to 400), round or square, and the keys.
- It runs on your own game (also when you join someone); nobody else needs it. Installed **switched off**.

## For mod makers

It is short on purpose: copy it. It uses EMF's canvas API (Lua 1.27, [lua.md](../../docs/modding/lua.md),
"Canvases"): `emf.canvas` places a panel, and every client step `emf.canvas_draw` hands EMF the panel's drawing as a
list (`{"map", x, z, metres_per_unit}` is the game's own map, `{"markers", ...}` the markers flagged `minimap`, then
circles, triangles and text). `emf.player_view` gives your position and heading, `emf.map_markers` the game's map
markers. A marker of your own mod shows on minimaps with `minimap = true` in `emf.marker`.
