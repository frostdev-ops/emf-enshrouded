# Bounty Board (`bounty`, Lua)

Build the game's Notice Board in your base and it becomes a Bounty Board. It posts notices: slay creatures of a
faction ("Slay 6 Vukah"), gather a material ("Gather 60 Wood logs"), or slay a champion. Take one, finish it
anywhere, and claim its reward at the board. New notices come every 4 in-game days. Your first bounties are quick ones
("Gather 20 Wood logs", "Slay 2 Wildlife") until you finish one. Daily and repeatable quests (164 votes) and a kill
tracker (65) are open requests on the official board; no Nexus mod has them.

It runs where the world runs, so on the host or a dedicated server it works for every player, also those who joined
without EMF. The notices, each player's bounties and progress, and the marked props are kept with the world, by
account (`emf\config\bounty\<world slot>.txt`). Nothing is written into the game's save: without the mod a Bounty
Board is an ordinary Notice Board again.

## Switch it on

Bounty Board is installed **switched off**: it hands out items to every player. Switch it on in the Mods tab (the key
under Esc) or in EMF.exe, then restart the game (or the server).

## The board

- **A Notice Board in a base:** build a Notice Board (Decoration Workbench) within 40 m of a flame altar (**Board must
  be within (m) of a flame altar**). It becomes a Bounty Board within a minute. The notice boards in the world's
  villages are no Bounty Boards (unless a base is that close to them).
- **Any prop:** before you have a Notice Board, the host looks at a prop within 8 m (a sign, a post, a table) and
  types `bounty.mark`: it is a Bounty Board now. `bounty.unmark` near it takes the mark off. Marks are kept with the
  world by the prop's place; a marked prop that was taken down loses its mark.
- `bounty.boards` lists the world's Bounty Boards. If it does not list your Notice Board (on a dedicated server, for
  example), look at the board and type `bounty.mark`.

## Reading the board

- **The host:** look at the board within 4 m: a note says "Bounty Board: press E to read" (at most every 10 minutes). Press **E**: a wheel shows
  the notices (click one to take it), "Drop" for each bounty you hold, and "Claim" when one is done. E is also the
  game's key for using things: a chest standing right next to the board opens too. **Read the board (host)** changes
  the key.
- **Players who joined, and everyone on a dedicated server:** walking up to the board, facing it, sends you a private
  chat line with the notices ("1. Gather 20 Wood logs (3 runes) ... Type /bounty.take 1-3 here"), and again after you
  went 8 m away (at most once a minute). Take one with `/bounty.take <n>`; claim with `/bounty.claim` at the board. This needs players' chat:
  always on on a dedicated server, `[chat] players=1` in `emf\emf_core.ini` in a game you host (see
  `docs/modding/server.md`).
- `/bounty.board` lists the notices when you are within 8 m of a Bounty Board, and your bounties anywhere.

## Notices and your bounties

- The notices are the same for everyone (with **Notices** set to "Each player their own", everyone gets their own);
  each player keeps their own progress. A player can take each notice once until new notices come.
- You hold one bounty at a time (**Bounties a player can hold**, up to 4): finish it, or give it up with
  `/bounty.drop <n>`.
- A bounty you hold stays when new notices come, until you finish and claim it, or drop it.
- New notices come every 4 in-game days at dawn (**Every (in-game days)**, 1-10); the board says how long it is to
  them. The game keeps no day number, so the mod counts dawns while the world runs: a dawn counts when the clock passes
  6:00, also when the host sets the time from the evening to the morning or the players sleep through the night.
  **New notices** can also be every real day at an hour on the host's clock, every dawn, or every few hours of play.

## Take and claim anywhere, as in 1.1

Switch off **Take and claim at a Bounty Board** (`need_board`): in the Mods tab, in EMF.exe, or with
`set bounty need_board 0` in the console. Then `/bounty.board` shows the notices anywhere and `/bounty.take` and
`/bounty.claim` work anywhere. The boards still work as above.

## Commands

