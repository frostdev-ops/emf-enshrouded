#include <windows.h>

#include <doctest.h>

#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/fnv.h"
#include "common/offsets.h"
#include "core/item_names.h"
#include "core/items.h"
#include "fake_world.h"

using namespace emf::core;
namespace off = emf::off;

using FakeRegistry = emf::test::FakeItemRegistry;

TEST_CASE("items: an item id is FNV-1a-32 over the 16 raw GUID bytes, and 0 for a null GUID") {
    uint8_t g[16];
    for (int i = 0; i < 16; ++i) g[i] = static_cast<uint8_t>(i * 7 + 1);
    CHECK(item_id_from_guid(g) == emf::fnv1a32(g, 16));
    const uint8_t zero[16] = {};
    CHECK(item_id_from_guid(zero) == 0);
}

TEST_CASE("items: list, by id and by name read the registry's item map") {
    FakeRegistry r;
    r.add(0x11, 10, 20, "Wood_Log");
    r.add(0x22, 40, 1, "Torch");
    r.add(0x33, 70, 99, nullptr);
    const auto all = list_items(r.reg);
    CHECK(all.size() == 3);
    const auto torch = item_by_id(r.reg, 0x22);
    REQUIRE(torch.has_value());
    CHECK(torch->name == "Torch");
    CHECK(torch->max_stack == 1);
    CHECK(torch->guid[0] == 40);
    CHECK(torch->guid[15] == 55);
    CHECK_FALSE(item_by_id(r.reg, 0x44).has_value());
    CHECK(item_by_id(r.reg, 0x33)->name.empty());
    REQUIRE(item_by_name(r.reg, "Wood_Log").has_value());
    CHECK(item_by_name(r.reg, "Wood_Log")->id == 0x11);
    REQUIRE(item_by_name(r.reg, "wood_log").has_value());  // case-insensitive fallback
    CHECK(item_by_name(r.reg, "wood_log")->id == 0x11);
    CHECK_FALSE(item_by_name(r.reg, "Stone").has_value());
    CHECK_FALSE(item_by_name(r.reg, "").has_value());
}

TEST_CASE("items: an exact name wins over a case-insensitive one") {
    FakeRegistry r;
    r.add(0x01, 0, 1, "torch");
    r.add(0x02, 0, 1, "Torch");
    CHECK(item_by_name(r.reg, "Torch")->id == 0x02);
    CHECK(item_by_name(r.reg, "torch")->id == 0x01);
}

TEST_CASE("items: broken registries and names give empty results, never a fault") {
    CHECK(list_items(nullptr).empty());
    FakeRegistry empty;
    CHECK(list_items(empty.reg).empty());
    CHECK_FALSE(item_by_id(empty.reg, 1).has_value());
    FakeRegistry r;
    r.add(0x05, 0, 1, "Bad", true);
    REQUIRE(item_by_id(r.reg, 0x05).has_value());
    CHECK(item_by_id(r.reg, 0x05)->name.empty());  // unprintable debug name dropped
    r.vals[0x05 & 7] = reinterpret_cast<const uint8_t*>(0x10);  // a bad ItemInfo pointer
    CHECK_FALSE(item_by_id(r.reg, 0x05).has_value());
    CHECK(list_items(r.reg).empty());
}

TEST_CASE("items: known answer from the game: Material_T1_Wood_Logs") {
    // GUID text from the game's item list; item id from the same run's PickupEvent (Windows GUID byte order).
    const uint8_t guid[16] = {0xae, 0x93, 0x40, 0x3f, 0xed, 0x75, 0x5d, 0x4e,
                              0xbd, 0x0b, 0x78, 0x4b, 0xa8, 0x3b, 0x88, 0xf5};
    CHECK(item_id_from_guid(guid) == 0x2b0cf1d5u);
}

// ---- item definition writes ----
namespace {
// Moves an item's ItemInfo into a read-only page (as if the game's data were not writable).
uint8_t* make_read_only(FakeRegistry& r, uint32_t id) {
    for (int slot = 0; slot < 8; ++slot) {
        if (r.keys[slot] != id || !(r.bits[0] >> slot & 1)) continue;
        auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        std::memcpy(page, r.vals[slot], off::kItemInfoSize);
        std::memset(page + off::kItemInfoDebugName, 0, 8);  // the name is not copied
        DWORD old = 0;
        VirtualProtect(page, 0x1000, PAGE_READONLY, &old);
        r.vals[slot] = page;
        return page;
    }
    return nullptr;
}
}  // namespace

