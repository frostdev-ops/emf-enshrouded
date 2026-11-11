#pragma once
// Markers (API v33): entries in the game's own map marker list for markers with a game_icon (EmfMarker.game_icon; EMF
// draws every map marker itself over the map screen, marker_ui.h). The list (UiPlayData+0x36ea00: data, count,
// capacity; 0xA0-byte entries) is emptied by the game's clear_map_markers_for_ui and filled by
// replicate_map_markers_for_ui every client step; EMF's post-hook on the clear puts its entries first, within a share
// that leaves the game's own entries room. An entry of EMF: entity 0, the position as 32.32 fixed point, the type,
// shown (2), the label's pointer, length and FNV-1a 64 (checked live: plans/2026-11-11-markers-step1.ledger.md, Task 1).
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core/markers.h"

namespace emf::core {

constexpr size_t kMapEntryBytes = 0xA0;
constexpr uint32_t kMapShareMax = 128;     // EMF's entries at most
constexpr uint32_t kMapGameReserve = 16;   // kept free beyond the game's own count of the step before
constexpr uint64_t kMapCapacityMax = 0x4000;  // a capacity beyond this means the list was not read right
constexpr size_t kMapSeenTypesMax = 256;

struct MapList {
    uint8_t* data = nullptr;
    uint64_t count = 0;
    uint64_t capacity = 0;
};

// One entry of EMF (the rest zero).
void fill_map_entry(uint8_t* entry, const Marker& m, const char* label, size_t len);
// How many entries EMF may put: min(128, capacity - game_last - 16), 0 when there is no such room.
uint32_t emf_share(uint64_t capacity, uint64_t game_last);

// The client world's thread (the clear's pre- and post-hook), except seen_types.
class MapMarkers {
public:
    // Before the game empties the list: the count its own entries reached last step, the types they use.
    void pre(const MapList* list);
    // After it emptied it: EMF's entries with a game_icon, nearest to `player` first (null: in id order), within the
    // share and the capacity; how many were put (list->count grows by that).
    uint32_t post(MapList* list, const std::vector<Marker>& markers, const double* player);
    uint64_t game_last() const { return game_last_; }
    std::vector<uint32_t> seen_types() const;  // ascending; any thread
    void reset();                              // a new world
    void set_appended_for_tests(uint32_t n) { appended_ = n; }

private:
    static constexpr size_t kArenas = 3;  // a label lives through the step it was put and the two after it
    std::array<std::deque<std::string>, kArenas> arenas_;
    size_t step_ = 0;
    uint32_t appended_ = 0;
    uint64_t game_last_ = 0;
    mutable std::mutex seen_m_;
    std::set<uint32_t> seen_;
};

}  // namespace emf::core
