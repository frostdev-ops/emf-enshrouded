# The game's reflection data: type descriptors in the exe (build 1273043)

Every type the game reflects (components, events, resources, enums, the serialized `keen::ds::*` forms...) has a
descriptor in the exe's `.rdata`, and one master list points at all of them. EMF reads them with
`tools/re/exe_types.py` (Python, standard library only) and builds its generated headers from them
(`python tools/gen_headers.py <game>\enshrouded.exe`). The bytes are static data: no code has to run to fill them, so a
static reader and a reader inside the running game see the same thing (in the game the pointers are already moved to
the module's real base).

Found by static analysis of `enshrouded.exe` 1273043 (sha256 `d64966e2...`). Confidence High: the reader's output
equals EML's `types.json` export of the same build for all 12,646 types (names, namespaces, kinds, sizes, alignments,
both hashes, base and element types, counts, every field's name, type and offset in order, enum values, attribute
names), and the headers generated from it are byte for byte the committed ones. Not checked on the dedicated server's
exe (none on the analysis PC); its types come from the same code, so the same layout is expected.

## Conventions
- All offsets are in bytes. Every pointer is an absolute 64-bit address at the image base `0x140000000`; every one of
  them has a base relocation, so in the running game it already points at the right place.
- A **slice** is `{const char* data; uint64 length}`. The text is also NUL-terminated in this build; read `length`
  bytes. An empty name is `{pointer to "", 0}`.
- A **list** is `{T* data; uint64 count}`.

## Finding the type table (no addresses needed)
The master list is the longest list of type descriptor pointers stored as `{data, count}` in `.rdata`/`.data`.
`exe_types.find_type_table` scans both sections as 64-bit words for a pointer into the image followed by a count, keeps
the candidates whose first and last entries look like descriptors (below), checks every entry of those, and takes the
longest. The other lists of descriptors (the groups, 687 and 131 entries) are shorter, so there is no doubt.

A descriptor "looks like" one when, at an 8-aligned address with 0x98 readable bytes: the qualified name (`+0x20`) and
the name (`+0x00`) are slices of text (non-empty, NUL right after them), the kind byte (`+0x4C`) is not 0 and the
alignment (`+0x44`) is a power of two.

The list is held in two places. One is the **registry root**, four lists in a row:

| Root offset | List | 1273043 |
|---|---|---|
| `+0x00` | all type descriptors (`Type*[]`) | RVA `0x16ffca0`, 12,646 types (root at RVA `0x171c580`) |
| `+0x10` | namespaces (`Namespace*[]`) | 213 |
| `+0x20` | attribute descriptors (`AttributeDesc*[]`) | 133 |
| `+0x30` | groups (`Group[]`, 0x20 bytes each) | 3 |

A **group** is `{slice name; list of Type*}`. The first group has an empty name and is the master list again; then
`ecs.component` (687: every component the game knows, including 26 abstract bases such as `keen::ecs::Component`
itself) and `ecs.event` (131: the events and their two bases `GameEvent` and `Event`). These match the lists
`gen_headers.py` derives by inheritance exactly (661 components + 26 bases, 129 events + 2 bases). The reader takes as
root the holder whose `+0x30` list reads as groups.

A type's **index** is its position in the master list (`keen::BlobString` is 0, `keen::ecs::Component` is 136). EML
numbers types the same way. A type reached only through another type and not in the list would be numbered after the
list (none in 1273043).

## Type descriptor (at least 0x98 bytes)

