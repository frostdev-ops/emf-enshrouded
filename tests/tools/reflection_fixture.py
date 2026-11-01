"""Builds small fake game images that hold keen type descriptors laid out the way enshrouded.exe stores them
(docs/engine_parts/re_reflection.md), for the tests of tools/re/exe_types.py and tools/gen_headers.py.

No game bytes: every descriptor, name and hash here is made up; only the layout follows the game's."""

import struct

IMAGE_BASE = 0x140000000
RDATA_RVA = 0x2000
DATA_RVA = 0x80000
TYPE_SIZE = 0x98

KIND = {"bool": 0x01, "uint8": 0x02, "sint8": 0x03, "uint16": 0x04, "sint16": 0x05, "uint32": 0x06, "sint32": 0x07,
        "uint64": 0x08, "sint64": 0x09, "float32": 0x0A, "enum": 0x0C, "bitmask8": 0x0D, "bitmask16": 0x0E,
        "bitmask32": 0x0F, "bitmask64": 0x10, "typedef": 0x11, "struct": 0x12, "static_array": 0x13,
        "ds_array": 0x14, "ds_string": 0x15, "ds_optional": 0x16, "ds_variant": 0x17, "blob_array": 0x18,
        "blob_string": 0x19, "blob_optional": 0x1A, "blob_variant": 0x1B, "object_reference": 0x1C, "guid": 0x1D}


class Section:
    def __init__(self, name, rva):
        self.name = name
        self.rva = rva
        self.buf = bytearray()

    @property
    def va(self):
        return IMAGE_BASE + self.rva

    def alloc(self, size, align=8):
        while len(self.buf) % align:
            self.buf.append(0)
        at = self.va + len(self.buf)
        self.buf += bytes(size)
        return at

    def put(self, va, fmt, *values):
        struct.pack_into("<" + fmt, self.buf, va - self.va, *values)


