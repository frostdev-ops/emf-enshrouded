"""Reads the game's reflection data (its keen type descriptors) straight from enshrouded.exe and writes EMF's types
file: every reflected type with its size, alignment, kind, hashes, base type, fields (name, type, offset), enum values,
attributes and default value, plus where it came from (the exe's sha256, build, version and git commit).

Usage: python -I tools/re/exe_types.py [--exe <enshrouded.exe>] [-o <types.emf.json>]
Without --exe: EMF_GAME_DIR's enshrouded.exe (the dedicated server's exe works too). Default output: types.emf.json in
the current folder. tools/gen_headers.py reads the file, or the exe itself.

The descriptor layout is in docs/engine_parts/re_reflection.md. The reader needs no address of any build: it finds the
type table by its shape, so it is meant to be ported as is to C++ inside the running game, where the same bytes are
mapped at the module's base. Python 3 standard library only.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys

FORMAT = "emf-types"
FORMAT_VERSION = 1

# Known game builds by the exe's sha256: the same table as kKnownBuilds in src/common/signatures.cpp (a test checks).
KNOWN_BUILDS = {
    "82e5020a8736348cd0374f004b2f5e5ef456b5bd27aa5420df34c7dd241cfeeb": 1269579,
    "d64966e23b1a458ee6f147bcbceac0b577fa7e39a212f371287d8cc119817882": 1273043,
    "d7baa768eb00aa895d9f5a1cd8747ccf66858c7b542f50983092b2e3261588ee": 1273043,  # its dedicated server
}

# The low byte of a descriptor's kind word. 0x0B is not used by build 1273043 (between float32 and enum).
KINDS = {
    0x01: "bool", 0x02: "uint8", 0x03: "sint8", 0x04: "uint16", 0x05: "sint16", 0x06: "uint32", 0x07: "sint32",
    0x08: "uint64", 0x09: "sint64", 0x0A: "float32", 0x0C: "enum", 0x0D: "bitmask8", 0x0E: "bitmask16",
    0x0F: "bitmask32", 0x10: "bitmask64", 0x11: "typedef", 0x12: "struct", 0x13: "static_array", 0x14: "ds_array",
    0x15: "ds_string", 0x16: "ds_optional", 0x17: "ds_variant", 0x18: "blob_array", 0x19: "blob_string",
    0x1A: "blob_optional", 0x1B: "blob_variant", 0x1C: "object_reference", 0x1D: "guid",
}
SIGNED = {"sint8", "sint16", "sint32", "sint64"}

# --- the layout (offsets in bytes; every pointer is an absolute address at the image base) ---
# A slice is {const char* data, u64 length}; the text is also NUL-terminated.
TYPE_NAME, TYPE_IMPACT_NAME, TYPE_QUALIFIED_NAME = 0x00, 0x10, 0x20  # slices
TYPE_NAMESPACE = 0x30        # Namespace*
TYPE_INNER = 0x38            # Type*: the base of a struct, the element of an array, the integer of an enum...
TYPE_SIZE = 0x40             # u32
TYPE_ALIGNMENT = 0x44        # u16
TYPE_ELEMENT_ALIGNMENT = 0x46  # u16
TYPE_COUNT = 0x48            # u32: fields, enum values, array elements or variant types
TYPE_KIND = 0x4C             # u32: kind in the low byte, flags above
TYPE_HASH = 0x50             # u32: the hash the ECS uses for the type (components by this hash)
TYPE_INTERNAL_HASH = 0x54    # u32
TYPE_FIELDS = 0x58           # Field[count] (struct)
TYPE_VALUES = 0x60           # EnumValue[count] (enum)
TYPE_VARIANTS = 0x68         # Type*[count] (blob_variant, ds_variant)
TYPE_DEFAULT = 0x70          # {void* data, u64 size}
TYPE_ATTRIBUTES = 0x80       # {Attribute* data, u64 count}
TYPE_BYTES = 0x98            # read at least this much of a descriptor

FIELD_BYTES, FIELD_NAME, FIELD_TYPE, FIELD_OFFSET, FIELD_ATTRIBUTES = 0x30, 0x00, 0x10, 0x18, 0x20
VALUE_BYTES, VALUE_NAME, VALUE_VALUE, VALUE_ATTRIBUTES = 0x28, 0x00, 0x10, 0x18
ATTRIBUTE_BYTES, ATTRIBUTE_DESC, ATTRIBUTE_VALUE = 0x18, 0x00, 0x08   # value: slice
ATTRIBUTE_DESC_NAME = 0x08   # slice (at +0x00: its Namespace*)
NAMESPACE_NAME, NAMESPACE_PARENT = 0x00, 0x10
GROUP_BYTES, GROUP_NAME, GROUP_LIST = 0x20, 0x00, 0x10   # list: {Type** data, u64 count}
ROOT_TYPES, ROOT_GROUPS = 0x00, 0x30   # {Type** data, u64 count}; between them the namespace and attribute lists

MAX_NAME = 4096
MAX_TYPES = 1 << 20
MAX_GROUPS = 256


class ImageError(Exception):
    """The file is not a game exe this reader understands; the message says what is missing."""


class Image:
    """The initialized bytes of a PE image, addressed by virtual address at its preferred base (as mapped)."""

    def __init__(self, image_base, sections):
        """sections: [(name, va, bytes)]."""
        self.image_base = image_base
        self.timestamp = None  # the PE header's, when read from a file
        self.sections = sorted(((name, va, bytes(raw)) for name, va, raw in sections), key=lambda s: s[1])

    @classmethod
    def from_pe(cls, blob):
        if len(blob) < 0x40 or blob[:2] != b"MZ":
            raise ImageError("not an exe (no MZ header)")
        pe = struct.unpack_from("<I", blob, 0x3C)[0]
        if pe + 24 > len(blob) or blob[pe:pe + 4] != b"PE\0\0":
            raise ImageError("not an exe (no PE header)")
        nsec, = struct.unpack_from("<H", blob, pe + 6)
        opt_size, = struct.unpack_from("<H", blob, pe + 20)
        opt = pe + 24
        if struct.unpack_from("<H", blob, opt)[0] != 0x20B:
            raise ImageError("not a 64-bit exe")
        base, = struct.unpack_from("<Q", blob, opt + 24)
        sections = []
        for i in range(nsec):
            o = opt + opt_size + 40 * i
            name = blob[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, rva, raw_size, raw_at = struct.unpack_from("<IIII", blob, o + 8)
            sections.append((name, base + rva, blob[raw_at:raw_at + min(raw_size, vsize or raw_size)]))
        img = cls(base, sections)
        img.timestamp = struct.unpack_from("<I", blob, pe + 8)[0]
        return img

    def section(self, va):
        for s in self.sections:
            if s[1] <= va < s[1] + len(s[2]):
                return s
        return None

    def contains(self, va, size=1):
        s = self.section(va)
        return s is not None and va + size <= s[1] + len(s[2])

    def read(self, va, size):
        s = self.section(va)
        if s is None or va + size > s[1] + len(s[2]):
            raise ImageError("address 0x%x is outside the image" % va)
        return s[2][va - s[1]:va - s[1] + size]

    def u16(self, va):
        return struct.unpack("<H", self.read(va, 2))[0]

    def u32(self, va):
        return struct.unpack("<I", self.read(va, 4))[0]

    def u64(self, va):
        return struct.unpack("<Q", self.read(va, 8))[0]

    def text(self, va, length):
        """`length` bytes of text at va, followed by a NUL; None when it is not that."""
        if length > MAX_NAME or not self.contains(va, length + 1):
            return None
        raw = self.read(va, length + 1)
        if raw[-1] != 0 or 0 in raw[:-1]:
            return None
        return raw[:-1].decode("utf-8", "replace")

    def slice(self, va):
        """The text of a slice {const char*, u64 length} at va; "" for length 0; None when it is not a slice."""
        ptr, length = struct.unpack("<QQ", self.read(va, 16))
        if length == 0:
            return ""
        return self.text(ptr, length)

    def cstr(self, va, limit=256):
        s = self.section(va)
        if s is None:
            return None
        raw = s[2][va - s[1]:va - s[1] + limit]
        end = raw.find(b"\0")
        return raw[:end].decode("utf-8", "replace") if end >= 0 else None


def looks_like_type(img, va):
    """True when va holds a type descriptor: its qualified name and name are slices of text, a kind is set and the
    alignment is a power of two (or 0)."""
    if va % 8 or not img.contains(va, TYPE_BYTES):
        return False
    qualified = img.slice(va + TYPE_QUALIFIED_NAME)
    name = img.slice(va + TYPE_NAME)
    if not qualified or not name:
        return False
    align = img.u16(va + TYPE_ALIGNMENT)
    return img.u32(va + TYPE_KIND) & 0xFF != 0 and align & (align - 1) == 0


class TypeTable:
    def __init__(self, list_va, count, holders, root_va, groups):
        self.list_va = list_va
        self.count = count
        self.holders = holders        # where {list, count} is stored
        self.root_va = root_va        # the holder that is the registry root (with the groups), or None
        self.groups = groups          # [(name, list_va, count)]


def _pairs(img, names=(".rdata", ".data")):
    """Every 8-aligned {pointer, count} in the data sections whose pointer is an 8-aligned address of the image and
    whose count is a plausible list length: yields (holder_va, pointer, count)."""
    secs = [s for s in img.sections if s[0] in names] or img.sections
    lo = min(s[1] for s in img.sections)
    hi = max(s[1] + len(s[2]) for s in img.sections)
    for name, va, raw in secs:
        n = len(raw) // 8
        if n < 2:
            continue
        words = memoryview(raw[:n * 8]).cast("Q")
        for i in range(n - 1):
            count = words[i + 1]
            if 0 < count <= MAX_TYPES:
                ptr = words[i]
                if lo <= ptr < hi and ptr % 8 == 0:
                    yield va + 8 * i, ptr, count


def _all_types(img, list_va, count):
    if not img.contains(list_va, 8 * count):
        return False
    raw = img.read(list_va, 8 * count)
    seen = set()
    for (v,) in struct.iter_unpack("<Q", raw):
        if v not in seen:
            if not looks_like_type(img, v):
                return False
            seen.add(v)
    return True


def _groups(img, holder):
    """The groups of a root candidate at holder ({types}, {namespaces}, {attributes}, {groups}), or None."""
    if not img.contains(holder + ROOT_GROUPS, 16):
        return None
    gva, n = struct.unpack("<QQ", img.read(holder + ROOT_GROUPS, 16))
    if not 0 < n <= MAX_GROUPS or not img.contains(gva, GROUP_BYTES * n):
        return None
    groups = []
    for i in range(n):
        g = gva + GROUP_BYTES * i
        name = img.slice(g + GROUP_NAME)
        lst, count = struct.unpack("<QQ", img.read(g + GROUP_LIST, 16))
        if name is None or count > MAX_TYPES or (count and not _all_types(img, lst, count)):
            return None
        groups.append((name, lst, count))
    return groups


def find_type_table(img):
    """The game's master list of type descriptors: the longest list of descriptor pointers stored as {list, count} in
    the data sections. Also the registry root that holds it (followed by the namespace, attribute and group lists)."""
    best = None
    holders = {}
    for holder, ptr, count in _pairs(img):
        if best is not None and count < best[1]:
            continue
        if not img.contains(ptr, 8 * count):
            continue
        if not (looks_like_type(img, img.u64(ptr)) and looks_like_type(img, img.u64(ptr + 8 * (count - 1)))):
            continue
        key = (ptr, count)
        if key not in holders:
            if not _all_types(img, ptr, count):
                holders[key] = None
                continue
            holders[key] = []
        if holders[key] is None:
            continue
        holders[key].append(holder)
        if best is None or count > best[1]:
            best = key
    if best is None:
        raise ImageError("no type table found: this exe has no keen type descriptors this reader knows")
    root, groups = None, []
    for h in holders[best]:
        g = _groups(img, h)
        if g is not None:
            root, groups = h, g
            break
    return TypeTable(best[0], best[1], holders[best], root, groups)


class _Reader:
    def __init__(self, img, table):
        self.img = img
        self.order = [img.u64(table.list_va + 8 * i) for i in range(table.count)]
        self.index = {}
        for i, va in enumerate(self.order):
            self.index.setdefault(va, i)
        self.listed = table.count
        self.namespaces = {}

    def ref(self, va):
        """The index of the type at va (types outside the table are added after it); None for a null pointer."""
        if va == 0:
            return None
        i = self.index.get(va)
        if i is None:
            if not looks_like_type(self.img, va):
                raise ImageError("0x%x is used as a type but is not a type descriptor" % va)
            i = self.index[va] = len(self.order)
            self.order.append(va)
        return i

    def namespace_of(self, va):
        """["keen", "ecs"] for the namespace at va: its name after its parents' (a namespace points at its parent)."""
        chain, prefix = [], []
        cur = va
        while cur and len(chain) < 64:
            if cur in self.namespaces:
                prefix = self.namespaces[cur]
                break
            if cur in chain:
                break
            chain.append(cur)
            cur = self.img.u64(cur + NAMESPACE_PARENT)
        names = list(prefix)
        for c in reversed(chain):
            names = names + [self.img.slice(c + NAMESPACE_NAME) or ""]
            self.namespaces[c] = names
        return list(names)

    def attributes(self, va):
        ptr, count = struct.unpack("<QQ", self.img.read(va, 16))
        out = []
        for i in range(count if ptr else 0):
            a = ptr + ATTRIBUTE_BYTES * i
            desc = self.img.u64(a + ATTRIBUTE_DESC)
            name = self.img.slice(desc + ATTRIBUTE_DESC_NAME) if desc else None
            value = self.img.slice(a + ATTRIBUTE_VALUE)
            out.append([name if name is not None else "", value if value is not None else ""])
        return out

    def type(self, i):
        img, va = self.img, self.order[i]
        word = img.u32(va + TYPE_KIND)
        kind = KINDS.get(word & 0xFF, "kind_%d" % (word & 0xFF))
        count = img.u32(va + TYPE_COUNT)
        t = {
            "index": i,
            "name": img.slice(va + TYPE_NAME),
            "impact_name": img.slice(va + TYPE_IMPACT_NAME),
            "qualified_name": img.slice(va + TYPE_QUALIFIED_NAME),
            "namespace": self.namespace_of(img.u64(va + TYPE_NAMESPACE)),
            "kind": kind,
            "flags": word >> 8,
            "size": img.u32(va + TYPE_SIZE),
            "alignment": img.u16(va + TYPE_ALIGNMENT),
            "element_alignment": img.u16(va + TYPE_ELEMENT_ALIGNMENT),
            "hash": img.u32(va + TYPE_HASH),
            "internal_hash": img.u32(va + TYPE_INTERNAL_HASH),
            "inner": self.ref(img.u64(va + TYPE_INNER)),
            "count": count,
        }
        if i >= self.listed:
            t["listed"] = False
        fields = img.u64(va + TYPE_FIELDS)
        if fields and count:
            t["fields"] = []
            for k in range(count):
                f = fields + FIELD_BYTES * k
                field = {"name": img.slice(f + FIELD_NAME), "type": self.ref(img.u64(f + FIELD_TYPE)),
                         "offset": img.u32(f + FIELD_OFFSET)}
                attrs = self.attributes(f + FIELD_ATTRIBUTES)
                if attrs:
                    field["attributes"] = attrs
                t["fields"].append(field)
        values = img.u64(va + TYPE_VALUES)
        if values and count:
            signed = False
            inner = img.u64(va + TYPE_INNER)
            if inner:
                signed = KINDS.get(img.u32(inner + TYPE_KIND) & 0xFF) in SIGNED
            t["values"] = []
            for k in range(count):
                v = values + VALUE_BYTES * k
                value = img.u64(v + VALUE_VALUE)
                if signed and value >= 1 << 63:
                    value -= 1 << 64
                entry = {"name": img.slice(v + VALUE_NAME), "value": value}
                attrs = self.attributes(v + VALUE_ATTRIBUTES)
                if attrs:
                    entry["attributes"] = attrs
                t["values"].append(entry)
        variants = img.u64(va + TYPE_VARIANTS)
        if variants and count:
            t["variants"] = [self.ref(img.u64(variants + 8 * k)) for k in range(count)]
        dptr, dsize = struct.unpack("<QQ", img.read(va + TYPE_DEFAULT, 16))
        if dptr and dsize:
            t["default"] = img.read(dptr, dsize).hex()
        attrs = self.attributes(va + TYPE_ATTRIBUTES)
        if attrs:
            t["attributes"] = attrs
        return t


