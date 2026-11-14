// path.door (0.6.11): the game's path grid as EMF reads it, over a fake grid in memory (a door-table entry, the cells
// object, a cell record with its node block). Private research path_doors_2026-11-14.md sections 1 and 2.
#include <doctest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/path_grid.h"
#include "fake_path_grid.h"

using namespace emf::core;

using emf::test::FakeGrid;
using emf::test::grid_put;

TEST_CASE("path grid: a closed door's table entry, its box in metres, the door row and the ClosedDoor node there") {
    FakeGrid g;
    g.standard_door(2);
    g.node(4, 2, 5, 0x98, 0x00);  // ClosedDoor (19), not ground, no room
    const PathDoorView v = read_path_door(g.grid.data(), 77);
    REQUIRE(v.readable);
    CHECK(v.doors == 2u);
    REQUIRE(v.found);
    CHECK(v.kind == 2);
    // x: 100 + 10 * 8 + 3 / 2 = 181.5 .. the far side of node 6: 100 + 80 + 7 / 2 = 183.5
    CHECK(v.min[0] == doctest::Approx(181.5));
    CHECK(v.max[0] == doctest::Approx(183.5));
    // y: -50 + 4 * 8 + 2 = -16 .. one 1 m layer
    CHECK(v.min[1] == doctest::Approx(-16.0));
    CHECK(v.max[1] == doctest::Approx(-15.0));
    CHECK(v.row_y == doctest::Approx(-16.0));
    // z: 200 + 20 * 8 + 5 / 2 = 362.5 .. 200 + 160 + 7 / 2 = 363.5
    CHECK(v.min[2] == doctest::Approx(362.5));
    CHECK(v.max[2] == doctest::Approx(363.5));
    // the centre node: x nodes 163..166 -> 164 (cell 10, node 4), z nodes 325..326 -> 325 (cell 20, node 5)
    CHECK(v.cell[0] == 10u);
    CHECK(v.cell[2] == 20u);
    CHECK(v.local[0] == 4u);
    CHECK(v.local[1] == 2u);
    CHECK(v.local[2] == 5u);
    CHECK(v.node_at[0] == doctest::Approx(182.25));  // the node's middle across, the row's floor up
    CHECK(v.node_at[1] == doctest::Approx(-16.0));
    CHECK(v.node_at[2] == doctest::Approx(362.75));
    REQUIRE(v.cell_found);
    CHECK(v.cell_state == 1);
    REQUIRE(v.node.read);
    CHECK(v.node.type == 19);
    CHECK_FALSE(v.node.ground);
    CHECK(v.node.across == 0);
    CHECK(v.node.up == 0);
    const std::string text = path_door_text(77, v);
    CHECK(text.find("path.door 77: in the path grid's door table, closed (kind 2)") == 0);
    CHECK(text.find("box x 181.5..183.5, y -16.0..-15.0, z 362.5..363.5") != std::string::npos);
    CHECK(text.find("door row y -16.0") != std::string::npos);
    CHECK(text.find("node (182.25, -16.00, 362.75)") != std::string::npos);
    CHECK(text.find("cell built") != std::string::npos);
    CHECK(text.find("ClosedDoor (19), not ground, room across 0.0 m, up 0.0 m") != std::string::npos);
}

TEST_CASE("path grid: an open door; the node is what the voxels say (ground, room across and up in half metres)") {
    FakeGrid g;
    g.standard_door(1);
    g.node(4, 2, 5, (9 << 3) | 2, 0x80 | (5 << 4) | 3);  // Building, room across 2, ground, room up 5
    const PathDoorView v = read_path_door(g.grid.data(), 77);
    REQUIRE(v.found);
    CHECK(v.kind == 1);
    REQUIRE(v.node.read);
    CHECK(v.node.type == 9);
    CHECK(v.node.ground);
    CHECK(v.node.across == 2);
    CHECK(v.node.up == 5);
    CHECK(v.node.sub == 3);
    const std::string text = path_door_text(77, v);
    CHECK(text.find(", open (kind 1)") != std::string::npos);
    CHECK(text.find("Building (9), ground, room across 1.0 m, up 2.5 m") != std::string::npos);
}