| Command | Who | What |
|---|---|---|
| `/bounty.board` | every player | the notices (at a board), your bounties and progress, rewards waiting, the time to new notices |
| `/bounty.take <n>` | every player | take notice n (at a board) |
| `/bounty.drop <n>` | every player | give up your bounty n |
| `/bounty.claim` | every player | the rewards of your finished bounties (at a board) |
| `/bounty.reroll <n>` | every player | with notices of your own: swap notice n for another one (1 per period by default) |
| `/bounty.top` | every player | who finished the most bounties in this world |
| `bounty.mark`, `bounty.unmark` | host, admins | the prop you look at becomes a Bounty Board / the marked prop nearest to you is none again |
| `bounty.boards` | host, admins | the world's Bounty Boards |
| `bounty.give <id>` | host, admins | hold a bounty from the pool at once (for tests) |
| `bounty.reset` | host, admins | new notices for everyone now |

The commands for every player work from the chat on a dedicated server, and in a game you host with
`[chat] players=1` in `emf\emf_core.ini` (see `docs/modding/server.md`). The host also sees their bounties on screen
("Bounty: Vukah 2/6"). (Lua mods put their id in front of every command, so the board is `/bounty.board` rather than
a bare `/bounty`.)

## What counts

Only bounties you hold count.

- **Slay:** a kill of a creature of the bounty's faction (read from the creature when first hit). With **Shared
  kills** (on) it counts for every player who hit it in its last 10 seconds; off, only for the last player who hit it.
  **Your companion and your summons count as you**: a hit by the companion, a Beastmaster beast or a Necromancer undead
  that follows you (an ally that guards you, through the game's guard system) is your hit. An ally told to stay and
  guard a spot does not follow you then, so its kills count for nobody until it follows you again. A creature killed
  without a hit (a test command) counts for nobody.
- **Gather:** raw materials a player picks up in the world (from a tree, a plant, a rock, a loose drop, a creature's
  corpse, or the glowing loot spot a kill leaves, also when Loot Magnet takes it for you) whose name holds the bounty's
  word. Never items taken out of a chest, a crafting station or a death bag, nor
  out of a tombstone, nor a bounty's own reward, nor items coming back from a chest a mod was filling (quick stack,
  sorting chests). Loose drops a mod made do not count either: a reward this mod dropped at a full player's feet
  (picked up within 30 minutes of play), and a loose drop of an item that a chest within 30 m got from a mod in the
  last 2 minutes (what did not fit there: quick stack and sorting chests drop it beside the chest or the player). A
  drop picked up later than that, or farther from such a chest, counts. Another mod that puts items straight into a
  player counts like gathering: a reward, a bonus, and a Grave Keeper claim of a fresh tombstone (its stacks are put
  into you). EMF cannot tell such a put from a pickup in the world (that would need the core to mark its own puts).
- **Champion:** a kill (as above) of a strong creature: at least 2 levels above the player and at least 1,000 health
  when first hit (the Wandering Champion and the bosses with a boss bar are). A heuristic: EMF cannot name a creature's
  template from Lua.

## Rewards

Rewards wait for `/bounty.claim` at a Bounty Board (or come at once, wherever you are, with **Rewards come at once**).
They go straight into your inventory; what does not fit is dropped at your feet, never lost. A reward that could not
even be dropped (you left the game before it landed) is kept for you: `/bounty.board` says "1 reward waiting" and your
next `/bounty.claim` brings it. Gear comes at your level with **Gear rewards' rarity** (uncommon by default).
**Reward amounts** scales the counts. A player who joined gets a chat line when a bounty is done; the host gets a note
on screen.

## Your first bounties

Until you finish your first bounty, the board shows you quick **starter** notices (and your rerolls draw starters):
gather 20 wood logs, 20 stone or 15 plant fibre, slay 2 wildlife, 3 shroud creatures or 2 scavengers, for a small
reward. Once you have finished one, you see the board's other notices.

