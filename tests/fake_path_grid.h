#pragma once
// A fake of the game's path grid in memory for path.door's tests (tests/test_path_grid.cpp, test_path_glue.cpp): the
// PathGridSystem's door table and count, its cells object, a cell list of two records and one node block, laid out as
// in build 1287369 (private research path_doors_2026-11-14.md section 1.1).
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace emf::test {

template <typename T>
void grid_put(uint8_t* p, size_t off, T v) {
    std::memcpy(p + off, &v, sizeof v);
}

// The PathGridSystem (only what path.door reads), the cells object and one cell, laid out as the game's are.
struct FakeGrid {
    std::vector<uint8_t> grid = std::vector<uint8_t>(0xabe1c8, 0);  // the door count is the last field read
    alignas(8) uint8_t cells[0x400] = {};
    alignas(8) uint8_t cell[0x50] = {};      // a cell record
    alignas(8) uint8_t other[0x50] = {};     // another cell in the list, before it
    std::unique_ptr<uint8_t[]> block{new uint8_t[0x1028]()};  // the cell's node block: nodes at +0x28
    FakeGrid() {
        grid_put<void*>(grid.data(), 0x130, cells);
        // cells: 16 nodes across and 8 up per cell, 2/1/2 nodes per metre, 8 m cells, the world's corner, strides
        cells[0xd1] = 16;
        cells[0xd2] = 8;
        grid_put<float>(cells, 0xf0, 2.0f);
        grid_put<float>(cells, 0xf4, 1.0f);
        grid_put<float>(cells, 0xf8, 2.0f);
        grid_put<uint32_t>(cells, 0x108, 3);
        grid_put<uint32_t>(cells, 0x10c, 3);
        grid_put<float>(cells, 0x120, 100.0f);
        grid_put<float>(cells, 0x124, -50.0f);
        grid_put<float>(cells, 0x128, 200.0f);
        grid_put<uint16_t>(cells, 0x12c, 1);
        grid_put<uint16_t>(cells, 0x130, 16);
        grid_put<uint16_t>(cells, 0x134, 128);
        // the cell list: other -> cell
        grid_put<void*>(cells, 0xb8, other);
        grid_put<void*>(other, 0x08, cell);
        grid_put<uint32_t>(other, 0x20, 9);  // a neighbour: (9, 4, 20)
        grid_put<uint32_t>(other, 0x24, 4);
        grid_put<uint32_t>(other, 0x28, 20);
        other[0x2c] = 1;
        grid_put<uint32_t>(cell, 0x20, 10);  // the door's cell: (10, 4, 20)
        grid_put<uint32_t>(cell, 0x24, 4);
        grid_put<uint32_t>(cell, 0x28, 20);
        cell[0x2c] = 1;  // built
        grid_put<void*>(cell, 0x40, block.get());
    }
    // A door-table entry: entity, kind, the box as (cell, node) pairs (min x, y, z; max x, y, z).
    void door(size_t i, uint32_t entity, uint8_t kind, const uint32_t c[6], const uint8_t n[6]) {
        uint8_t* e = grid.data() + 0xa861c0 + i * 0x38;
        grid_put<uint32_t>(e, 0, entity);
        e[4] = kind;
        for (size_t k = 0; k < 6; ++k) {
            grid_put<uint32_t>(e, 8 + k * 8, c[k]);
            grid_put<uint32_t>(e, 12 + k * 8, 0xabcd0000u | n[k]);  // the game leaves the upper bytes as they were
        }
        grid_put<uint16_t>(grid.data(), 0xabe1c0, static_cast<uint16_t>(i + 1));
    }
    void standard_door(uint8_t kind) {
        const uint32_t c[6] = {10, 4, 20, 10, 4, 20};
        const uint8_t n[6] = {3, 2, 5, 6, 2, 6};
        door(0, 0x4711, 7, c, n);  // another entry first: path.door looks the entity up
        door(1, 77, kind, c, n);
    }
    // The node at local (x, y, z) of the cell: index x + 16 y + 128 z.
    void node(uint32_t x, uint32_t y, uint32_t z, uint8_t b0, uint8_t b1) {
        const size_t i = x + 16 * y + 128 * z;
        block[0x28 + i * 2] = b0;
        block[0x28 + i * 2 + 1] = b1;
    }
};

}  // namespace emf::test