TEST_CASE("path grid: not in the table; a moving object; a cell not built yet; a dirty cell; an empty cell") {
    SUBCASE("an entity the table does not hold") {
        FakeGrid g;
        g.standard_door(2);
        const PathDoorView v = read_path_door(g.grid.data(), 5);
        REQUIRE(v.readable);
        CHECK_FALSE(v.found);
        CHECK(path_door_text(5, v) == "path.door 5: not in the path grid's door table (2 doors registered)");
        grid_put<uint16_t>(g.grid.data(), 0xabe1c0, 1);
        CHECK(path_door_text(5, read_path_door(g.grid.data(), 5)) ==
              "path.door 5: not in the path grid's door table (1 door registered)");
        grid_put<uint16_t>(g.grid.data(), 0xabe1c0, 0);
        CHECK(path_door_text(5, read_path_door(g.grid.data(), 5)) ==
              "path.door 5: not in the path grid's door table (0 doors registered)");
    }
    SUBCASE("a moving object keeps its whole box (no door row)") {
        FakeGrid g;
        const uint32_t c[6] = {10, 4, 20, 10, 5, 20};
        const uint8_t n[6] = {3, 2, 5, 6, 1, 6};
        g.door(0, 77, 3, c, n);
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        REQUIRE(v.found);
        CHECK(v.max[1] == doctest::Approx(-50.0f + 40 + 2));  // cell 5, node 1: -50 + 40 + 1 .. + 1
        const std::string text = path_door_text(77, v);
        CHECK(text.find("a moving object (kind 3)") != std::string::npos);
        CHECK(text.find("door row") == std::string::npos);
    }
    SUBCASE("the door's cell is not in the grid's cell list") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<uint32_t>(g.cell, 0x20, 11);  // the list holds (9,4,20) and (11,4,20) only
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        REQUIRE(v.found);
        CHECK_FALSE(v.cell_found);
        CHECK(path_door_text(77, v).find("its cell is not built (no path query has needed it yet)") != std::string::npos);
    }
    SUBCASE("a dirty cell: the nodes are shown, marked stale") {
        FakeGrid g;
        g.standard_door(2);
        g.cell[0x2c] = 2;
        g.node(4, 2, 5, 0x98, 0x00);
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        REQUIRE(v.node.read);
        CHECK(path_door_text(77, v).find("cell dirty (rebuilt when a query needs it; these nodes are the old ones)") !=
              std::string::npos);
    }
    SUBCASE("a new cell (state 0): no nodes yet") {
        FakeGrid g;
        g.standard_door(2);
        g.cell[0x2c] = 0;
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK(v.cell_found);
        CHECK_FALSE(v.node.read);
        CHECK(path_door_text(77, v).find("cell not built yet") != std::string::npos);
    }
    SUBCASE("a built cell without a node block") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<void*>(g.cell, 0x40, nullptr);
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK(v.cell_found);
        CHECK_FALSE(v.node.read);
        CHECK(path_door_text(77, v).find("cell built, no node data (nothing walkable in it)") != std::string::npos);
    }
}