TEST_CASE("items: set_max_stack changes the stack size the registry reports") {
    FakeRegistry r;
    r.add(0x11, 10, 250, "Wood_Log");
    r.add(0x22, 40, 1, "Torch");
    CHECK(item_data_ptr(r.reg, 0x11) != nullptr);
    CHECK(item_data_ptr(r.reg, 0x44) == nullptr);
    CHECK(set_max_stack(r.reg, 0x11, 1000));
    CHECK(item_by_id(r.reg, 0x11)->max_stack == 1000);
    CHECK(item_by_id(r.reg, 0x22)->max_stack == 1);
}

TEST_CASE("items: set_max_stack refuses 0, unknown items and a null registry") {
    FakeRegistry r;
    r.add(0x11, 10, 250, "Wood_Log");
    CHECK_FALSE(set_max_stack(r.reg, 0x11, 0));
    CHECK(item_by_id(r.reg, 0x11)->max_stack == 250);
    CHECK_FALSE(set_max_stack(r.reg, 0x44, 10));
    CHECK_FALSE(set_max_stack(nullptr, 0x11, 10));
}

TEST_CASE("items: data the game made read-only (like its resource memory) is written, and stays read-only after") {
    FakeRegistry r;
    r.add(0x22, 40, 20, "Stone");
    uint8_t* page = make_read_only(r, 0x22);
    REQUIRE(page != nullptr);
    CHECK(set_max_stack(r.reg, 0x22, 99));
    CHECK(item_by_id(r.reg, 0x22)->max_stack == 99);
    MEMORY_BASIC_INFORMATION mbi{};
    REQUIRE(VirtualQuery(page, &mbi, sizeof(mbi)));
    CHECK((mbi.Protect & 0xff) == PAGE_READONLY);
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST_CASE("write_game_memory refuses code, no-access and unmapped memory") {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    REQUIRE(page != nullptr);
    const uint32_t v = 0x12345678;
    CHECK(write_game_memory(page, &v, 4));
    DWORD old = 0;
    VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old);
    CHECK_FALSE(write_game_memory(page, &v, 4));  // code: never
    VirtualProtect(page + 0x1000, 0x1000, PAGE_NOACCESS, &old);
    CHECK_FALSE(write_game_memory(page + 0x1000, &v, 4));
    CHECK_FALSE(write_game_memory(page + 0xffe, &v, 4));  // straddles into code / no-access pages
    VirtualFree(page, 0, MEM_RELEASE);
    CHECK_FALSE(write_game_memory(page, &v, 4));  // released
    CHECK_FALSE(write_game_memory(nullptr, &v, 4));
}

// ---- write_game_memory speed and scope ----
namespace {
const int kImageConstant = 0x5eed;  // lives in the test exe's read-only .rdata
}

TEST_CASE("write_game_memory: thousands of writes into a large read-only region stay fast") {
    const size_t kRegion = size_t(256) << 20;  // the game's resource region is ~400 MB; one VirtualQuery scans it all
    auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, kRegion, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    REQUIRE(base != nullptr);
    DWORD old = 0;
    REQUIRE(VirtualProtect(base, kRegion, PAGE_READONLY, &old));
    const ULONGLONG t0 = GetTickCount64();
    bool all = true;
    for (int i = 0; i < 1000; ++i) {
        const uint16_t v = static_cast<uint16_t>(i);
        all &= write_game_memory(base + size_t(i) * 0x10000 + 0x14, &v, 2);
    }
    const ULONGLONG ms = GetTickCount64() - t0;
    CHECK(all);
    CHECK(ms < 150);
    uint16_t back = 0;
    std::memcpy(&back, base + size_t(999) * 0x10000 + 0x14, 2);
    CHECK(back == 999);
    MEMORY_BASIC_INFORMATION mbi{};
    REQUIRE(VirtualQuery(base + 0x10000, &mbi, sizeof(mbi)));
    CHECK((mbi.Protect & 0xff) == PAGE_READONLY);
    VirtualFree(base, 0, MEM_RELEASE);
}