class ImageBuilder:
    """Lay out namespaces, types, a master type list and the root table, then take .image() or .pe_bytes()."""

    def __init__(self):
        self.rdata = Section(".rdata", RDATA_RVA)
        self.data = Section(".data", DATA_RVA)
        self.rdata.alloc(16)  # keep address 0 of the section unused
        self.empty = self.string("")
        self._strings = {}

    # --- raw pieces ---
    def string(self, text, section=None):
        sec = section or self.rdata
        raw = text.encode("utf-8") + b"\0"
        at = sec.alloc(len(raw), 1)
        sec.buf[at - sec.va:at - sec.va + len(raw)] = raw
        return at

    def slice(self, at, text, section=None):
        """{const char*, u64 length} at `at`."""
        sec = section or self.rdata
        if text is None:
            sec.put(at, "QQ", 0, 0)
            return
        if text == "":
            sec.put(at, "QQ", self.empty, 0)
            return
        if text not in self._strings:
            self._strings[text] = self.string(text)
        sec.put(at, "QQ", self._strings[text], len(text.encode("utf-8")))

    def pointers(self, vas, section=None):
        sec = section or self.rdata
        at = sec.alloc(8 * max(len(vas), 1))
        for i, v in enumerate(vas):
            sec.put(at + 8 * i, "Q", v)
        return at

    # --- reflection objects ---
    def namespace(self, name, parent=0):
        at = self.rdata.alloc(0x60)
        self.slice(at, name)
        self.rdata.put(at + 0x10, "Q", parent)
        return at

    def attribute_desc(self, name, namespace=0):
        at = self.rdata.alloc(0x60)
        self.rdata.put(at, "Q", namespace)
        self.slice(at + 0x08, name)
        return at

    def attributes(self, pairs):
        """[(attribute_desc, value or None)] -> (pointer, count)."""
        if not pairs:
            return 0, 0
        at = self.rdata.alloc(0x18 * len(pairs))
        for i, (desc, value) in enumerate(pairs):
            self.rdata.put(at + 0x18 * i, "Q", desc)
            self.slice(at + 0x18 * i + 8, value)
        return at, len(pairs)

    def new_type(self):
        """Reserve a descriptor (so types can point at each other), fill it with set_type."""
        return self.rdata.alloc(TYPE_SIZE)

    def set_type(self, at, qualified, kind, size=0, align=1, elem_align=None, namespace=0, inner=0, count=None,
                 fields=(), values=(), variants=(), default=None, attributes=(), hash=0, internal_hash=0,
                 flags=0, name=None, impact_name=None):
        """fields: [(name, type_va, offset, attributes)], values: [(name, value)], variants: [type_va]."""
        short = name if name is not None else qualified.split("::")[-1]
        self.slice(at + 0x00, short)
        self.slice(at + 0x10, impact_name if impact_name is not None else short)
        self.slice(at + 0x20, qualified)
        self.rdata.put(at + 0x30, "QQ", namespace, inner)
        self.rdata.put(at + 0x40, "IHH", size, align, elem_align if elem_align is not None else align)
        if count is None:
            count = len(fields) or len(values) or len(variants)
        self.rdata.put(at + 0x48, "II", count, (flags << 8) | (KIND[kind] if isinstance(kind, str) else kind))
        self.rdata.put(at + 0x50, "II", hash, internal_hash)
        if fields:
            f = self.rdata.alloc(0x30 * len(fields))
            for i, field in enumerate(fields):
                fname, ftype, offset = field[:3]
                attrs = self.attributes(field[3] if len(field) > 3 else ())
                self.slice(f + 0x30 * i, fname)
                self.rdata.put(f + 0x30 * i + 0x10, "QIIQQ", ftype, offset, 0, *attrs)
            self.rdata.put(at + 0x58, "Q", f)
        if values:
            v = self.rdata.alloc(0x28 * len(values))
            for i, (vname, value) in enumerate(values):
                self.slice(v + 0x28 * i, vname)
                self.rdata.put(v + 0x28 * i + 0x10, "QQQ", value & 0xFFFFFFFFFFFFFFFF, 0, 0)
            self.rdata.put(at + 0x60, "Q", v)
        if variants:
            self.rdata.put(at + 0x68, "Q", self.pointers(list(variants)))
        if default is not None:
            d = self.rdata.alloc(len(default))
            self.rdata.buf[d - self.rdata.va:d - self.rdata.va + len(default)] = default
            self.rdata.put(at + 0x70, "QQ", d, len(default))
        self.rdata.put(at + 0x80, "QQ", *self.attributes(list(attributes)))
        return at

    def type(self, qualified, kind, **kw):
        return self.set_type(self.new_type(), qualified, kind, **kw)

    def type_list(self, types):
        return self.pointers(types)

    def root(self, types, namespaces=(), attribute_descs=(), groups=()):
        """The registry root: {types, namespaces, attribute descriptors, groups} as (pointer, count) pairs; a group is
        (name, [type_va]). The first group is the unnamed one with every type, as in the game."""
        type_list = self.type_list(types)
        group_rows = [("", type_list, len(types))]
        for gname, members in groups:
            group_rows.append((gname, self.type_list(members), len(members)))
        g = self.rdata.alloc(0x20 * len(group_rows))
        for i, (gname, lst, n) in enumerate(group_rows):
            self.slice(g + 0x20 * i, gname)
            self.rdata.put(g + 0x20 * i + 0x10, "QQ", lst, n)
        at = self.rdata.alloc(0x40)
        self.rdata.put(at, "QQQQQQQQ", type_list, len(types), self.pointers(list(namespaces)), len(namespaces),
                       self.pointers(list(attribute_descs)), len(attribute_descs), g, len(group_rows))
        return at, type_list

    def build_info(self, commit, version, branch, product="enshrouded"):
        """The game's build record in .data: pointers to the git commit, version and branch strings."""
        c, v, b, p = (self.string(s) for s in (commit, version, branch, product))
        r = self.string("detached HEAD")
        at = self.data.alloc(0x60)
        self.data.put(at + 0x28, "QIIQQIIQQ", c, len(commit), len(version), v, b, len(branch), len(product), p, r)
        return at

    # --- results ---
    def sections(self):
        return [(s.name, s.rva, bytes(s.buf)) for s in (self.rdata, self.data)]

    def image(self):
        import exe_types
        return exe_types.Image(IMAGE_BASE, [(name, IMAGE_BASE + rva, raw) for name, rva, raw in self.sections()])

    def pe_bytes(self, timestamp=0x6A000000):
        """A minimal PE32+ file with the two sections (enough for exe_types.Image.from_pe)."""
        secs = self.sections()
        file_align = 0x200
        headers = 0x400
        e_lfanew = 0x80
        opt_size = 0xF0
        out = bytearray(headers)
        out[0:2] = b"MZ"
        struct.pack_into("<I", out, 0x3C, e_lfanew)
        out[e_lfanew:e_lfanew + 4] = b"PE\0\0"
        struct.pack_into("<HHIIIHH", out, e_lfanew + 4, 0x8664, len(secs), timestamp, 0, 0, opt_size, 0x22)
        opt = e_lfanew + 24
        struct.pack_into("<H", out, opt, 0x20B)
        struct.pack_into("<Q", out, opt + 24, IMAGE_BASE)
        raw_at = headers
        table = opt + opt_size
        body = bytearray()
        for i, (name, rva, raw) in enumerate(secs):
            size = (len(raw) + file_align - 1) // file_align * file_align
            entry = table + 40 * i
            out[entry:entry + 8] = name.encode().ljust(8, b"\0")
            struct.pack_into("<IIII", out, entry + 8, len(raw), rva, size, raw_at + len(body))
            body += raw + bytes(size - len(raw))
        return bytes(out + body)