TEST_CASE("path grid: unreadable memory and a layout that does not look like the game's are said, never guessed") {
    SUBCASE("no grid") {
        const PathDoorView v = read_path_door(nullptr, 77);
        CHECK_FALSE(v.readable);
        CHECK(path_door_text(77, v) == "path.door 77: no path grid in this world");
    }
    SUBCASE("a grid whose memory faults") {
        const PathDoorView v = read_path_door(reinterpret_cast<const uint8_t*>(uintptr_t(0x10)), 77);
        CHECK_FALSE(v.readable);
        CHECK(path_door_text(77, v) == "path.door 77: could not read the path grid");
    }
    SUBCASE("a door count over the table's 4096") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<uint16_t>(g.grid.data(), 0xabe1c0, 0x1001);
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK_FALSE(v.readable);
        CHECK(path_door_text(77, v).find("path.door 77: the path grid's layout is not what EMF knows") == 0);
    }
    SUBCASE("nodes per metre of 0 (not the game's cells object)") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<float>(g.cells, 0xf4, 0.0f);
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK(v.found);  // the table entry still counts
        CHECK_FALSE(v.layout_ok);
        CHECK(path_door_text(77, v).find("the path grid's layout is not what EMF knows") != std::string::npos);
    }
    SUBCASE("a cell list that loops is cut off") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<uint32_t>(g.cell, 0x20, 11);
        grid_put<void*>(g.cell, 0x08, g.other);  // other -> cell -> other -> ...
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK(v.found);
        CHECK_FALSE(v.cell_found);
    }
}

TEST_CASE("path grid: node type names are the game's PathNodeType") {
    CHECK(path_node_type_name(0) == "None");
    CHECK(path_node_type_name(9) == "Building");
    CHECK(path_node_type_name(10) == "Collider");
    CHECK(path_node_type_name(19) == "ClosedDoor");
    CHECK(path_node_type_name(20) == "type 20");
    CHECK(path_node_type_name(31) == "type 31");
}

// Review (0.6.11) M3: what could not be read is said as such, not as "not built" or "nothing walkable"; state 6 (built,
// or set when a build failed) and states EMF does not know are named. M7: a grid corner past a million metres is not
// the game's layout.
TEST_CASE("path grid (review M3, M7): an unreadable or cut-off cell list, unreadable nodes, state 6 and unknown states") {
    SUBCASE("a cell list that loops: cut off, said so") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<uint32_t>(g.cell, 0x20, 11);
        grid_put<void*>(g.cell, 0x08, g.other);  // other -> cell -> other -> ...
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK(v.list_cut);
        CHECK(path_door_text(77, v).find("its cell could not be looked up (the grid's cell list could not be read to "
                                         "the end); ask again") != std::string::npos);
    }
    SUBCASE("a cell list whose next cell faults") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<void*>(g.other, 0x08, reinterpret_cast<void*>(uintptr_t(0x10)));
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK_FALSE(v.cell_found);
        CHECK(v.list_cut);
    }
    SUBCASE("the list's end without the cell: not built (the list was read to the end)") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<uint32_t>(g.cell, 0x20, 11);
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK_FALSE(v.list_cut);
        CHECK(path_door_text(77, v).find("its cell is not built (no path query has needed it yet)") != std::string::npos);
    }
    SUBCASE("a node block that faults") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<void*>(g.cell, 0x40, reinterpret_cast<void*>(uintptr_t(0x10)));
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK_FALSE(v.node.read);
        CHECK(v.node_unreadable);
        CHECK(path_door_text(77, v).find("cell built, its nodes could not be read; ask again") != std::string::npos);
    }
    SUBCASE("state 6: built, or set when a build failed; the nodes are read") {
        FakeGrid g;
        g.standard_door(2);
        g.cell[0x2c] = 6;
        g.node(4, 2, 5, 0x98, 0x80);
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        REQUIRE(v.node.read);
        CHECK(path_door_text(77, v).find("cell built or its build failed (state 6), ClosedDoor (19), ground") !=
              std::string::npos);
    }
    SUBCASE("a state EMF does not know") {
        FakeGrid g;
        g.standard_door(2);
        g.cell[0x2c] = 7;
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK_FALSE(v.node.read);
        CHECK(path_door_text(77, v).find("cell state 7 (not one EMF knows); its nodes are not read") != std::string::npos);
    }
    SUBCASE("M7: a grid corner past a million metres is not the game's layout") {
        FakeGrid g;
        g.standard_door(2);
        grid_put<float>(g.cells, 0x120, 2.0e6f);
        const PathDoorView v = read_path_door(g.grid.data(), 77);
        CHECK(v.found);
        CHECK_FALSE(v.layout_ok);
    }
}