def read_types(img):
    """{"type_table", "groups", "types"} of the image: the types in the order of the game's table (index = position),
    then any type only reached through another one."""
    table = find_type_table(img)
    r = _Reader(img, table)
    types = []
    i = 0
    while i < len(r.order):  # the list grows when a type outside the table is reached
        types.append(r.type(i))
        i += 1
    groups = {}
    for name, lst, count in table.groups:
        if name:
            groups[name] = [r.ref(img.u64(lst + 8 * k)) for k in range(count)]
    return {
        "type_table": {"va": table.list_va, "rva": table.list_va - img.image_base, "count": table.count,
                       "root_rva": None if table.root_va is None else table.root_va - img.image_base},
        "groups": groups,
        "types": types,
    }


VERSION = re.compile(r"^\d+(\.\d+)+$")
BRANCH = re.compile(r"^[\w./-]{1,64}$")


def find_build_info(img):
    """The game's build record: a pointer to its git commit (40 hex digits), followed 0x10 bytes later by pointers to
    its version ("0.0.15.0") and branch ("master"). Missing parts are None."""
    info = {"git_commit": None, "version": None, "branch": None}
    strings = {}
    for name, va, raw in img.sections:
        for m in re.finditer(rb"(?<=\0)[0-9a-f]{40}\0", raw):
            strings[va + m.start()] = raw[m.start():m.start() + 40].decode()
    if not strings:
        return info
    for name, va, raw in sorted((s for s in img.sections if s[0] in (".data", ".rdata")), key=lambda s: s[0]):
        n = len(raw) // 8
        words = memoryview(raw[:n * 8]).cast("Q")
        for i in range(n):
            commit = strings.get(words[i])
            if commit is None:
                continue
            info["git_commit"] = commit
            if i + 3 < n:
                version = img.cstr(words[i + 2])
                branch = img.cstr(words[i + 3])
                info["version"] = version if version and VERSION.match(version) else None
                info["branch"] = branch if branch and BRANCH.match(branch) else None
            return info
    return info


