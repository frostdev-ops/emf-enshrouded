"""Generate sdk/include/emf/generated/components.hpp (and reflection.hpp) from the game's reflection data.

Emits every ECS component (types deriving from keen::ecs::Component), every keen::ecs::Dynamic* struct,
every ECS event (types deriving from keen::ecs::GameEvent or keen::ecs::Event, with kName = short name),
and the value types they use, as byte-exact packed structs with static_asserts on size and field offsets.

Usage: python tools/gen_headers.py [source] [out.hpp]
source: the game's enshrouded.exe (read with tools/re/exe_types.py), the types file exe_types.py writes, or EML's
types.json. Without a source: enshrouded.exe in the game folder EMF_GAME_DIR names, else a plain error.
The generator works on EMF's types format (see tools/re/exe_types.py); EML's types.json is converted to it on load.
"""
import json
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "re"))
import exe_types  # noqa: E402

DEFAULT_OUT = Path(__file__).resolve().parent.parent / "sdk" / "include" / "emf" / "generated" / "components.hpp"
COMPONENT_BASE_NAME = "keen::ecs::Component"
EVENT_BASE_NAMES = ("keen::ecs::GameEvent", "keen::ecs::Event")

PRIMS = {
    "bool": "bool", "uint8": "uint8_t", "sint8": "int8_t", "uint16": "uint16_t", "sint16": "int16_t",
    "uint32": "uint32_t", "sint32": "int32_t", "uint64": "uint64_t", "sint64": "int64_t",
    "float32": "float", "float64": "double",
}
BITMASKS = {"bitmask8": "uint8_t", "bitmask16": "uint16_t", "bitmask32": "uint32_t", "bitmask64": "uint64_t"}
CPP_KEYWORDS = set("""alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t
char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield
decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if
inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public
register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch
template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile
wchar_t while xor xor_eq""".split())
# Common Windows / CRT macros that would break identifiers when windows.h is included first.
WIN_MACROS = set("""DELETE ERROR IN OUT OPTIONAL TRUE FALSE VOID CONST NEAR FAR PASCAL CALLBACK WINAPI interface
small min max near far ABSOLUTE RELATIVE TRANSPARENT OPAQUE ALTERNATE WINDING BLACKNESS WHITENESS SYNCHRONIZE
STANDARD_RIGHTS_READ IGNORE INFINITE DOMAIN OVERFLOW UNDERFLOW PLOSS TLOSS SING EOF NULL BOOL BYTE WORD DWORD
INT UINT LONG ULONG SHORT USHORT CHAR UCHAR FLOAT DOUBLE HANDLE MOUSE_MOVED DOUBLE_CLICK MOUSE_WHEELED
KEY_EVENT MOUSE_EVENT FOCUS_EVENT MENU_EVENT FILE_DEVICE_MASS_STORAGE IDLE_PRIORITY_CLASS HIGH_PRIORITY_CLASS
NORMAL_PRIORITY_CLASS REALTIME_PRIORITY_CLASS PURGE GENERIC_READ GENERIC_WRITE GENERIC_ALL DIFFERENCE RGB
GetObject GetMessage SendMessage PostMessage CreateWindow LoadImage DrawText GetClassName CreateFont LoadIcon
LoadCursor LoadBitmap LoadString CreateEvent CreateMutex CreateSemaphore OpenEvent SetProp GetProp RemoveProp
Yield PlaySound CopyFile MoveFile DeleteFile CreateFile CreateDirectory RemoveDirectory GetCurrentTime
NOERROR S_OK S_FALSE E_FAIL FAILED SUCCEEDED THIS RELEASE CONTEXT_FULL REG_NONE AF_UNSPEC FILE_ATTRIBUTE_NORMAL
DM_COPY DM_UPDATE DM_IN_BUFFER DM_OUT_BUFFER OF_READ OF_WRITE IDOK IDCANCEL IDABORT IDRETRY IDIGNORE IDYES IDNO
MB_OK HELP_INDEX CAPTUREBLT MIRROR_BITMAP NOMIRRORBITMAP SRCCOPY SRCPAINT DSTINVERT PATCOPY POINT RECT SIZE
NotEqual Equal Always Never Less Greater""".split())


def ident(name):
    s = re.sub(r"[^0-9A-Za-z_]", "_", name)
    if not s or s[0].isdigit():
        s = "_" + s
    if s in CPP_KEYWORDS or s in WIN_MACROS or s.startswith("__"):
        s += "_"
    return s


def has_attribute(t, name):
    return any(a[0] == name for a in t.get("attributes") or ())


