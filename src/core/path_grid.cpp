// path.door (path_grid.h).
#include "core/path_grid.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "common/seh.h"

namespace emf::core {

namespace {
// PathGridSystem (Sig::PathGridRegister)
constexpr size_t kGridCells = 0x130;          // -> the cells object
constexpr size_t kGridDoors = 0xa861c0;       // door table: entries of kDoorEntry bytes
constexpr size_t kGridDoorCount = 0xabe1c0;   // u16
constexpr size_t kDoorEntry = 0x38;           // u32 entity, u8 kind, six (u32 cell, u8 node in a u32) pairs from +8
constexpr uint32_t kDoorMax = 0x1000;
// the cells object (Sig::PathGridBoxToCells, Sig::PathGridNodePos, Sig::PathGridRegister for the list)
constexpr size_t kCellsList = 0xb8;           // -> first cell record; a record's next at kCellNext
constexpr size_t kCellsNodesAcross = 0xd1;    // u8 nodes per cell across (16), kCellsNodesUp u8 up (8)
constexpr size_t kCellsNodesUp = 0xd2;
constexpr size_t kCellsNodesPerMetre = 0xf0;  // float x, y, z (2, 1, 2)
constexpr size_t kCellsShiftAcross = 0x108;   // u32: cell = (int)(metres from the corner) >> shift (3: 8 m cells)
constexpr size_t kCellsShiftUp = 0x10c;
constexpr size_t kCellsOrigin = 0x120;        // float x, y, z: the grid's corner
constexpr size_t kCellsStrides = 0x12c;       // u16 at +0, +4, +8: a node's index = x * sx + y * sy + z * sz
// a cell record
constexpr size_t kCellNext = 0x08;
constexpr size_t kCellCoords = 0x20;          // u32 x, y, z
constexpr size_t kCellState = 0x2c;           // u8
constexpr size_t kCellBlock = 0x40;           // -> node block, null when the cell has none
constexpr size_t kBlockNodes = 0x28;          // 2 bytes per node
constexpr size_t kMaxCellsWalked = 0x8000;    // the game keeps at most 0x3000 cells

const char* const kNodeTypes[] = {"None", "TerrainEdge", "Tar", "Mud", "Road", "RoadCenter", "Fog", "DeadlyFog", "Lava",
                                  "Building", "Collider", "WalkablePlatform", "NoNpcMovementCollider",
                                  "NoNpcMovementTerrain", "NoNpcMovementBuilding", "NpcAvoid", "WaterSurface",
                                  "UnderWater", "WaterGround", "ClosedDoor"};

template <typename T>
bool rd(const uint8_t* p, T* out) {
    return p && seh_read(p, out, sizeof(T));
}

bool built(uint8_t state) { return state == 1 || state == 3 || state == 4 || state == 6; }

struct Layout {
    uint32_t nodes[3] = {};   // per cell: across, up, across
    float npm[3] = {};        // nodes per metre
    uint32_t shift[3] = {};   // cell shift
    float origin[3] = {};
    uint16_t stride[3] = {};
};

bool read_layout(const uint8_t* cells, Layout* l) {
    uint8_t across = 0, up = 0;
    uint32_t sa = 0, su = 0;
    if (!rd(cells + kCellsNodesAcross, &across) || !rd(cells + kCellsNodesUp, &up) ||
        !seh_read(cells + kCellsNodesPerMetre, l->npm, sizeof l->npm) || !rd(cells + kCellsShiftAcross, &sa) ||
        !rd(cells + kCellsShiftUp, &su) || !seh_read(cells + kCellsOrigin, l->origin, sizeof l->origin))
        return false;
    for (int i = 0; i < 3; ++i)
        if (!rd(cells + kCellsStrides + 4 * i, &l->stride[i])) return false;
    l->nodes[0] = l->nodes[2] = across;
    l->nodes[1] = up;
    l->shift[0] = l->shift[2] = sa;
    l->shift[1] = su;
    // what the game's code makes it (path_doors_2026-11-14.md 1.1): anything else is not this layout
    for (int i = 0; i < 3; ++i) {
        if (!(l->npm[i] > 0.01f && l->npm[i] < 100.0f) || !std::isfinite(l->origin[i]) || l->nodes[i] == 0 ||
            l->shift[i] > 12 || l->stride[i] == 0)
            return false;
        // a cell is (1 << shift) metres and holds `nodes` nodes of 1 / npm metres
        if (std::fabs(static_cast<float>(1u << l->shift[i]) * l->npm[i] - static_cast<float>(l->nodes[i])) > 0.01f)
            return false;
    }
    return true;
}

// The far corner of global node g (or its near corner with far = false) in metres on axis i.
float metres(const Layout& l, int i, uint64_t g, bool far = false) {
    return l.origin[i] + static_cast<float>(g + (far ? 1 : 0)) / l.npm[i];
}

std::string metre_text(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}
}  // namespace

std::string path_node_type_name(uint8_t type) {
    if (type < sizeof(kNodeTypes) / sizeof(kNodeTypes[0])) return kNodeTypes[type];
    return "type " + std::to_string(type);
}

PathDoorView read_path_door(const uint8_t* grid, uint32_t entity) {
    PathDoorView v;
    if (!grid) return v;
    v.grid = true;
    uint16_t count = 0;
    const uint8_t* cells = nullptr;
    if (!rd(grid + kGridDoorCount, &count) || !rd(grid + kGridCells, &cells)) return v;
    v.doors = count;
    if (count > kDoorMax) {
        v.layout_ok = false;
        return v;  // readable stays false: not the table EMF knows
    }
    v.readable = true;
    const uint8_t* entry = nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t e = 0;
        if (!rd(grid + kGridDoors + i * kDoorEntry, &e)) {
            v.readable = false;
            return v;
        }
        if (e == entity) {
            entry = grid + kGridDoors + i * kDoorEntry;
            break;
        }
    }
    if (!entry) return v;
    uint8_t raw[kDoorEntry] = {};
    if (!seh_read(entry, raw, sizeof raw)) {
        v.readable = false;
        return v;
    }
    v.found = true;
    v.kind = raw[4];
    Layout l;
    if (!cells || !read_layout(cells, &l)) {
        v.layout_ok = false;
        return v;
    }
    // the box: (u32 cell, u8 node) for min x, y, z then max x, y, z; global node = cell * nodes + node
    uint64_t gmin[3] = {}, gmax[3] = {};
    for (int i = 0; i < 3; ++i) {
        uint32_t c0 = 0, c1 = 0;
        std::memcpy(&c0, raw + 8 + i * 8, 4);
        std::memcpy(&c1, raw + 8 + (3 + i) * 8, 4);
        const uint8_t n0 = raw[12 + i * 8], n1 = raw[12 + (3 + i) * 8];  // the node is the low byte only
        gmin[i] = uint64_t(c0) * l.nodes[i] + n0;
        gmax[i] = uint64_t(c1) * l.nodes[i] + n1;
        v.min[i] = metres(l, i, gmin[i]);
        v.max[i] = metres(l, i, gmax[i], true);
    }
    v.row_y = v.min[1];
    // the node at the box's centre on its lowest layer
    const uint64_t g[3] = {(gmin[0] + gmax[0]) / 2, gmin[1], (gmin[2] + gmax[2]) / 2};
    for (int i = 0; i < 3; ++i) {
        v.cell[i] = static_cast<uint32_t>(g[i] / l.nodes[i]);
        v.local[i] = static_cast<uint32_t>(g[i] % l.nodes[i]);
        const float half = i == 1 ? 0.0f : 0.5f / l.npm[i];
        v.node_at[i] = metres(l, i, g[i]) + half;
    }
    // find the cell in the grid's list of cells
    const uint8_t* rec = nullptr;
    if (!rd(cells + kCellsList, &rec)) return v;
    for (size_t walked = 0; rec && walked < kMaxCellsWalked; ++walked) {
        uint32_t c[3] = {};
        if (!seh_read(rec + kCellCoords, c, sizeof c)) break;
        if (c[0] == v.cell[0] && c[1] == v.cell[1] && c[2] == v.cell[2]) {
            v.cell_found = true;
            break;
        }
        const uint8_t* next = nullptr;
        if (!rd(rec + kCellNext, &next)) break;
        rec = next;
    }
    if (!v.cell_found) return v;
    rd(rec + kCellState, &v.cell_state);
    if (!built(v.cell_state) && v.cell_state != 2) return v;  // new or failed: no nodes worth reading
    const uint8_t* block = nullptr;
    if (!rd(rec + kCellBlock, &block) || !block) return v;
    const uint64_t index = uint64_t(v.local[0]) * l.stride[0] + uint64_t(v.local[1]) * l.stride[1] +
                           uint64_t(v.local[2]) * l.stride[2];
    if (index >= uint64_t(l.nodes[0]) * l.nodes[1] * l.nodes[2]) {
        v.layout_ok = false;
        return v;
    }
    uint8_t b[2] = {};
    if (!seh_read(block + kBlockNodes + index * 2, b, 2)) return v;
    v.node.read = true;
    v.node.type = static_cast<uint8_t>(b[0] >> 3);
    v.node.across = static_cast<uint8_t>(b[0] & 7);
    v.node.ground = (b[1] & 0x80) != 0;
    v.node.up = static_cast<uint8_t>((b[1] >> 4) & 7);
    v.node.sub = static_cast<uint8_t>(b[1] & 0x0f);
    return v;
}

