# Flame Blessings: god mode, endless stamina and mana, XP, levels and loot chests (`blessings`)

Cheats as plain switches, for your own world, or for the host and the admins of a server. Each one is a "blessing" and
works per player, at once, with no restart:

- **God mode**: your health can't drop, and no fall damage. Hits land but take nothing (your lowest possible health is
  held at your maximum), and your health also refills.
- **Endless health**: hits land, and your health refills right after each one. A very strong hit can still kill you
  in one go: use god mode for that.
- **Endless stamina** and **endless mana**: always full.
- **No fall damage**: jump from as high as you like.
- **XP and levels** (1.1): give XP, or take a player to a level, up or down, with an undo.
- **Loot chests** (1.1): one of the game's own loot chests beside you, with random loot of the tier you pick.
- **Free building**: planned (a later version).

Not covered yet by god mode: the shroud timer, drowning and the cold hurt you through other paths of the game; whether
god mode also holds against them is **untested**.

Each blessing shows on the host's screen ("Blessings: god mode, stamina", bottom left) and every player is told in
chat when the host or an admin changes one. **The blessings are not saved:** they only fill your bars, hold your lowest
health and reset the start of a fall, all in values the game never saves. **XP, levels and chests are:** XP and levels
are saved with the character like XP from play, and a chest stays in the world until it is emptied.

It runs where the world runs, so on the host or a dedicated server it works for every player, also those who joined
without EMF.

## Switch it on

Flame Blessings is installed **switched off**. Switch it on in the Mods tab (the key under Esc) or in EMF.exe, then
restart the game (or the server).

## Your own blessings (the host)

In the Mods tab under Flame Blessings: **Health** (off, endless health, god mode), **Endless stamina**, **Endless mana**
and **No fall damage**. They apply at once and stay on until you switch them off (they are settings).

Or hold **F11** for the blessings wheel: God mode, Endless health, Stamina, Mana, No fall damage (each shows on or
off), Fill me up (health, stamina and mana full once), Level up (one level, the game's own level-up), Loot chest (the
**Loot chest tier** setting's chest beside you) and, on the second page (turn it with the mouse wheel), All off.
Release F11 over an entry, or tap F11 and click. The Mods tab follows what you pick. (Not F4: Alt+F4 closes the game,
and the game uses Alt.)

## The command (the host and admins)

Type it into the console (the key under Esc), `emf\commands.txt`, or the game's chat with a `/` in front.

| Command | What it does |
|---|---|
| `blessings` or `blessings help` | how to use it, and EMF's other cheats: `give`, `put`, `time`, `goto`, `stack` |
| `blessings list` | every player as `#k name: blessings` (or `none`), so you can see each one's `#k` |
| `blessings god on` | god mode for you; also `refill`, `stamina`, `mana`, `nofall`, and `off` instead of `on` |
| `blessings stamina on #2` | for the 2nd player (`#k`: the k-th player, lowest id first, as `goto` counts) |
| `blessings god on Ivar` | for the player called Ivar (as the chat shows the name; when two share it, use `#k`) |
| `blessings heal [#k\|name]` | fills up health, stamina and mana once (it says so when there was nothing to fill) |
| `blessings off [#k\|name]` | every blessing off |
| `blessings xp 500 [#k\|name]` | gives 500 XP (1 to 100,000,000), the way the game's own XP comes in: the game counts it and levels up as usual. At the highest level it is refused (the game would throw it away) |
| `blessings level 10 [#k\|name]` | to level 10. Up: the XP the levels on the way need, so the game levels up one level per step, with its level-up messages and skill points. Down: at once, at the start of that level |
| `blessings level +2` / `level -1` | up two levels / down one |
| `blessings level undo [#k\|name]` | back to the level and XP from before Flame Blessings' last XP or level change for that player (kept until the world closes; once) |
| `blessings chest [1-6] [#k\|name]` | one of the game's own end-room loot chests, 2 m beside the player, with random loot of that tier (1 and 2 are the same chest; no tier: the **Loot chest tier** setting) |

**Levels and skills:** skill points follow the level (the game works them out). Lowering a level keeps the skills
already unlocked, but when the game loads the character next it checks the skill tree: if the skills cost more points
than the new level gives, the game resets the whole tree by itself (the points come back to spend again). Use
`blessings level undo` before leaving the world to avoid that.

**Who may use it:** the console, `emf\commands.txt`, the host's own chat, and the accounts in `[chat] admins` of
`emf\emf_core.ini` (see EMF's [server.md](../../docs/modding/server.md)). Other players cannot bless themselves. With
`[chat] everyone=1` every player counts as an admin, so then everyone can.

A blessing for the host changes the host's settings (the Mods tab shows it). A blessing for another player ends when
that player leaves or the world closes; a new player never gets someone else's. When two admins give one player both
endless health and god mode, god mode wins; `off` clears both.

## Settings (Mods tab, EMF.exe, or `set blessings <key> <value>`)

| Setting | Default | What it does |
|---|---|---|
| Health (`health`) | off | `off`, `refill` (endless health) or `god` (god mode), for the host |
| Endless stamina (`stamina`) | off | for the host |
| Endless mana (`mana`) | off | for the host |
| No fall damage (`no_fall_damage`) | off | for the host |
| Blessings wheel (`wheel_key`) | F11 | held to open the wheel (empty: no wheel) |
| Loot chest tier (`chest_tier`) | 3 | the wheel's Loot chest, and `blessings chest` without a tier: 1 (tier 1-2), 3, 4, 5 or 6 |
| Show your blessings on screen (`hud`) | on | the host's line |
| Corner (`corner`) | bottom left | top right, bottom right or bottom left |
| Tell every player in chat (`announce`) | on | "[Blessings] Ember: god mode on for Ivar" when the host (also in the Mods tab) or an admin changes a blessing (only when someone else is there to read it) |

Every setting applies at once. On a dedicated server there is no host: the Health, stamina, mana and fall settings do
nothing there; admins use the command.

## Good to know

- Switching the mod off in the Mods tab ends every blessing at once and removes the line on screen.
- The line on screen is your own world's: when you join someone else's game, it goes away after a moment.
- In a game you host, guests see the chat lines with `[chat] players=1` in `emf\emf_core.ini`; on a dedicated server
  they always do.
- After a game update EMF may not know the game's attribute functions yet: then god mode, endless health, stamina and
  mana say "not available on this game version" (no fall damage still works) until EMF is updated. XP and levels say
  the same when EMF does not find the game's XP curve.
