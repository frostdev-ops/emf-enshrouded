# Beastmaster (`beastmaster`, a Lua mod)

A Beastmaster class for Enshrouded. Tame wild wolves, boars, sabretooths, hounds, bears, aurochs and snakes, keep up to 8 in
your collection, and call as many to fight beside you as you have active slots (1 at the start, up to 4 as your character
levels up). Build a stable to choose which beasts are active. They follow you, come back when they stray, rest after
they fall, and are kept with the world. Their kills are yours: XP and loot.

It needs the `lua` mod 1.13 or newer (in the EMF zip). It is installed **switched off**: switch it on in the Mods tab
(the key under Esc) or in EMF.exe, then restart the game. The bear, the aurochs and the snake come in the **EMF creature packs**
download (the **bear pack** and the **beasts pack**, below): install them with EMF.exe ("Install a downloaded file")
and EMF builds them when the game starts (no EML needed; with EML or ShroudForge installed, the card on EMF.exe's EMF
page says who applies them). Without them everything else works and those beasts are simply not there.

## How it goes

1. **Tame a beast**, in one of two ways:
   - **By defeat:** bring it under **25 %** health and choose **Tame** (wheel) or type `/beastmaster tame` with it
     within 8 m: the beast under your crosshair, else the nearest beaten-down one (in a fight it is often at your side).
     It calms down for those 3 s: it stops fighting and is held where it is (one that still runs off is put back),
     and your beasts leave it alone. Stay within 8 m. If it dies or you walk away, taming stops and it turns wild
     again. A beast someone else is taming or feeding is theirs to finish.
   - **By bait:** choose **Feed** 3 times within 10 s (the beast you look at, else the nearest within 8 m). Each feed
     takes one bait from your bag, one that suits the beast and when you meet it: raw lean meat (goats, hophares and
     rats drop it) for wolves, boars and snakes; the game's animal baits from the Farmer for the rest: dog bait for
     hounds, cat bait for sabretooths, goat bait for aurochs; red berries for bears; for the rare snakes game meat
     (crimson), turkey bait (golden king) and yak bait (albino).
2. A tamed beast becomes your ally, guards you, and joins your collection with a name: "Wolf", "Wolf 2", "Boar"... For its first
   5 s it cannot fall (a swing it began as a wild beast can land on itself).
3. **Call a beast** from your collection: **Call <name>** on the wheel, or `/beastmaster call <name or number>`. It costs
   15 % of your maximum mana, with 10 s between calls. As many are out at once as you have **active slots** (see The
   stable), of any kinds: with two slots, two wolves.
4. **Fight together:** they defend you on their own. **Attack** (wheel) or `/beastmaster attack` sends every beast that is
   out at the creature you look at, up to 40 m away; when you look at nothing, at the creature that hit you in the last
   10 s. When a beast's hit leaves a foe at 25 % health or less, you land the killing blow, so the XP and loot are
   yours.
5. **They stay close.** A beast that chases something more than 45 m from you drops the chase and runs back; one more
   than 60 m away for 3 s, or 120 m away at once, comes back beside you.
6. **Modes**, for all your beasts at once: **Mode** on the wheel steps through them, or `/beastmaster mode <mode>`;
   the line at the bottom left shows it ("Beasts (passive): ...").
   - **Defending** (the first): they fight what attacks you or them, and what you send them at with Attack.
   - **Passive:** they ignore fights and keep following you; Attack still sends them at a creature (for 20 s).
   - **Aggressive:** they also go after hostile creatures within 25 m of you (never wildlife, farm animals, bosses or
     players).
7. **A beast that falls** rests for 60 seconds at home, and then you can call it again. Its body is gone a second after,
   so our own beasts never drop loot.
8. **Dismiss** a beast to send it home: **Dismiss <name>** on the wheel (one for each beast that is out), **Dismiss
   all** (with two or more out), or `/beastmaster dismiss <name or number>`; `/beastmaster dismiss` alone sends every
   beast home. A dismissed beast waits at home: call it again whenever you like. Beasts that are out stay out when you
   reload the world, and they come back beside you, free of mana.