std::string path_door_text(uint32_t entity, const PathDoorView& v) {
    const std::string head = "path.door " + std::to_string(entity) + ": ";
    if (!v.grid) return head + "no path grid in this world";
    if (!v.readable) {
        if (!v.layout_ok) return head + "the path grid's layout is not what EMF knows (" + std::to_string(v.doors) +
                                 " doors in its table); nothing read";
        return head + "could not read the path grid";
    }
    if (!v.found)
        return head + "not in the path grid's door table (" + std::to_string(v.doors) +
               (v.doors == 1 ? " door registered)" : " doors registered)");
    std::string kind = v.kind == 1   ? "open (kind 1)"
                       : v.kind == 2 ? "closed (kind 2)"
                       : v.kind == 3 ? "a moving object (kind 3)"
                                     : "kind " + std::to_string(v.kind);
    std::string line = head + "in the path grid's door table, " + kind;
    if (!v.layout_ok) return line + "; the path grid's layout is not what EMF knows, so its box is not shown";
    line += "; box x " + metre_text(v.min[0]) + ".." + metre_text(v.max[0]) + ", y " + metre_text(v.min[1]) + ".." +
            metre_text(v.max[1]) + ", z " + metre_text(v.min[2]) + ".." + metre_text(v.max[2]);
    if (v.kind == 1 || v.kind == 2) line += "; door row y " + metre_text(v.row_y);
    char at[96];
    std::snprintf(at, sizeof at, "node (%.2f, %.2f, %.2f)", v.node_at[0], v.node_at[1], v.node_at[2]);
    std::string second = std::string(v.kind == 3 ? "lowest layer at the box's centre: " : "at the row's centre: ") + at;
    if (!v.cell_found) return line + "\n" + second + ": its cell is not built (no path query has needed it yet)";
    if (v.cell_state == 0) return line + "\n" + second + ": cell not built yet";
    if (v.cell_state == 5) return line + "\n" + second + ": the cell's build failed";
    std::string cell = built(v.cell_state) ? "cell built"
                       : v.cell_state == 2 ? "cell dirty (rebuilt when a query needs it; these nodes are the old ones)"
                                           : "cell state " + std::to_string(v.cell_state);
    if (!v.node.read) return line + "\n" + second + ": " + cell + ", no node data (nothing walkable in it)";
    char n[160];
    std::snprintf(n, sizeof n, ", %s (%u), %s, room across %.1f m, up %.1f m", path_node_type_name(v.node.type).c_str(),
                  unsigned(v.node.type), v.node.ground ? "ground" : "not ground", v.node.across * 0.5, v.node.up * 0.5);
    return line + "\n" + second + ": " + cell + n;
}

}  // namespace emf::core