| Offset | Size | Field |
|---|---|---|
| `+0x00` | 16 | name slice: `CurrentTransform` |
| `+0x10` | 16 | impact name slice: `ecs.CurrentTransform` (the name Impact scripts use; often the same as the name) |
| `+0x20` | 16 | qualified name slice: `keen::ecs::CurrentTransform` |
| `+0x30` | 8 | `Namespace*` (`ecs`, whose parent is `keen`) |
| `+0x38` | 8 | inner `Type*`: the base of a struct (null when none), the element of an array, the integer of an enum, the enum of a bitmask, the target of a typedef or reference |
| `+0x40` | 4 | size |
| `+0x44` | 2 | alignment |
| `+0x46` | 2 | element alignment (differs from the alignment on 210 types, all with alignment 16: GPU buffer structs and the SIMD vectors `Vector3`, `Vector4`, `vf32_data`) |
| `+0x48` | 4 | count: fields of a struct, values of an enum, elements of a static array, types of a variant |
| `+0x4C` | 4 | kind word: the kind in the low byte, flags in the upper 24 bits (tables below) |
| `+0x50` | 4 | hash = FNV-1a-32 of the qualified name (true for all 12,646 types). The ECS knows components and events by it (`getComponent`, `emitEvent`) |
| `+0x54` | 4 | internal hash (it looks like a layout hash: every empty component has `0x6c36572e`; how it is computed is not known) |
| `+0x58` | 8 | `Field*` (struct): `count` entries of 0x30 bytes |
| `+0x60` | 8 | `EnumValue*` (enum): `count` entries of 0x28 bytes |
| `+0x68` | 8 | `Type**` (blob_variant, ds_variant): `count` types the variant can hold (`BlobVariant<keen::ecs::Component>` lists all 687 components) |
| `+0x70` | 16 | default value: `{void* data; uint64 size}` (1,444 types; the raw bytes of a default instance) |
| `+0x80` | 16 | attributes: list of `Attribute` (0x18 bytes each) |
| `+0x90` | 8 | a pointer into `.text` on 2,116 structs, all with flag `0x01` (serialized forms); purpose not looked at |

Only the pointer that fits the kind is set; the others are null.

### Field (0x30 bytes)
| Offset | Field |
|---|---|
| `+0x00` | name slice |
| `+0x10` | `Type*` of the field |
| `+0x18` | u32 offset in the struct (the next 4 bytes are 0) |
| `+0x20` | attributes: list of `Attribute` |

A struct lists only its own fields; the base's fields come from the inner type (and sit at lower offsets).

### Enum value (0x28 bytes)
`+0x00` name slice, `+0x10` 64-bit value (no negative values in 1273043; read signed when the enum's integer is
signed), `+0x18` attributes list (unused in 1273043).

### Attribute (0x18 bytes) and attribute descriptor
An attribute is `{AttributeDesc* desc; slice value}`; the value is text (`"0.0f"`, a description, a name) or empty
for a flag-like attribute (`abstract`, `server_only`, `is_dynamic`, `do_not_save`, `savegame_relevant`, `hidden`...).
An attribute descriptor starts with `Namespace*` and its name slice at `+0x08`; `+0x18` looks like the value's type
(`keen::BlobString`, or null for a flag). 4 types carry the same attribute twice, so EMF keeps attributes as a list.

### Namespace
`+0x00` name slice, `+0x10` parent `Namespace*` (null for `keen`). The rest was not needed.

## Kinds (low byte of `+0x4C`)
| Kind | EMF name | | Kind | EMF name |
|---|---|---|---|---|
| `0x01` | bool | | `0x10` | bitmask64 |
| `0x02` | uint8 | | `0x11` | typedef (`HashKey32`, `ItemId`...) |
| `0x03` | sint8 | | `0x12` | struct |
| `0x04` | uint16 | | `0x13` | static_array (count = elements) |
| `0x05` | sint16 | | `0x14` | ds_array (`DsArray<T>`) |
| `0x06` | uint32 | | `0x15` | ds_string (`DsString`) |
| `0x07` | sint32 | | `0x16` | ds_optional |
| `0x08` | uint64 | | `0x17` | ds_variant |
| `0x09` | sint64 | | `0x18` | blob_array (`BlobArray<T>`) |
| `0x0A` | float32 | | `0x19` | blob_string (`BlobString`) |
| `0x0B` | not used in 1273043 (probably float64) | | `0x1A` | blob_optional |
| `0x0C` | enum | | `0x1B` | blob_variant |
| `0x0D` | bitmask8 | | `0x1C` | object_reference (`ObjectReference<T>`, 16 bytes) |
| `0x0E` | bitmask16 | | `0x1D` | guid (`keen::Guid`, 16 bytes) |
| `0x0F` | bitmask32 | | | |

