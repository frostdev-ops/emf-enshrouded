# New content: items (what works and what does not)

EMF changes the running game; new items are game **data**. They come from a data patch made with EML, the community's
Enshrouded Mod Loader (github.com/Brabb3l/kfc-parser): a short Lua file that adds resources to the game's data files
before the game reads them. EMF's own tools then work with the new item like with any other (`items`, `give`, `put`,
`take`, mods).

`content\emberbrand\` is a complete example: a new sword, the **Emberbrand** (a Bronze Sword with more damage and some
fire), with its own name in every language, and a recipe for it in the crafting menu. Read `src\mod.lua`: it uses
EMF's helper (below) and is about 40 lines.

## The helper: `emf_content.lua`

`content\lib\emf_content.lua` does the steps below for you. Copy it into your mod's `src` folder, next to `mod.lua`, and:

```lua
local content = require("emf_content")
content.add_item{guid = ..., name_guid = ..., debug_name = "Weapon_..._Mine", name = "My Sword", clone_of = <item GUID>,
                 edit = function(item) item.damageSetup.dmgMod = 1.5 end}
content.add_recipe{guid = ..., debug_name = "Craft_Mine", output = {<item GUID>, 1}, inputs = {{<item GUID>, 5}},
                   like = "Consumable_Tool_Campfire_PK", menu = {group = "<group GUID>"}}
content.finish()   -- once, at the end: writes the names back
```

- `add_item` copies an item (model, icon, animations, perks), registers it and gives it its name.
- `add_recipe` adds the recipe **and lists it in the crafting menu**. The menu is not built from the recipe table but
  from a fixed tree in the game's UI data (per workshop: groups, sets, recipes); a recipe that is not in that tree
  never shows. `like` names a vanilla recipe whose station, unlock and duration are copied (the campfire: the
  player's own crafting, known once you have had wood logs); `unlock = "always"` or `unlock = {item = <GUID>}`
  overrides the unlock; `menu = {group = ..., set = 1}` picks the place (`content.menu_groups(0)` lists the own
  crafting menu's groups; Emberbrand uses the weapons group `2632c31c-37fe-48f6-b788-dfa649065edb`). Without `menu`
  the recipe goes where the copied one is.
- `add_text(id, text)` adds a line to the English text table; `add_prompt_words("My words")` adds the words of an E
  prompt of your own (`emf.offer{text = "My words"}`, EMF API v25).
- The UI parts (menu, icons, text) are skipped on the dedicated server for you.

## What a new item needs

All four come from the example, and the game stops or shows nothing if one is missing:

1. **The item** (`keen::ItemInfo`): a copy of an existing item's whole resource under a new GUID, then your changes
   (`itemId`, `objectId`, `name`, `debugName`, damage, perks...). A copy keeps the model, animations and sounds.
2. **Its icon** (`keen::UiTextureResource` under the item's own GUID): copy the source item's. Without it the game
   stops with "corrupted game ui texture" the moment the item shows.
3. **Its registration**: the GUID in `ItemRegistryResource.itemRefs` and the debug name in `dbgNames` (same position).
4. **Its name**: one tag in the keenglish loca table (the game falls back to it for every language), in sorted order.

## Rules that matter

- **Fixed GUIDs, forever.** Saves keep items by ItemId = FNV-1a of the GUID. Pick new GUIDs once for your item (any
  GUID generator) and never change them, or every saved copy disappears.
- **Everyone needs the same patch.** Each game builds its tables from its own files: the host and every player need
  the same content mod (with the same GUIDs). Nothing checks this; from the game's code, a player without it sees
  "[Invalid Item Stack]" (not tested with two players).
- **Removing the mod removes the items.** From the game's code: it deletes stacks of unknown items when it next
  touches them (not tested). Take the items out first, and never remove it while one is equipped.
- **Game updates** replace the data files. EML's loader (`dbghelp.dll`) patches again at the next start; if you
  patch offline with `emm.exe`, run it again after every update **before loading a world**, or the item is unknown
  and the game deletes saved copies when it next touches them.
- **A recipe needs its menu entry.** The game's recipe table alone is not enough: the crafting menu lists only the
  recipes in its own tree (the helper's `add_recipe` adds both). The server never checks the unlock when crafting:
  every lock is the menu's.
- **The dedicated server has no render or UI data.** Its files (`enshrouded_server.*`) carry no models, meshes,
  textures, icons (`keen::UiTextureResource` is not even a type there) or text tables, and EML tells your script with
  `loader.is_server`. Make those parts conditional (`if not loader.is_server then ... end`) and keep the rest (items,
  templates, registries, recipes) for both sides, or the server's build stops at the first missing resource or type.
  Emberbrand does this: on a server it adds the item and the recipe without the icon and the name. EMF builds a
  server's content at the server's start in `emf\content\server\` (`docs/engine_parts/content_loader.md`).

## Apply it

EML applies patches either at every game start (its `dbghelp.dll` loader) or once, offline:
`emm.exe run --patch -g "<game folder>"` with the mod in `<game folder>\mods\<id>\`. `emm.exe restore` puts the
original `enshrouded.kfc` back but leaves the data EML appended.

For testing, `scripts\content_test.ps1` from the EMF source (not in the release zip) does it with full backups (in
`%LOCALAPPDATA%\EMF\content_test_backup`, out of reach of temp cleaners; `-Backup <folder>` picks another). It
restores the folder it backed up, puts back containers an earlier EML patch had written, and checks every file:

```powershell
scripts\content_test.ps1 -Apply -Mod content\emberbrand -Emm <path to emm.exe>   # backs up, patches
# ... play: items Emberbrand, put Weapon_T1_1H_Sword_Emberbrand 1 ...
scripts\content_test.ps1 -Restore                                              # byte-for-byte back
```

## More loot without new data

`loot_scale` (an EMF mod, off by default) multiplies the world's "Loot Amount" setting: chests, enemy drops and
pickups give more (or fewer) materials, and nothing is saved into the world. Its function is found in the dedicated
server too (not tested on a running server). Switching it off takes effect at the next start; factor 1 brings
chests, enemy drops and pickups back to the world's amounts at once (harvesting, chopping and mining follow it unless
they have their own factor). Set
`factor` in the Mods tab or `emf\config\loot_scale.ini`. It is also a short example of hooking a game function by its
bytes (`hook_pattern`). Plants picked with E, chopped trees and mined rocks never read that setting: for those it
watches the game's `PickupEvent` and gives the difference (`put_items_ex`) or takes it back (`take_items`), with
their own factors (`harvest`, `chopping`, `mining`; 0 = the same as `factor`). Fruit out of a tree's own inventory
follows `factor` (that inventory was filled with the game's setting). See `mods/loot_scale/README.md`.
