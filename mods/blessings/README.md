# Flame Blessings: god mode, endless stamina and mana, free building (`blessings`)

Cheats as plain switches, for your own world, or for the host and the admins of a server. Each one is a "blessing" and
works per player, at once, with no restart:

- **God mode**: no damage, you cannot die. Your health also refills.
- **Endless health**: hits land, and your health refills right after each one. A very strong hit can still kill you
  in one go: use god mode for that.
- **Endless stamina** and **endless mana**: always full.
- **No fall damage**: jump from as high as you like.
- **Free building**: planned (a later version).

Each blessing shows on the host's screen ("Blessings: god mode, stamina", bottom left) and every player is told in
chat when the host or an admin changes one. **Nothing is saved:** the blessings only fill your bars and switch the
game's own "cannot be hit" flag, which the game never saves. Your character and your world stay as they are.

It runs where the world runs, so on the host or a dedicated server it works for every player, also those who joined
without EMF.

## Switch it on

Flame Blessings is installed **switched off**. Switch it on in the Mods tab (the key under Esc) or in EMF.exe, then
restart the game (or the server).

## Your own blessings (the host)

In the Mods tab under Flame Blessings: **Health** (off, endless health, god mode), **Endless stamina**, **Endless mana**
and **No fall damage**. They apply at once and stay on until you switch them off (they are settings).

Or hold **F4** for the blessings wheel: God mode, Endless health, Endless stamina, Endless mana, No fall damage (each
shows on or off), Fill me up (health, stamina and mana full once) and All off. Release F4 over an entry, or tap F4
and click. The Mods tab follows what you pick.

## The command (the host and admins)

Type it into the console (the key under Esc), `emf\commands.txt`, or the game's chat with a `/` in front.

| Command | What it does |
|---|---|
| `blessings` or `blessings help` | how to use it, and EMF's other cheats: `give`, `put`, `time`, `goto`, `stack` |
| `blessings list` | every player with blessings |
| `blessings god on` | god mode for you; also `refill`, `stamina`, `mana`, `nofall`, and `off` instead of `on` |
| `blessings stamina on #2` | for the 2nd player (`#k`: the k-th player, lowest id first, as `goto` counts) |
| `blessings god on Ivar` | for the player called Ivar (as the chat shows the name; when two share it, use `#k`) |
| `blessings heal [#k\|name]` | fills up health, stamina and mana once |
| `blessings off [#k\|name]` | every blessing off |

**Who may use it:** the console, `emf\commands.txt`, the host's own chat, and the accounts in `[chat] admins` of
`emf\emf_core.ini` (see EMF's [server.md](../../docs/modding/server.md)). Other players cannot bless themselves.

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
| Blessings wheel (`wheel_key`) | F4 | held to open the wheel (empty: no wheel) |
| Show your blessings on screen (`hud`) | on | the host's line |
| Corner (`corner`) | bottom left | top right, bottom right or bottom left |
| Tell every player in chat (`announce`) | on | "[Blessings] Ember: god mode on for Ivar" when the host or an admin changes a blessing (only when someone else is there to read it) |

Every setting applies at once. On a dedicated server there is no host: the Health, stamina, mana and fall settings do
nothing there; admins use the command.

## Good to know

- Switching the mod off in the Mods tab ends every blessing at once and removes the line on screen.
- In a game you host, guests see the chat lines with `[chat] players=1` in `emf\emf_core.ini`; on a dedicated server
  they always do.
- After a game update EMF may not know the game's attribute functions yet: then endless health, stamina and mana say
  "not available on this game version" (god mode and no fall damage still work) until EMF is updated.