9. **Building:** the game most likely does not let you build or place things next to a creature that runs the enemy
   AI (that is what its code suggests; not yet seen in game), and your beasts are such creatures. So while you, or
   another player within 20 m of them, hold something to build or place, your beasts that are out **step aside**:
   they leave for a moment and come back beside you, free, a few seconds after the building stops (the first time a
   note says so). **Beasts step aside while someone builds next to them** (`make_room`) turns this off.
   `/beastmaster aside` sends them away by hand, for example when they stand where you want to work; **Come back** on
   the wheel, `/beastmaster aside` again, calling or taming a beast, or sending the last one home ends it.
   `/beastmaster list` shows them as "aside".

## The stable

**Active slots.** You start with one: one beast out at a time. Slot 2 opens at character level 10, slot 3 at 20, slot 4
at 30 (settings; 0 turns a slot off). `/beastmaster slots` tells you yours and when the next one opens.

**Before your world has a stable**, you tame and call as before: any beast of your collection, as many out as your slots
(dismiss one on the wheel and call another to swap). A beast you tame while your slots are full goes home.

**The stable** is a prop Beastmaster knows: the game's **animal bed** (the straw bed for farm animals, large or small)
built in your base, within 80 m of a flame altar (on the host a new one counts within seconds), or any prop the host
marks with `beastmaster.stable mark` while looking at it (on a dedicated server an admin types `/beastmaster.stable
mark` in the chat). An animal bed your farm animals already use counts too; it keeps working for them. Once your world has a stable (it
stays so, kept with the world):
- only **active** beasts can be called, and the wheel lists only those; the others wait in the stable. Calling one in
  the stable says to make it active at a stable;
- at the first stable, each player's beasts that are out become active, then the first of their list, up to their
  slots;
- a beast you tame takes a free active slot; with none free it goes to your stable.

**The Beasts window:** look at the stable within 4 m and press **E** (the host). It shows your active slots (a beast and
its loyalty, an empty slot, or a locked one with the level that opens it) and the stable: every beast with its kind,
loyalty and whether it is out, at home or resting. Click a beast (or a slot) to select it, then:
- **Make active**: into a free slot (it can be called);
- **Store**: out of its slot (a beast that is out goes home);
- **Release**: it leaves your collection for good (a beast that is out leaves the world); click it twice within 5 s.

E or Esc closes the window. **Players who joined** use the chat within 10 m of a stable: `/beastmaster active <name or
number>`, `/beastmaster store <name or number>` and `/beastmaster release <name or number>` (the same command again
within 10 s lets the beast go). `/beastmaster list` shows which beasts are active.

**Your beasts' strength.** A beast out is always at your level (also when you level up while it is out), with the
health and damage a wild one of its kind has at that level, changed by its role:

| Kind | Role | Health | Damage |
|---|---|---|---|
| Wolf | fast hunter | x1.2 | x1.15 |
| Hound | like the wolf, a bit sturdier | x1.3 | x1.1 |
| Sabretooth | the striker | x1.15 | x1.25 |
| Snake | quick biter | x1.2 | x1.15 |
| Crimson, golden king, albino snake | (their bites are strong already) | x1.2 | x1, x0.95, x0.9 |
| Boar | tank | x1.7 | x0.9 |
| Aurochs | charging tank | x2.0 | x1.0 |
| Bear | heavy tank | x2.2 | x1.1 |

Between fights it heals: 5 s after its last hit, 5 % of its health a second.

**Loyalty** (1 to 10) grows with the fights a beast takes part in: a point for each hostile creature it hit that dies
within 30 s (wildlife and farm animals give none); 10 points to loyalty 2, 20 more to 3, and so on (450 to loyalty 10;
the **Loyalty grows** setting changes the pace). Each loyalty makes it a little stronger and bigger: at loyalty 10 it
has 20 % more health and damage and is 1.25x its size. The strength comes at once; the size the next time you call it
(the game draws a creature at the size it is made with). `/beastmaster list` shows it; a new loyalty is told to you.