class Gen:
    def __init__(self, types):
        self.by_index = {t["index"]: t for t in types}
        self.emitted = set()
        self.out = []
        self.short_names = set()  # types that get a kName (events)
        base = [t["index"] for t in types if t["qualified_name"] == COMPONENT_BASE_NAME]
        self.component_base = base[0] if base else None

    def cpp_name(self, t):
        ns = [ident(n) for n in t["namespace"][1:]]  # drop leading keen
        return "::".join(["emf", "gen"] + ns + [ident(t["name"])])

    def resolve(self, idx):
        """Returns (cpp_type, dims, dep_index) for a field type, or None for an opaque type."""
        t = self.by_index.get(idx)
        if t is None:
            return None
        k = t.get("kind")
        if k in PRIMS:
            return PRIMS[k], [], None
        if k in BITMASKS:
            return BITMASKS[k], [], None
        if k == "typedef":
            return self.resolve(t.get("inner")) if t.get("inner") is not None else None
        if k in ("guid", "object_reference") and t["size"] == 16:
            return "::emf::gen::Guid", [], None
        if k == "enum":
            return self.cpp_name(t), [], idx
        if k == "struct":
            return self.cpp_name(t), [], idx
        if k == "static_array":
            elem = self.by_index.get(t.get("inner"))
            inner = self.resolve(t.get("inner"))
            if inner is None or elem is None or elem["size"] == 0:
                return None
            count = t.get("count") or (t["size"] // elem["size"])
            if count * elem["size"] != t["size"]:
                return None
            ctype, dims, dep = inner
            return ctype, [count] + dims, dep
        return None  # BlobArray, DS types, variants, strings: opaque bytes

    def fields_of(self, t):
        """All fields including those of non-empty base structs, sorted by offset."""
        fields = []
        base = self.by_index.get(t.get("inner")) if t.get("kind") == "struct" else None
        if base is not None and base.get("kind") == "struct" and base["index"] != self.component_base:
            fields += self.fields_of(base)
        fields += list(t.get("fields") or ())
        return sorted(fields, key=lambda f: f["offset"])

    def emit(self, idx):
        if idx in self.emitted:
            return
        t = self.by_index[idx]
        self.emitted.add(idx)
        k = t.get("kind")
        if k == "enum":
            self.emit_enum(t)
        elif k == "struct":
            # dependencies first
            for f in self.fields_of(t):
                r = self.resolve(f["type"])
                if r and r[2] is not None:
                    self.emit(r[2])
            self.emit_struct(t)

    def open_ns(self, t):
        ns = [ident(n) for n in t["namespace"][1:]]
        return "namespace emf::gen" + "".join("::" + n for n in ns) + " {"

    def emit_enum(self, t):
        under = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t", 8: "uint64_t"}.get(t["size"], "uint32_t")
        inner = self.by_index.get(t.get("inner"))
        if inner is not None and inner.get("kind") in PRIMS:
            under = PRIMS[inner["kind"]]
        values = sorted(t.get("values") or (), key=lambda v: v["value"])
        lines = [self.open_ns(t), f"enum class {ident(t['name'])} : {under} {{"]
        seen = set()
        for v in values:
            n = ident(v["name"])
            while n in seen:
                n += "_"
            seen.add(n)
            lines.append(f"    {n} = {v['value']},")
        lines += ["};", "}", ""]
        self.out.append("\n".join(lines))

    def emit_struct(self, t):
        name = ident(t["name"])
        size = t["size"]
        lines = [self.open_ns(t), f"// {t['qualified_name']}", f"struct {name} {{"]
        lines.append(f"    static constexpr uint32_t kHash = 0x{t['hash']:08x}u;")
        lines.append(f"    static constexpr uint32_t kInternalHash = 0x{t['internal_hash']:08x}u;")
        lines.append(f"    static constexpr uint32_t kSize = {size};")
        if t["index"] in self.short_names:
            lines.append(f"    static constexpr const char* kName = \"{t['name']}\";")
        cur = 0
        asserts = []
        used = set()
        pad = 0
        for f in self.fields_of(t):
            off = f["offset"]
            ft = self.by_index.get(f["type"])
            fsize = ft["size"] if ft else 0
            if off < cur or fsize == 0 or off + fsize > size:
                continue  # overlapping or empty: skip, keep layout exact via padding
            if off > cur:
                lines.append(f"    uint8_t _pad{pad}[{off - cur}];")
                pad += 1
            fname = ident(f["name"])
            while fname in used or fname in ("kHash", "kInternalHash", "kSize", "kName"):
                fname += "_"
            used.add(fname)
            r = self.resolve(f["type"])
            if r is None:
                lines.append(f"    uint8_t {fname}[{fsize}];  // {ft['qualified_name']}")
            else:
                ctype, dims, _ = r
                lines.append(f"    {ctype} {fname}{''.join(f'[{d}]' for d in dims)};")
            asserts.append(f"static_assert(offsetof({self.cpp_name(t)}, {fname}) == {off});")
            cur = off + fsize
        if cur < size:
            lines.append(f"    uint8_t _pad{pad}[{size - cur}];")
        if size == 0:
            lines.append("    // empty")
        lines += ["};", "}", f"static_assert(sizeof({self.cpp_name(t)}) == {size});"] + asserts + [""]
        self.out.append("\n".join(lines))


REFLECT_KINDS = {"bool": "Bool", "uint8_t": "U8", "int8_t": "I8", "uint16_t": "U16", "int16_t": "I16",
                 "uint32_t": "U32", "int32_t": "I32", "uint64_t": "U64", "int64_t": "I64", "float": "F32",
                 "double": "F64"}


def reflection(gen, components, dynamics, events, generated_line):
    """sdk/include/emf/generated/reflection.hpp: every emitted struct with its fields (names, offsets, kinds), so
    tools and script hosts can read and write fields by name."""
    structs = [i for i in sorted(gen.emitted, key=lambda i: gen.by_index[i]["qualified_name"])
               if gen.by_index[i].get("kind") == "struct"]
    slot = {idx: n for n, idx in enumerate(structs)}
    comp = {t["index"] for t in components} | {t["index"] for t in dynamics}
    evts = {t["index"] for t in events}
    type_lines, field_lines = [], []
    nfield = 0
    for idx in structs:
        t = gen.by_index[idx]
        first, count, cur, used = nfield, 0, 0, set()
        for f in gen.fields_of(t):
            off = f["offset"]
            ft = gen.by_index.get(f["type"])
            fsize = ft["size"] if ft else 0
            if off < cur or fsize == 0 or off + fsize > t["size"]:
                continue
            fname = ident(f["name"])
            while fname in used or fname in ("kHash", "kInternalHash", "kSize", "kName"):
                fname += "_"
            used.add(fname)
            r = gen.resolve(f["type"])
            kind, ref, elems = "Bytes", 0xFFFF, 1
            if r is not None:
                ctype, dims, dep = r
                for d in dims:
                    elems *= d
                if ctype == "::emf::gen::Guid":
                    kind = "Guid"
                elif ctype in REFLECT_KINDS:
                    kind = REFLECT_KINDS[ctype]
                elif dep is not None and gen.by_index[dep].get("kind") == "struct":
                    kind, ref = "Struct", slot[dep]
                elif dep is not None:  # an enum: its underlying integer
                    e = gen.by_index[dep]
                    under = {1: "U8", 2: "U16", 4: "U32", 8: "U64"}.get(e["size"], "U32")
                    inner = gen.by_index.get(e.get("inner"))
                    if inner is not None and inner.get("kind") in PRIMS:
                        under = REFLECT_KINDS[PRIMS[inner["kind"]]]
                    kind = under
            field_lines.append(f'    {{"{f["name"]}", {off}, {fsize}, Kind::{kind}, 0x{ref:04x}, {elems}}},')
            nfield += 1
            count += 1
            cur = off + fsize
        flags = (1 if idx in comp else 0) | (2 if idx in evts else 0)
        type_lines.append(f'    {{"{t["qualified_name"]}", "{t["name"]}", 0x{t["hash"]:08x}u, {t["size"]}, '
                          f'{first}, {count}, {flags}}},')
    head = [
        generated_line,
        "// Every struct of components.hpp with its fields, for reading and writing fields by name at run time.",
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace emf::gen::reflect {",
        "enum class Kind : uint8_t { Bool, U8, I8, U16, I16, U32, I32, U64, I64, F32, F64, Struct, Guid, Bytes };",
        "struct Field {",
        "    const char* name;",
        "    uint32_t offset, size;  // size of the whole field (all array elements)",
        "    Kind kind;              // enums appear as their integer kind",
        "    uint16_t type;          // Struct: index into kTypes, else 0xffff",
        "    uint32_t count;         // array elements (1 = not an array)",
        "};",
        "struct Type {",
        "    const char* name;        // keen::ecs::CurrentTransform",
        "    const char* short_name;  // CurrentTransform",
        "    uint32_t hash, size;",
        "    uint32_t first_field, field_count;  // into kFields",
        "    uint32_t flags;          // 1 = a component (or a Dynamic half), 2 = an event",
        "};",
        f"inline constexpr uint32_t kTypeCount = {len(type_lines)};",
        f"inline constexpr uint32_t kFieldCount = {max(nfield, 1)};",
        "inline constexpr Type kTypes[kTypeCount] = {",
    ]
    empty_field = '    {"", 0, 0, Kind::Bytes, 0xffff, 1},'
    tail = ["};", "inline constexpr Field kFields[kFieldCount] = {"] + (field_lines or [empty_field]) + \
           ["};", "}  // namespace emf::gen::reflect", ""]
    return "\n".join(head + type_lines + tail)


def from_eml(data):
    """EML's types.json in EMF's types format (the one tools/re/exe_types.py writes)."""
    types = []
    for t in data["types"]:
        e = {"index": t["index"], "name": t["name"], "qualified_name": t["qualifiedName"],
             "namespace": t.get("namespace") or [], "kind": (t.get("primitiveType") or "").lower(),
             "size": t["size"], "hash": t.get("qualifiedHash", 0), "internal_hash": t.get("internalHash", 0),
             "inner": t.get("innerType"), "count": t.get("fieldCount") or 0}
        if t.get("structFields"):
            e["fields"] = [{"name": f["name"], "type": f["type"], "offset": f["dataOffset"]}
                           for f in t["structFields"].values()]
        if t.get("enumFields"):
            e["values"] = [{"name": v["name"], "value": v["value"]} for v in t["enumFields"].values()]
        if t.get("attributes"):
            e["attributes"] = [[name, ""] for name in t["attributes"]]
        types.append(e)
    return {"format": "eml-types", "source": {"eml_version": data.get("version", "?")}, "types": types}


def load(path):
    """The types of a source: an exe (read with exe_types.py), EMF's types file, or EML's types.json. A source that
    cannot be read stops the run with one line saying why."""
    try:
        with open(path, "rb") as f:
            magic = f.read(2)
        if magic == b"MZ":
            doc = exe_types.read_exe(path)
        else:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                raise ValueError("not a JSON object")
            if data.get("format") == exe_types.FORMAT:
                if data.get("format_version", 0) > exe_types.FORMAT_VERSION:
                    sys.exit(f"gen_headers.py: {path} is a newer types file (format {data.get('format_version')}) "
                             f"than this generator reads ({exe_types.FORMAT_VERSION}); update EMF's tools.")
                doc = data
            else:
                doc = from_eml(data)
    except OSError as e:
        sys.exit(f"gen_headers.py: cannot read {path}: {e.strerror or e}")
    except exe_types.ImageError as e:
        sys.exit(f"gen_headers.py: {path}: {e}")
    except ValueError as e:  # not JSON, not UTF-8
        sys.exit(f"gen_headers.py: {path} is neither the game's exe nor a types file ({e})")
    except (KeyError, TypeError) as e:
        sys.exit(f"gen_headers.py: {path} is not a types file (no {e})")
    if (doc.get("type_table") or {}).get("root_rva", 0) is None:
        sys.exit(f"gen_headers.py: {path}: the game's type registry root was not found next to its type list, so the "
                 f"layout may have changed (docs/engine_parts/re_reflection.md); nothing was written.")
    return doc


def source_lines(doc):
    """The first line of each header and the line naming the source."""
    if doc.get("format") == "eml-types":
        return ("// GENERATED by tools/gen_headers.py from EML types.json. Do not edit.",
                f"// Source version: {doc['source'].get('eml_version', '?')}")
    return ("// GENERATED by tools/gen_headers.py from the game's reflection data (tools/re/exe_types.py). "
            "Do not edit.", "// Source: " + exe_types.describe(doc.get("source") or {}))


def generate(doc, out_path):
    """Writes components.hpp at out_path and reflection.hpp next to it from a types document (EMF's format)."""
    out_path = Path(out_path)
    types = [t for t in doc["types"] if not t["qualified_name"].startswith("keen::ds::")]
    gen = Gen(types)

    def derives_from_component(t):
        cur, seen = t, 0
        while cur is not None and cur.get("kind") == "struct" and seen < 32 and gen.component_base is not None:
            if cur.get("inner") == gen.component_base:
                return True
            cur = gen.by_index.get(cur.get("inner"))
            seen += 1
        return False

    if gen.component_base is None:
        sys.exit(f"gen_headers.py: the types have no {COMPONENT_BASE_NAME}, so there are no components; nothing was "
                 f"written (is the source the game's exe?)")
    # Components derive from keen::ecs::Component directly or through a base (Actor <- BaseActor, Health <- FloatAttribute).
    component_like = [t for t in types if t.get("kind") == "struct" and derives_from_component(t)]
    components = sorted((t for t in component_like if not has_attribute(t, "abstract")),
                        key=lambda t: t["qualified_name"])
    if not components:
        sys.exit(f"gen_headers.py: no components found (no type derives from {COMPONENT_BASE_NAME}); nothing was "
                 f"written")
    abstract_bases = sorted((t for t in component_like if has_attribute(t, "abstract")),
                            key=lambda t: t["qualified_name"])
    dynamics = sorted((t for t in types if t["qualified_name"].startswith("keen::ecs::Dynamic")
                       and t.get("kind") == "struct"), key=lambda t: t["qualified_name"])
    by_name = {t["qualified_name"]: t for t in types}
    event_bases = {by_name[n]["index"] for n in EVENT_BASE_NAMES if n in by_name}

    def is_event(t):
        seen = 0
        cur = t
        while cur is not None and cur.get("kind") == "struct" and seen < 32:
            base = cur.get("inner")
            if base in event_bases:
                return True
            cur = gen.by_index.get(base)
            seen += 1
        return False

    events = sorted((t for t in types if t["qualified_name"].startswith("keen::ecs::") and t["index"] not in event_bases
                     and t.get("kind") == "struct" and is_event(t)), key=lambda t: t["qualified_name"])
    gen.short_names = {t["index"] for t in events}
    for t in abstract_bases + components + dynamics + events:
        gen.emit(t["index"])

    generated_line, source_line = source_lines(doc)
    head = [
        generated_line,
        source_line,
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace emf::gen {",
        "struct Guid { uint8_t bytes[16]; };",
        "struct ComponentInfo { const char* name; uint32_t hash; uint32_t size; };",
        "struct EventInfo { const char* name; uint32_t hash; uint32_t size; };  // name = short name",
        "}",
        "",
        "#pragma pack(push, 1)",
        "#pragma warning(push)",
        "#pragma warning(disable : 4200 4201)",
        "",
    ]
    reg = ["#pragma warning(pop)", "#pragma pack(pop)", "", "namespace emf::gen {",
           f"inline constexpr size_t kComponentCount = {len(components)};",
           "inline constexpr ComponentInfo kComponents[kComponentCount] = {"]
    for t in components:
        reg.append(f"    {{\"{t['qualified_name']}\", 0x{t['hash']:08x}u, {t['size']}}},")
    reg += ["};", f"inline constexpr size_t kDynamicComponentCount = {len(dynamics)};",
            "// The per-entity halves of split components (DynamicX for X), keyed by their own hash.",
            "inline constexpr ComponentInfo kDynamicComponents[kDynamicComponentCount] = {"]
    for t in dynamics:
        reg.append(f"    {{\"{t['qualified_name']}\", 0x{t['hash']:08x}u, {t['size']}}},")
    reg += ["};", f"inline constexpr size_t kEventCount = {len(events)};",
            "inline constexpr EventInfo kEvents[kEventCount] = {"]
    for t in events:
        reg.append(f"    {{\"{t['name']}\", 0x{t['hash']:08x}u, {t['size']}}},")
    reg += ["};", "}  // namespace emf::gen", ""]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(head + gen.out + reg), encoding="utf-8")
    refl_path = out_path.parent / "reflection.hpp"
    refl_path.write_text(reflection(gen, components, dynamics, events, generated_line), encoding="utf-8")
    print(f"wrote {out_path}: {len(components)} components, {len(dynamics)} dynamic structs, {len(events)} events, "
          f"{len(gen.emitted)} types; and {refl_path.name}")


def default_source():
    """The game's enshrouded.exe in the folder EMF_GAME_DIR names; stops with what to pass when it is not set."""
    game = os.environ.get("EMF_GAME_DIR")
    if not game:
        sys.exit("gen_headers.py: where is the game? Pass its exe (python tools/gen_headers.py <enshrouded.exe> "
                 "[out.hpp]), or set EMF_GAME_DIR to the game folder (its enshrouded.exe is read).")
    return os.path.join(game, "enshrouded.exe")


def main(argv=None):
    args = sys.argv[1:] if argv is None else list(argv)
    source = args[0] if args else default_source()
    out_path = Path(args[1]) if len(args) > 1 else DEFAULT_OUT
    generate(load(source), out_path)


if __name__ == "__main__":
    main()