TEST_CASE("write_game_memory unlocks only private memory: image constants and file views stay untouched") {
    const int v = 1;
    CHECK_FALSE(write_game_memory(const_cast<int*>(&kImageConstant), &v, sizeof(v)));
    CHECK(kImageConstant == 0x5eed);
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 0x1000, nullptr);
    REQUIRE(map != nullptr);
    void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0x1000);
    REQUIRE(view != nullptr);
    CHECK_FALSE(write_game_memory(view, &v, sizeof(v)));
    UnmapViewOfFile(view);
    CloseHandle(map);
}

// ---- item names as people type them (give, put, take, stack, items) ----
namespace {
// 105 of the game's 4207 debug names (tests/fixtures/items): the groups that share words
std::vector<NamedItem> real_items() {
    std::ifstream f(std::string(EMF_SOURCE_DIR) + "/tests/fixtures/items/debug_names_sample.txt", std::ios::binary);
    std::vector<NamedItem> out;
    std::string line;
    uint32_t id = 1;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') out.push_back({id++, line});
    }
    return out;
}
uint32_t id_of(const std::vector<NamedItem>& items, const std::string& name) {
    for (const auto& i : items)
        if (i.name == name) return i.id;
    return 0;
}
std::string name_of(const std::vector<NamedItem>& items, uint32_t id) {
    for (const auto& i : items)
        if (i.id == id) return i.name;
    return id ? "?" : "";
}
std::vector<std::string> names_of(const std::vector<NamedItem>& items, const std::vector<size_t>& at) {
    std::vector<std::string> out;
    for (size_t i : at) out.push_back(items[i].name);
    return out;
}
bool listed(const std::vector<NamedItem>& items, const std::vector<size_t>& at, const std::string& name) {
    for (size_t i : at)
        if (items[i].name == name) return true;
    return false;
}
std::string meant(const std::vector<NamedItem>& items, const char* typed) {
    return name_of(items, match_item(items, typed).id);
}
std::vector<std::string> split_words(const std::string& line) {
    std::vector<std::string> out;
    size_t from = 0;
    while (from < line.size()) {
        size_t to = line.find(' ', from);
        if (to == std::string::npos) to = line.size();
        if (to > from) out.push_back(line.substr(from, to - from));
        from = to + 1;
    }
    return out;
}
}  // namespace

TEST_CASE("item names: the words of a debug name") {
    CHECK(item_words("Material_T0_simple_FishingBait") ==
          std::vector<std::string>{"material", "t0", "simple", "fishing", "bait"});
    CHECK(item_words("  Fishing  Bait ") == std::vector<std::string>{"fishing", "bait"});
    CHECK(item_words("wood-log") == std::vector<std::string>{"wood", "log"});
    CHECK(item_words("Tool_Torch01") == std::vector<std::string>{"tool", "torch", "01"});  // a number after a word
    CHECK(item_words("Material_T6_01COMMON_FishingBait_improved") ==
          std::vector<std::string>{"material", "t6", "01", "common", "fishing", "bait", "improved"});
    CHECK(item_words("Ammo_T1_Arrow_Flint_lvl5") == std::vector<std::string>{"ammo", "t1", "arrow", "flint", "lvl5"});
    CHECK(item_words("Weapon_T1_1H_Sword_01") == std::vector<std::string>{"weapon", "t1", "1h", "sword", "01"});
    CHECK(item_words("").empty());
}

TEST_CASE("item names (real names): the exact debug name wins, in any case, also with spaces for '_'") {
    const auto items = real_items();
    REQUIRE(items.size() == 105);
    CHECK(meant(items, "Material_T1_Wood_Logs_Dead") == "Material_T1_Wood_Logs_Dead");
    CHECK(meant(items, "material_t1_wood_logs") == "Material_T1_Wood_Logs");
    CHECK(meant(items, "Material T1 Wood Logs Dead") == "Material_T1_Wood_Logs_Dead");
    CHECK(match_item(items, "Material_T1_Wood_Logs").how == ItemHow::Exact);
}