`exe_types.py` writes an unknown kind as `kind_<number>` and `gen_headers.py` treats it as opaque bytes.

## Flags (upper 24 bits of `+0x4C`, shifted down)
Seen, not traced in code; EMF does not use them yet.

| Flag | On | Looks like |
|---|---|---|
| `0x01` | 4,801 | the serialized (`keen::ds::*`) form, and types built from it |
| `0x02` | 2,148 | holds an array (`BlobArray`/`DsArray` itself or in a field) |
| `0x04` | 575 | holds a string |
| `0x08` | 65 | holds an optional |
| `0x10` | 37 | holds a variant |
| `0x20` | 346 | a GPU uniform buffer struct (attribute `gpu_uniform`) |
| `0x40` | 197 | a GPU storage buffer struct (`gpu_storage`) |
| `0x80` | 54 | a GPU push constant struct (`gpu_push_constant`) |

## The build record
The exe also has a record of its own build in `.data` (RVA `0x1fc6018` in 1273043): a pointer to the git commit text
(40 hex digits, `f68140a3...`), then 0x10 bytes later pointers to the version (`0.0.15.0`) and the branch (`master`).
The game prints them at start (`[GameFramework] enshrouded(detached HEAD) - version <commit> (master)`). The Steam
build number (1273043) is not in the exe; `exe_types.py` names it from the exe's sha256 with the same table as
`src/common/signatures.cpp`.

## EMF's types file
`python -I tools/re/exe_types.py --exe <enshrouded.exe> -o types.emf.json` (about 1 s, 8 MB; game data, so it is not
committed). One JSON document: `format` (`emf-types`), `format_version`, `source` (file, sha256, size, PE timestamp,
image base, build, version, git commit, branch), `type_table` (address, count, root), `groups` (name to type indices)
and `types`, one per line:

```json
{"index":206,"name":"WorldPosition","impact_name":"WorldPosition","qualified_name":"keen::WorldPosition",
 "namespace":["keen"],"kind":"struct","flags":0,"size":24,"alignment":8,"element_alignment":8,
 "hash":4187772213,"internal_hash":3280565889,"inner":null,"count":3,
 "fields":[{"name":"x","type":28,"offset":0},{"name":"y","type":28,"offset":8},{"name":"z","type":28,"offset":16}],
 "attributes":[["extern","keen/math/world_transform.hpp"],["compact",""]]}
```

`inner` and field `type`s are type indices; enums have `values` (`[{"name", "value"}]`), variants `variants`,
types with a default `default` (hex). `gen_headers.py` reads this file, the exe, or EML's `types.json` (converted to
this form on load).

## Porting the reader to C++ (inside the game)
The Python reader is written to port line by line:
- **Memory.** `Image` becomes the game module: its base from `GetModuleHandle(nullptr)`, the section table from the
  PE headers in memory, the same "inside an initialized section" check before every read (a wild pointer must never
  be followed). No address of any build is needed, and nothing has to be relocated: the loader already did that.
- **Finding.** The same scan of `.rdata` and `.data` for `{pointer, count}` (about 1.6 million words, a few
  milliseconds in C++), the same descriptor check, the longest list, then the root by its group list. It can run at
  any time after the exe is mapped (the data is static), before the first world exists.
- **Decoding.** Plain structs with these offsets and `static_assert`s on them (`Slice`, `TypeDesc`, `FieldDesc`,
  `EnumValueDesc`, `AttributeEntry`, `NamespaceDesc`, `GroupEntry`, `Root`), reading names by length.
- **Lookups.** By index (the master list), by hash (the hash is FNV-1a-32 of the qualified name, the same hash the
  ECS uses, so `fnv1a("keen::ecs::Health")` finds the descriptor), fields by name within a struct and its bases, the
  `ecs.component` and `ecs.event` groups as the lists of components and events.
- **Tests.** The C++ tests need the same kind of fake image as `tests/tools/reflection_fixture.py`: descriptors
  laid out in a buffer whose address stands in for the module base.
- **If it fails** (a new layout: no table found, or a descriptor that does not check out), log it and keep using the
  generated headers, which stay the build-time copy of the same data.