def small_game():
    """A few types of each kind the readers care about, with a decoy list; returns (builder, names -> descriptor)."""
    b = ImageBuilder()
    keen = b.namespace("keen")
    ecs = b.namespace("ecs", keen)
    t = {}
    for qn, kind, size in (("keen::bool", "bool", 1), ("keen::uint8", "uint8", 1), ("keen::sint8", "sint8", 1),
                           ("keen::uint32", "uint32", 4), ("keen::sint64", "sint64", 8),
                           ("keen::float32", "float32", 4)):
        t[qn] = b.type(qn, kind, size=size, align=size, namespace=keen, hash=0x1000 + size, internal_hash=0x2000)
    abstract = b.attribute_desc("abstract", keen)
    server_only = b.attribute_desc("server_only", keen)
    description = b.attribute_desc("description", keen)
    t["keen::Guid"] = b.type("keen::Guid", "guid", size=16, align=4, namespace=keen, hash=0x47554944)
    t["keen::WorldPosition"] = b.type(
        "keen::WorldPosition", "struct", size=24, align=8, namespace=keen, hash=0xF99C5535, internal_hash=0xC3897681,
        fields=[("x", t["keen::sint64"], 0), ("y", t["keen::sint64"], 8), ("z", t["keen::sint64"], 16)])
    t["keen::ecs::Component"] = b.type("keen::ecs::Component", "struct", size=1, align=1, namespace=ecs,
                                       hash=0xB4B5CB62, internal_hash=0xFA4D6974, impact_name="ecs.Component",
                                       attributes=[(abstract, None)])
    t["keen::ecs::FactionType"] = b.type(
        "keen::ecs::FactionType", "enum", size=1, align=1, namespace=ecs, inner=t["keen::uint8"], hash=0x11,
        values=[("Environment", 0), ("HitAll", 1), ("Player", 7)])
    t["keen::ecs::Signed"] = b.type("keen::ecs::Signed", "enum", size=1, align=1, namespace=ecs,
                                    inner=t["keen::sint8"], hash=0x12, values=[("Minus", -1), ("Zero", 0)])
    t["keen::Bitmask8<keen::ecs::FactionType>"] = b.type(
        "keen::Bitmask8<keen::ecs::FactionType>", "bitmask8", size=1, align=1, namespace=keen,
        inner=t["keen::ecs::FactionType"], hash=0x13)
    t["keen::HashKey32"] = b.type("keen::HashKey32", "typedef", size=4, align=4, namespace=keen,
                                  inner=t["keen::uint32"], hash=0x14)
    t["keen::StaticArray<keen::uint8,3>"] = b.type("keen::StaticArray<keen::uint8,3>", "static_array", size=3,
                                                   align=1, namespace=keen, inner=t["keen::uint8"], count=3,
                                                   hash=0x15)
    t["keen::BlobString"] = b.type("keen::BlobString", "blob_string", size=8, align=4, namespace=keen, hash=0x16,
                                   flags=0x04)
    pos = b.new_type()  # the component is laid out before the type it derives from is filled, as types refer ahead
    t["keen::ecs::Position"] = pos
    t["keen::ecs::Health"] = b.type(
        "keen::ecs::Health", "struct", size=12, align=4, namespace=ecs, inner=t["keen::ecs::Component"],
        hash=0x027AC564, internal_hash=0x6E9F416E, impact_name="ecs.Health",
        fields=[("value", t["keen::float32"], 0, [(description, "Current health")]),
                ("faction", t["keen::ecs::FactionType"], 4), ("tag", t["keen::HashKey32"], 8)],
        attributes=[(server_only, None), (description, "Hit points")], default=bytes([0, 0, 0x80, 0x3F]) + bytes(8))
    b.set_type(pos, "keen::ecs::Position", "struct", size=24, align=8, namespace=ecs,
               inner=t["keen::ecs::Component"], hash=0x0A0B0C0D, internal_hash=0x01020304,
               fields=[("position", t["keen::WorldPosition"], 0)])
    t["keen::BlobVariant<keen::ecs::Component>"] = b.type(
        "keen::BlobVariant<keen::ecs::Component>", "blob_variant", size=12, align=4, namespace=keen,
        inner=t["keen::ecs::Component"], variants=[t["keen::ecs::Health"], t["keen::ecs::Position"]], hash=0x17)
    # A type no list names, reached only through a field: the reader adds it after the listed ones.
    hidden = b.type("keen::ecs::Hidden", "struct", size=4, align=4, namespace=ecs, hash=0x18,
                    fields=[("count", t["keen::uint32"], 0)])
    t["keen::ecs::Hidden"] = hidden
    t["keen::ecs::UsesHidden"] = b.type("keen::ecs::UsesHidden", "struct", size=4, align=4, namespace=ecs,
                                        hash=0x19, fields=[("hidden", hidden, 0)])
    order = [v for k, v in t.items() if k != "keen::ecs::Hidden"]
    b.pointers(order[:3])  # a decoy: a short list of the same descriptors
    b.root(order, namespaces=[keen, ecs], attribute_descs=[abstract, server_only, description],
           groups=[("ecs.component", [t["keen::ecs::Health"], t["keen::ecs::Position"]])])
    return b, t, order