TEST_CASE("item names (real names): what common words find") {
    const auto items = real_items();
    CHECK(meant(items, "wood logs") == "Material_T1_Wood_Logs");  // 6 match: the others have a word more
    CHECK(match_item(items, "wood logs").matches.size() == 6);
    CHECK(meant(items, "wood log") == "Material_T1_Wood_Logs");   // a part of a word is enough
    CHECK(meant(items, "Simple Bait") == "Material_T0_simple_FishingBait");
    CHECK(meant(items, "bait simple") == "Material_T0_simple_FishingBait");
    CHECK(meant(items, "dog bait") == "Food_T2_Dog_Bait");
    // 13 fishing baits; "simple" is no own word, so the plain one has no word more than was typed
    CHECK(match_item(items, "fishing bait").matches.size() == 13);
    CHECK(meant(items, "fishing bait") == "Material_T0_simple_FishingBait");
    CHECK(meant(items, "torch") == "Tool_Torch01");               // Tool_ and 01 are no own words
    CHECK(meant(items, "moonstone") == "Material_T7_Moonstone");
    CHECK(meant(items, "salt") == "Material_T1_Salt");
    CHECK(meant(items, "spores") == "Material_T1_Fog_Essence01_ShroudSpores");
    // the only match with "hardwood" as a whole word (the props have Hard and Wood)
    CHECK(meant(items, "hardwood") == "Material_T2_Wood_Hardwood");
    CHECK(match_item(items, "hardwood").how == ItemHow::Whole);
}

TEST_CASE("item names (real names): a word that is only part of the one meant item, but whole in others, lists them") {
    const auto items = real_items();
    const auto wolf = match_item(items, "wolf");  // not Wolfsbane: 7 items have Wolf as a whole word
    CHECK(wolf.id == 0);
    CHECK(wolf.matches.size() == 10);
    CHECK(listed(items, wolf.matches, "_Material_TX_Trophy_Wolf_Heavy_head"));
    CHECK(listed(items, wolf.matches, "Material_T5_Wolfsbane"));
    const auto moon = match_item(items, "moon");  // not Moonstone: the PactMoon vanity items have Moon
    CHECK(moon.id == 0);
    CHECK(moon.matches.size() == 14);
}

TEST_CASE("item names (real names): several matches and none meant: no item, every match listed, best first") {
    const auto items = real_items();
    const auto logs = match_item(items, "logs");
    CHECK(logs.id == 0);
    CHECK(names_of(items, logs.matches) ==
          std::vector<std::string>{"Material_T1_Wood_Logs", "Material_T1_Wood_Logs_Dead", "Material_T4_Wood_Logs_Palm",
                                   "Material_T5_Wood_Logs_Pine", "Material_T6_Wood_Logs_Bamboo",
                                   "Material_T6_Wood_Logs_Tropical"});
    CHECK(match_item(items, "bait").matches.size() == 22);
    CHECK(match_item(items, "bait").id == 0);
    CHECK(match_item(items, "stone").id == 0);
    CHECK(match_item(items, "plant fiber").matches.empty());
    CHECK(match_item(items, "").id == 0);
    CHECK(match_item(items, "   ").matches.empty());
    // a number only as a whole number word: "stone 10" is not Tombstone10's "stone" and a 1 in T1
    CHECK(meant(items, "tombstone 10") == "Prop_Decoration_T3_Tombstone10");
    CHECK(match_item(items, "bait 7").matches.empty());  // the 7 of T7 is no number word
}

TEST_CASE("item names (real names): search lists every match, best first; nothing typed lists every item") {
    const auto items = real_items();
    const auto found = names_of(items, search_items(items, "wood logs"));
    REQUIRE(found.size() == 6);
    CHECK(found[0] == "Material_T1_Wood_Logs");
    CHECK(search_items(items, "").size() == items.size());
    CHECK(search_items(items, "nothing like it").empty());
}