**The wheel (hold F3):** Tame, Feed, Attack, **Mode** (steps passive, defending, aggressive), **Stable: animal bed** while your world has no stable (it says what to build), **Come back** while your beasts stepped aside by `/beastmaster aside`,
**Dismiss <name>** for each beast that is out (and **Dismiss all** with two or more out), then **Call <name>** for up
to 6 beasts that are at home or resting (once your world has a stable: the active ones); a resting one shows its time
left. The labels are short so they fit the
wheel; the mouse wheel turns the page when there are more than 8 items. With **Necromancer** installed too, F3 and F2
open one "Summons" wheel with "Beasts: ..." and "Undead: ..." items; turn **One wheel** (`shared_wheel`) off to keep
Beastmaster's own wheel. A line at the bottom left lists your beasts that are out, aside or resting: "Beasts: Wolf
(out), Bear (rests 1:20)".

**Wild bears, aurochs and snakes (with their packs):** about every 30 minutes of play, when there is no wild one yet, one of
them (at random, from the packs you have) appears 50-70 m from a player who is out in the open, at that player's level;
never within 40 m of a flame altar. Beat it down or feed it its bait to tame it.
- **Bear** (bear pack): 1.4x a wolf's size, 2.5x its health, 1.5x its damage, a little slower.
- **Aurochs** (beasts pack): a huge horned wild bull on the boar's skeleton, it charges like a boar: 1.3x a boar's
  size, 3x its health, 1.4x its damage, a little slower.
- **Snake** (beasts pack): a 5 m snake from a 3D scan of a pit viper, on a spine and animations of its own: it
  slithers, rears up like a cobra and strikes; 2x a wolf's health, 1.3x its damage, a little slower.
