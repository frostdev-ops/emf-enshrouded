#pragma once
// path.door (0.6.11): what the game's path grid knows about a door, read from its PathGridSystem inside EMF's emf_path
// program (the only place EMF holds it). Private research path_doors_2026-11-14.md sections 1 and 2. The layout is the
// one of build 1287369, pinned by Sig::PathGridRegister, PathGridBoxToCells, PathGridNodePos and PathGridCellList;
// every read goes
// through seh_read, and values that do not look like the game's are reported, never guessed around.
#include <cstdint>
#include <string>

namespace emf::core {

// One grid node (2 bytes): byte0 = type << 3 | room across, byte1 = ground << 7 | room up << 4 | height in the layer.
struct PathGridNode {
    bool read = false;
    uint8_t type = 0;    // PathNodeType (19 = ClosedDoor)
    bool ground = false; // standable
    uint8_t across = 0;  // room across: rings of 0.5 m free in 8 directions (the game fills 0..4)
    uint8_t up = 0;      // room up: free half metres above (0..7)
    uint8_t sub = 0;     // height inside the 1 m layer, in 16ths
};

struct PathDoorView {
    bool grid = false;       // there is a path grid (only the server world has one)
    bool readable = false;   // the door table could be read
    bool layout_ok = true;   // the cells object looks like the game's (nodes per metre, cell sizes, strides)
    uint32_t doors = 0;      // entries in the door table
    bool found = false;      // the entity is one of them
    uint8_t kind = 0;        // 1 open door, 2 closed door, 3 moving object
    float min[3] = {}, max[3] = {};  // the registered box in metres: the first node's corner to the last node's far side
    float row_y = 0;         // a door's row: the box's lowest node layer (closed: ClosedDoor nodes there)
    uint32_t cell[3] = {};   // the node at the box's centre on that row: its cell ...
    uint32_t local[3] = {};  // ... and its node inside the cell
    float node_at[3] = {};   // that node in metres (the middle across, the layer's floor up)
    bool cell_found = false; // the cell is in the grid's cell list
    bool list_cut = false;   // the cell list could not be read to its end (a fault, or more cells than the game keeps)
    uint8_t cell_state = 0;  // 0 new, 1/3/4 built, 6 built or a failed build, 2 dirty (rebuilt when a query needs it),
                             // 5 failed
    PathGridNode node;       // its two bytes, when the cell has nodes
    bool node_unreadable = false;  // the cell's state or node block could not be read
};

// Reads the grid's view of `entity` (grid = the PathGridSystem, *(job + 0x28) in a server program; null: none).
PathDoorView read_path_door(const uint8_t* grid, uint32_t entity);
// One or two lines for the console ("path.door <entity>: ...").
std::string path_door_text(uint32_t entity, const PathDoorView& v);
// The game's PathNodeType names ("type N" past the 20 it has).
std::string path_node_type_name(uint8_t type);

}  // namespace emf::core