TEST_CASE("item names (real names): plain names (the console's list) give the same answers; the id is the index + 1") {
    const auto items = real_items();
    std::vector<std::string> names;
    for (const auto& i : items) names.push_back(i.name);
    for (const char* typed : {"wood logs", "wood log", "torch", "hardwood", "wolf", "logs", "Material_T1_Wood_Logs",
                              "material t1 wood logs dead", "fishing bait", "tombstone 10", "", "nothing like it"}) {
        CAPTURE(typed);
        const ItemMatch a = match_item(items, typed);
        const ItemMatch b = match_item(names, typed);
        CHECK(b.matches == a.matches);
        CHECK(b.how == a.how);
        CHECK(b.own_words_typed == a.own_words_typed);
        CHECK((b.id ? items[b.id - 1].id : 0u) == a.id);
        CHECK(search_items(names, typed) == search_items(items, typed));
    }
}

TEST_CASE("item names (real names): a command's numbers are the plain numbers at the end; a name may end in a number") {
    const auto items = real_items();
    const auto read = [&](const char* line, size_t max_numbers) {
        return read_item_args(items, split_words(line), max_numbers);
    };
    // take/put (count, entity): the 01 belongs to the name, so 5 is the count, not an entity
    auto a = read("tool torch 01 5", 2);
    CHECK(name_of(items, a.id) == "Tool_Torch01");
    CHECK(a.numbers == std::vector<std::string>{"5"});
    a = read("weapon t1 1h sword 01 3", 4);
    CHECK(name_of(items, a.id) == "Weapon_T1_1H_Sword_01");
    CHECK(a.numbers == std::vector<std::string>{"3"});
    a = read("weapon t1 2h bow 01", 1);  // give: no count
    CHECK(name_of(items, a.id) == "Weapon_T1_2H_Bow_01");
    CHECK(a.numbers.empty());
    // without the leading zero, the split whose name has exactly an item's words wins
    a = read("tool torch 1 5", 2);
    CHECK(name_of(items, a.id) == "Tool_Torch01");
    CHECK(a.numbers == std::vector<std::string>{"5"});
    // the usual case: the numbers at the end
    a = read("wood logs 20 1025", 4);
    CHECK(name_of(items, a.id) == "Material_T1_Wood_Logs");
    CHECK(a.numbers == (std::vector<std::string>{"20", "1025"}));
    a = read("fishing bait 4", 1);  // not Material_T6_04EPIC_FishingBait_Improved
    CHECK(name_of(items, a.id) == "Material_T0_simple_FishingBait");
    CHECK(a.numbers == std::vector<std::string>{"4"});
    a = read("torch 3", 1);  // 3 torches, not Tool_Torch03_Wax (wax was not typed)
    CHECK(name_of(items, a.id) == "Tool_Torch01");
    CHECK(a.numbers == std::vector<std::string>{"3"});
    // no item: the candidates of the name with the numbers taken off ("stone", not Tombstone10)
    a = read("stone 10", 1);
    CHECK(a.id == 0);
    CHECK(a.name == "stone");
    CHECK(a.numbers == std::vector<std::string>{"10"});
    CHECK(a.match.matches.size() == 16);
}

TEST_CASE("item names: a quoted name is the whole name; readings that name different items are unclear") {
    const std::vector<NamedItem> items{{1, "Tool_Torch"}, {2, "Tool_Torch_2"}, {3, "Material_T1_Wood_Logs"}};
    auto a = read_item_args(items, {"tool torch", "2"}, 1);  // "tool torch" 2
    CHECK(a.id == 1);
    CHECK(a.numbers == std::vector<std::string>{"2"});
    CHECK(read_item_args(items, {"wood logs", "many"}, 1).bad);
    CHECK(read_item_args(items, {"wood logs", "1", "2"}, 1).bad);
    a = read_item_args(items, {"tool", "torch", "2"}, 1);  // 2 x Tool_Torch, or 1 x Tool_Torch_2
    CHECK(a.id == 0);
    REQUIRE(a.readings.size() == 2);
    CHECK(a.readings[0] == std::make_pair(uint32_t(2), std::vector<std::string>{}));
    CHECK(a.readings[1] == std::make_pair(uint32_t(1), std::vector<std::string>{"2"}));
    a = read_item_args(items, {"wood", "logs", "many"}, 1);  // not a number: part of the name
    CHECK(a.id == 0);
    CHECK(a.name == "wood logs many");
}