To try the bounties quickly, set **How many to slay or gather** to **Tiny** (x0.2: "Slay 1 Vukah", "Gather 12 Wood
logs"); the size counts for bounties taken from then on.

## Updating from 1.1

The bounties you had become bounties you hold, with their progress (also more than the hold limit); finished ones wait
for `/bounty.claim`, rewards waiting stay, and the count of finished bounties (`/bounty.top`) stays. Nothing earned is
lost. Bounties already claimed are not kept.

## The pool

The bounties are the `POOL` table at the top of `emf\mods\bounty\main.lua`: kind, faction or item word, how many,
reward items (with an optional `name`, the word the notices show), how often it is drawn, and `starter = true` for the
quick first ones. Change it while the game runs and save: the mod reloads. **How many to slay or gather** scales the
counts (tiny x0.2, easy x0.6, hard x1.5). A pool without starters shows everyone the same kind of notices; a pool with
fewer starters than **Notices on the board** fills the rest of a starter board from its other bounties.

Installing EMF again replaces `main.lua`, and your changes to `POOL` with it. To keep your own bounties, put them in
`emf\config\bounty\pool.lua` instead: that folder stays when EMF is installed again or removed. The file returns a
table like `POOL`:

```lua
return {
  {id = "gather_flint", kind = "gather", match = "flint", name = "Flint", n = 30,
   reward = {{item = "Material_T1_Token_WeaponUpgrade_Rune01", count = 5, name = "runes"}}},
  {id = "kill_vukah", kind = "kill", faction = 3, name = "Vukah", n = 10, weight = 5,
   reward = {{item = "Material_T1_Token_WeaponUpgrade_Rune01", count = 10}}},
}
```

When it is there, it is the whole pool: `main.lua`'s `POOL` is not used. It is read when the mod starts and at each
world load. An entry that does not fit (no id, an unknown kind, no `n`, a kill without `faction`, a gather without
`match`) is skipped and named in the log; a file that does not run, or holds no entry that fits, leaves `main.lua`'s
pool in use, and the log says so. The file can only build the table: it cannot reach the game, files or other
programs.

## Settings (Mods tab, EMF.exe, or `set bounty <key> <value>`)

| Setting | Default | What it does |
|---|---|---|
| Take and claim at a Bounty Board (`need_board`) | on | off: the commands work anywhere, as in 1.1 |
| Every Notice Board in a base is a Bounty Board (`notice_boards`) | on | off: only marked props |
| Board must be within (m) of a flame altar (`base_radius`) | 40 | 10-100 |
| Read the board (host) (`read_key`) | E | |
| Reading distance (m) (`read_range`) | 4 | 2-8 |
| Tell players the notices when they walk up (`chat_notices`) | on | |
| Notices on the board (`bounties_per_period`) | 3 | 2-4 |
| Notices (`offers_for`) | the same for everyone | `everyone`, `each_player` |
| Bounties a player can hold (`hold_max`) | 1 | 1-4 |
| How many to slay or gather (`difficulty`) | normal | tiny (to try it out), easy, normal, hard |
| Shared kills (`share_kills`) | on | |
| New notices (`reset`) | every few days in the game | `game_days`, `real_day`, `game_dawn`, `hours` |
| Every (in-game days) (`reset_days`) | 4 | 1-10 |
| At (hour) (`reset_hour`) | 6 | 0-23, the host's clock |
| Every (hours of play) (`reset_hours`) | 4 | 1-24 |
| Rerolls per period (`rerolls`) | 1 | 0-3, with notices of your own |
| Rewards come at once (`auto_claim`) | off | |
| Reward amounts (x) (`reward_scale`) | 1 | 0.5-3 |
| Gear rewards' rarity (`reward_rarity`) | uncommon | |
| Show your bounties (`hud`), Corner, Colour | on, bottom left | the host's own line |

## Good to know

- The chat lines go out at most 30 in 10 seconds (EMF's limit for a Lua mod): when many players walk up at once, some
  get theirs a few seconds later.
- The log (`emf\emf_core.log`, lines `[bounty]`) names every bounty finished, every reward and every marked prop.