def read_exe(path):
    """EMF's types document for the exe at path."""
    with open(path, "rb") as f:
        blob = f.read()
    img = Image.from_pe(blob)
    sha = hashlib.sha256(blob).hexdigest()
    doc = {"format": FORMAT, "format_version": FORMAT_VERSION}
    source = {"file": os.path.basename(path), "sha256": sha, "size": len(blob),
              "pe_timestamp": img.timestamp, "image_base": img.image_base,
              "build": KNOWN_BUILDS.get(sha)}
    source.update(find_build_info(img))
    doc["source"] = source
    doc.update(read_types(img))
    return doc


def write_types(doc, path):
    """One type (and one group) per line, so the file stays readable and diffs stay small."""
    head = {k: v for k, v in doc.items() if k not in ("groups", "types")}
    text = json.dumps(head, indent=1)
    text = text[:text.rindex("}")].rstrip()  # reopen the object
    groups = ",\n".join("  %s: %s" % (json.dumps(name), json.dumps(members, separators=(",", ":")))
                        for name, members in (doc.get("groups") or {}).items())
    types = ",\n".join("  " + json.dumps(t, separators=(",", ":")) for t in doc["types"])
    text += ',\n "groups": {\n' + groups + "\n },\n" if groups else ',\n "groups": {},\n'
    text += ' "types": [\n' + types + "\n ]\n}\n"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def default_exe():
    game = os.environ.get("EMF_GAME_DIR")
    if not game:
        sys.exit("exe_types.py: which exe? Pass --exe <path to enshrouded.exe>, or set EMF_GAME_DIR to the game folder.")
    return os.path.join(game, "enshrouded.exe")


def describe(source):
    """One line naming the exe a types document came from."""
    parts = [source.get("file") or "exe"]
    if source.get("build"):
        parts.append("build %d" % source["build"])
    if source.get("version"):
        parts.append("version %s" % source["version"])
    if source.get("git_commit"):
        parts.append("git %s" % source["git_commit"][:12])
    parts.append("sha256 %s" % source.get("sha256", "?"))
    return ", ".join(parts)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Write EMF's types file from the game's reflection data.")
    ap.add_argument("--exe", help="enshrouded.exe or enshrouded_server.exe (default: EMF_GAME_DIR's enshrouded.exe)")
    ap.add_argument("-o", "--out", default="types.emf.json", help="the types file to write")
    args = ap.parse_args(argv)
    exe = args.exe or default_exe()
    try:
        doc = read_exe(exe)
    except (ImageError, OSError) as e:
        sys.exit("exe_types.py: %s: %s" % (exe, e))
    write_types(doc, args.out)
    print("wrote %s: %d types (%d in the game's table, %d groups) from %s" % (
        args.out, len(doc["types"]), doc["type_table"]["count"], len(doc["groups"]), describe(doc["source"])))


if __name__ == "__main__":
    main()
