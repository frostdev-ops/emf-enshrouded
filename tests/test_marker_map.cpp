// Markers (API v33): entries in the game's own map marker list (EmfMarker.game_icon): the 0xA0-byte entry, EMF's share
// of the list, the labels' memory and the types seen (fake list, no game).
#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "common/fnv.h"
#include "core/marker_map.h"

using namespace emf::core;

namespace {

template <class T>
T get(const uint8_t* p, size_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}

struct FakeList {
    std::vector<uint8_t> entries;
    MapList list;
    explicit FakeList(uint64_t cap) : entries(static_cast<size_t>(cap) * kMapEntryBytes, 0) {
        list.data = entries.data();
        list.count = 0;
        list.capacity = cap;
    }
    void add_game(uint32_t entity, uint32_t type) {
        uint8_t* e = entries.data() + list.count * kMapEntryBytes;
        std::memcpy(e, &entity, 4);
        std::memcpy(e + 0x30, &type, 4);
        ++list.count;
    }
};

Marker icon_marker(const std::string& id, double x, double z, uint32_t type, const std::string& text = "Grave") {
    Marker m;
    m.owner = "t";
    m.id = id;
    m.x = x;
    m.y = 50;
    m.z = z;
    m.flags = EMF_MARKER_GAME_MAP;
    m.game_icon = type;
    m.text = text;
    return m;
}

}  // namespace

TEST_CASE("map entry: entity 0, the position as 32.32, the type, shown, and the label's pointer, length and hash") {
    uint8_t e[kMapEntryBytes];
    std::memset(e, 0xAB, sizeof e);
    const Marker m = icon_marker("g", 1000.5, 2000.25, 0x813080bcu);
    const char* label = "Grave";
    fill_map_entry(e, m, label, 5);
    CHECK(get<uint32_t>(e, 0x00) == 0u);
    CHECK(get<uint32_t>(e, 0x04) == 0u);
    CHECK(get<uint32_t>(e, 0x08) == 0u);
    CHECK(get<int64_t>(e, 0x10) == static_cast<int64_t>(1000.5 * 4294967296.0));
    CHECK(get<int64_t>(e, 0x18) == static_cast<int64_t>(50.0 * 4294967296.0));
    CHECK(get<int64_t>(e, 0x20) == static_cast<int64_t>(2000.25 * 4294967296.0));
    CHECK(get<uint32_t>(e, 0x30) == 0x813080bcu);
    CHECK(e[0x34] == 2);
    CHECK(get<const char*>(e, 0x50) == label);
    CHECK(get<uint64_t>(e, 0x58) == 5u);
    CHECK(get<uint64_t>(e, 0x60) == emf::fnv1a64("Grave"));
    CHECK(e[0x48] == 0);  // everything not set is zero
    CHECK(get<uint64_t>(e, 0x40) == 0u);
}

TEST_CASE("map share: at most 128, never into the room the game's own entries need") {
    CHECK(emf_share(1024, 300) == 128u);
    CHECK(emf_share(1024, 950) == 58u);
    CHECK(emf_share(1024, 1008) == 0u);
    CHECK(emf_share(1024, 1020) == 0u);
    CHECK(emf_share(0, 0) == 0u);
}

TEST_CASE("map list: EMF's entries go first, within the share and the capacity; a list that reads wrong gets nothing") {
    MapMarkers mm;
    FakeList f(1024);
    std::vector<Marker> ms = {icon_marker("a", 10, 20, 0x11u), icon_marker("b", 30, 40, 0x22u),
                              icon_marker("plain", 1, 1, 0)};  // no game_icon: EMF draws it, the game's list does not
    ms[2].flags = EMF_MARKER_GAME_MAP;
    CHECK(mm.post(&f.list, ms, nullptr) == 2u);
    CHECK(f.list.count == 2u);
    CHECK(get<uint32_t>(f.entries.data(), 0x30) == 0x11u);
    CHECK(get<uint32_t>(f.entries.data() + kMapEntryBytes, 0x30) == 0x22u);
    // a list of 4: room for 0 once the game's 16 of reserve are kept
    FakeList small(4);
    CHECK(mm.post(&small.list, ms, nullptr) == 0u);
    CHECK(small.list.count == 0u);
    MapList broken;
    broken.data = nullptr;
    broken.capacity = 1024;
    CHECK(mm.post(&broken, ms, nullptr) == 0u);
    MapList huge;
    huge.data = f.entries.data();
    huge.capacity = 0x10000;  // more than any real list
    CHECK(mm.post(&huge, ms, nullptr) == 0u);
}

TEST_CASE("map list: the game's own count from last step shrinks EMF's share; the nearest go first") {
    MapMarkers mm;
    FakeList f(1024);
    // last step: EMF put 2, the game 1000 after them
    for (int i = 0; i < 1002; ++i) f.add_game(i < 2 ? 0 : 5000 + i, 0x99u);
    mm.set_appended_for_tests(2);
    mm.pre(&f.list);
    CHECK(mm.game_last() == 1000u);
    f.list.count = 0;  // the game's clear
    std::vector<Marker> ms;
    for (int i = 0; i < 20; ++i) ms.push_back(icon_marker("m" + std::to_string(i), 100.0 * i, 0, 0x33u));
    const double player[3] = {1900, 0, 0};
    CHECK(mm.post(&f.list, ms, player) == 8u);  // 1024 - 1000 - 16
    CHECK(get<int64_t>(f.entries.data(), 0x10) == static_cast<int64_t>(1900.0 * 4294967296.0));  // the nearest first
}

TEST_CASE("map list: the types the game's own entries use are collected; labels outlive two steps") {
    MapMarkers mm;
    FakeList f(64);
    f.add_game(1047, 0xa447cba3u);
    f.add_game(2030, 0x813080bcu);
    f.add_game(2031, 0x813080bcu);
    mm.pre(&f.list);
    const auto types = mm.seen_types();
    REQUIRE(types.size() == 2);
    CHECK(types[0] == 0x813080bcu);
    CHECK(types[1] == 0xa447cba3u);
    f.list.count = 0;
    std::vector<Marker> ms = {icon_marker("a", 1, 1, 0x11u, "First")};
    REQUIRE(mm.post(&f.list, ms, nullptr) == 1u);
    const char* first = get<const char*>(f.entries.data(), 0x50);
    CHECK(std::string(first, 5) == "First");
    // two more steps: the first label is still the same memory
    for (int i = 0; i < 2; ++i) {
        mm.pre(&f.list);
        f.list.count = 0;
        ms[0].text = "Step" + std::to_string(i);
        mm.post(&f.list, ms, nullptr);
    }
    CHECK(std::string(first, 5) == "First");
    mm.reset();
    CHECK(mm.seen_types().empty());
}

TEST_CASE("map list: entries that cannot be read are skipped without a fault; the types stay readable") {
    MapMarkers mm;
    MapList bad;
    bad.data = reinterpret_cast<uint8_t*>(0x10);  // not readable
    bad.count = 3;
    bad.capacity = 64;
    mm.pre(&bad);  // must not fault
    CHECK(mm.seen_types().empty());
    mm.reset();
    CHECK(mm.seen_types().empty());
}