- **Rare snakes** (beasts pack): now and then a wild snake is a rare one, bigger and tougher. Whoever slays it (the
  last player to hit it, else the nearest player within 60 m) gets a bonus in their bag: runes, by chance a tier-2
  rune, and by chance a weapon of that rarity at their level.

  | Kind | Of the wild snakes | Size | Health / damage (x a wolf's) | Runes | Tier-2 rune | Weapon |
  |---|---|---|---|---|---|---|
  | Crimson snake | 1 % | 1.15x | 3x / 1.6x | 3-5 | 15 % | 5 %, epic |
  | Golden king snake | 0.4 % | 1.25x | 4x / 1.8x | 4-6 | 25 % | 10 %, epic |
  | Albino snake | 0.25 % | 1.35x | 5x / 2.0x | 6-10 | 50 % | 25 %, legendary |

## Commands

All of these work from the game's chat for every player (`/beastmaster <word>`), and in the console
(`beastmaster.<word>`). Without a word, `/beastmaster` lists them.

| Command | What |
|---|---|
| `/beastmaster list` | your beasts and their loyalty: "1. Wolf - wolf, loyalty 3, out, active", "2. Bear - bear, loyalty 1, rests 0:40" (", active" once your world has a stable) |
| `/beastmaster slots` | your active slots and the level that opens the next one |
| `/beastmaster active <name or number>` | within 10 m of a stable: make a beast active (into a free slot) |
| `/beastmaster store <name or number>` | within 10 m of a stable: put a beast into the stable (one that is out goes home) |
| `/beastmaster release <name or number>` | within 10 m of a stable: let a beast go for good (the same command again within 10 s) |
| `/beastmaster rename <name or number> <new name>` | rename a beast (1-20 characters, not a name your collection has) |
| `/beastmaster tame` | tame the beast you look at, else the nearest beaten-down one (under 25 % health, within 8 m) |
| `/beastmaster feed` | feed the beast you look at (else the nearest) its bait: 3 times to tame it |
| `/beastmaster call <name or number>` | call a beast from your collection |
| `/beastmaster dismiss <name or number>` | send that beast home |
| `/beastmaster dismiss` (or `dismiss all`) | send every beast that is out home |
| `/beastmaster attack` | your beasts that are out attack the creature you look at (else what hit you last) |
| `/beastmaster aside` | your beasts that are out step aside (leave for now), or come back; they do it on their own while someone builds next to them |
| `/beastmaster mode [passive, defending or aggressive]` | how all your beasts fight; without a word, the mode now |
| `beastmaster.stable mark`, `unmark`, `list` | **host or admins:** the prop you look at (no camera data: the one within 3 m of you) is a stable; the marked prop nearest you (within 8 m) is no stable; this world's stables |
| `beastmaster.loyalty <name or number> <1-10>` | **host console only:** set a beast's loyalty (tests) |
| `beastmaster.give <species>` | **host console only:** add a beast to your collection without taming it (wolf, boar, sabretooth, hound, bear, aurochs, snake, crimson_snake, golden_snake, albino_snake) |
| `beastmaster.species <graph guid> <faction> [scale]` | for modders: which beast a creature is, by the animation graph its template runs, its faction number and its scale (render models are not on the server world; the bear its own copy of the wolf's graph at 1.4x (the bear pack 1.0's: the wolf's graph at 1.4x), the aurochs the boar's at 1.3x, the snake and each rare snake its own copy of the wolf's graph) |

## Settings (Mods tab, EMF.exe, or `set beastmaster <key> <value>`)

| Setting | Default | What it does |
|---|---|---|
| Beast wheel (`wheel_key`) | F3 | hold it for the wheel (any key the game does not use; B is its backpack key) |
| Beasts window (at a stable) (`stable_key`) | E | with a stable under your crosshair within 4 m; elsewhere E does what the game does |
| Active slot 2, 3, 4 at level (`slot2_level`, `slot3_level`, `slot4_level`) | 10, 20, 30 | 0: that slot never opens |
| One wheel with Necromancer's undead (`shared_wheel`) | on | off: Beastmaster's own wheel |
| Mana to call a beast (`call_mana_pct`) | 15 % | of your maximum mana; 0 = free |
| Seconds between calls (`call_cooldown_s`) | 10 | |
| Rest after a beast falls (`rest_s`) | 60 s | |
| Beasts in your collection (`collection_max`) | 8 | 1-16 |
| Tame by defeat below (`tame_health_pct`) | 25 % | |
| Taming takes (`tame_channel_s`) | 3 s | |
| Taming range (`tame_range_m`) | 8 m | |
| Baits to tame by feeding (`feed_count`), Feeding window (`feed_window_s`) | 3, 10 s | |
| Bait for wolves, hounds, boars, sabretooths, bears, aurochs, snakes, the rare snakes (`bait_<kind>`) | see above | an item's debug name (`items meat` in the console) |
| Beasts drop a chase at (`chase_m`) | 45 m | they turn from the fight and run back |
| Loyalty grows (`loyalty_pace`) | 1x | 2x: half the kills per loyalty |
| Beasts come back from (`leash_m`), At once from (`leash_far_m`) | 60 m, 120 m | |
| Aggressive beasts go at foes within (`aggro_m`) | 25 m | |
| You get your beasts' kills (`xp_credit`) | on | XP and loot for the owner |
| Beasts step aside while someone builds next to them (`make_room`) | on | while you, or a player within 20 m of them, hold something to build or place, your beasts leave and come back after |
| Rare wild bears, aurochs and snakes (`wild_bears`), About every (`wild_bear_minutes`) | on, 30 min | needs their pack |

## The bear pack (`emf_beastmaster_bear`)

An EML data patch that adds one new creature, `Beastmaster_Bear` (template `b3a7c2e1-5f4d-4c8b-9e21-7a0d4b6e2f02`): a
realistic grizzly on a skeleton and animations of its own (it walks, runs, rears up to swipe, flinches and rolls over
when it dies), played by a copy of the game wolf's animation graph and AI. Bear model and animations: "Realistic Animated
Bear 3D Model" by WildMesh 3D, licensed under CC BY 4.0 (used in the game with changes). Pack 1.0's bear, made with
Meshy on the game wolf's skeleton, is still known to Beastmaster. It is in
the **EMF creature packs** download on EMF's Nexus page. Install it with EMF.exe ("Install a downloaded file") or put
its folder in the `mods` folder next to the game; EMF builds it when the game starts (it is EML content: EML or
ShroudForge can apply it too). The template sets the size
(`ScaleComponent` 1.4), the damage (DamageMod multiplier x1.5) and the speeds (walk 1.6 m/s, run 6 m/s as a wolf's,
the rest x0.85). The health (x2.5) comes from
Beastmaster when it spawns a bear: the game's template has no base health of its own.

To rebuild it from its sources (for modders; the sources are not in the repository): `python -I
tools/models/make_mod.py tools/models/packs/beastmaster_bear.json`. The pack holds the new model and its texture, plus
instructions that copy and change the game's wolf at load time; no game files are copied into it.

## The beasts pack (`emf_beastmaster_beasts`)

An EML data patch with new beasts:
- the Aurochs, `Beastmaster_Aurochs` (template `d5f2b8e3-7c4a-4e9f-8b21-6a3c8d4e0f02`), made with Meshy and fitted to
  the boar's skeleton (its animations and AI): size (`ScaleComponent`) 1.3, damage x1.4, speeds x0.9; the health (x3)
  comes from Beastmaster. Its tail follows its hips.
- the Snake, `Beastmaster_Snake` (template `d4c87337-8c53-41df-8488-8079046e1502`), on a spine and animations of
  its own: "CC0 Japanese Pit Viper" by ffish.asia / floraZia.com (CC0), a 3D scan scaled to 5 m, its slither, rearing,
  strikes and death made for it, played by a copy of the wolf's animation graph (the wolf's AI); damage x1.3, walk
  1.6 and run 4.5 m/s; the health (x2) comes from Beastmaster. Pack 1.0's snake was a cartoon snake.
- the rare snakes, `Beastmaster_CrimsonSnake`, `Beastmaster_GoldenKingSnake` and `Beastmaster_AlbinoSnake` (templates
  `d4c87337-8c53-41df-8488-8079046e1512`, `...1522`, `...1532`): the snake with the scan's skin recoloured, each
  its own copy of the skeleton and graph; sizes 1.15, 1.25 and 1.35, damage x1.6, x1.8 and x2.0; the health (x3, x4,
  x5) comes from Beastmaster. With an older beasts pack that lacks them, a wild snake is always a plain one.

Installed and rebuilt like the bear pack (`tools/models/packs/beastmaster_beasts.json`). Without the pack (or when its
content was not built), an aurochs or snake in your collection stays at home: calling it says that it comes with the
EMF creature packs and how to install them, and one that was out when you saved goes home with one note when you
load.

## More beasts: `species_extra.lua` (for modders)

A file `species_extra.lua` in Beastmaster's folder (beside `species.lua`) adds beasts: a creature of your own, or one
from a content pack you cannot share. It returns a list of entries like the ones in `species.lua`:

```lua
return {
	{ id = "troll", name = "Troll", plural = "trolls",
		template = "<GUID of the template a call spawns>",
		graph = "<GUID of the animation graph the creature runs>",
		factions = { [3] = true },  -- optional: only creatures of these factions (3: Vukah)
		health_mul = 3,             -- optional: times the game's base health
		bait = "Food_T2_Dog_Bait",  -- optional: the item it is fed; without one it is tamed only by beating it down
		pack = "my content pack" }, -- optional: it also comes as a rare wild beast, like the packs' beasts
}
```

Each entry needs `id`, `name`, `template` and `graph`. An entry without them, or with an id that is taken, is
skipped. EMF's log lists the skipped entries and the beasts added, and says when the file does not load. Beastmaster
does not ship this file.

## Good to know

- It runs where the world runs: the host or a dedicated server. Without the packs, players who join need nothing:
  they see the beasts and use `/beastmaster` in the chat. **With a pack, everyone needs it:** the host (or the
  server) and every player who joins, because it changes the game's data (an EML patch). The wheel, the Beasts window
  and the line on screen are the host's.
- Render models are only on the game's client side: on a dedicated server a built stable is not seen; an admin marks it
  (`/beastmaster.stable mark`).
- Your collection is kept with the world, per player (by Steam account).
- A beast that is out is taken out of the world while its owner is away, and comes back when they return.
- Beasts are ordinary game creatures with the enemy AI: they guard you as the game's companions do, and sometimes take
  their own path to a foe.
- The bear's and the aurochs' health boost needs Beastmaster to spawn them: one made another way (the `spawn` console
  command) has a wolf's or a boar's health.
- Beasts are called with the wheel or the chat command; there are no spell items for it. The species are the six
  above.
