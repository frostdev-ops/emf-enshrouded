# Loot Scale (`loot_scale`)

More (or fewer) materials from everything you gather, on top of the world's own **Loot Amount** setting. Nothing is
saved into the world: switch the mod off and your loot is what the world's settings say again. In a game you host it
works for every player (the host makes the loot); it was not tested on a dedicated server.

**New in 1.1.0:** picking plants with E, chopping and mining are scaled too, and they follow the loot amount factor
unless you give them their own. Before 1.1.0 they kept the game's amounts; to keep it that way, set the three of them
to 1.

## Switch it on

Loot Scale is installed **switched off**. Switch it on in the Mods tab (the key under Esc) or in EMF.exe, then restart
the game. The factors change at once, also while you play.

## Settings (Mods tab, EMF.exe, or `set loot_scale <key> <value>`)

| Setting | Default | What it scales |
|---|---|---|
| Loot amount factor (`factor`) | 2 | chests, enemy loot and things you pick up from the ground (twigs, stones...): 0.1-10 |
| Picking plants and bushes (`harvest`) | 0 | berries, flax, crops and other plants you pick with E |
| Chopping trees (`chopping`) | 0 | wood and what else trees and woody bushes drop when you hit them |
| Mining rocks (`mining`) | 0 | stone, ore and minerals from rocks and veins you hit |

**0 means "the same as the loot amount factor"**, so with only the first one set, everything follows it. Set one of the
three to give it its own factor (0.1-10): `harvest 3` triples what you pick and leaves the rest at the loot amount
factor. Under 1 means fewer; a fraction comes by chance (x1.5 on one berry: two berries half the time).

## How it works

- **Chests, enemy loot, things on the ground**: the game reads its Loot Amount setting when it makes that loot, and the
  mod multiplies what the game reads.
- **Plants, chopping, mining**: the game does not read that setting there. When the game tells that you got something
  from a plant you picked, a tree or a rock, the mod gives you the difference (the game shows it as a second line,
  "+3 Berries"), or takes some back for a factor under 1.
- **Fruit trees** (fruit you pick out of the tree's own store, such as bananas or coconuts) follow the loot amount
  factor: the game fills that store with its Loot Amount setting, so the mod has already scaled it.
- Never scaled: what you craft, what you take out of a chest or a loot spot after it was filled (it was scaled when it
  was made), what a player dropped, and anything a mod gives.
- Digging terrain (soil and stone blocks from the ground) is not scaled.

## Good to know

- **Loot amount factor 1 no longer switches everything off at once**: harvesting, chopping and mining keep their own
  factors when you set them. Switch the mod off (and restart) to turn it all off.

- A fresh world or a game update: the log (`emf\emf_core.log`, lines `[loot_scale]`) shows `first loot`, and
  `first harvesting`, `first chopping`, `first mining` with the numbers the first time each happens.
- Life Skills counts the extra items as gathered: they give its XP too.
