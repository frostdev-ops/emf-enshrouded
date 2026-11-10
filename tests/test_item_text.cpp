// Item names: the name tag of an item, the shown name for EMF's screens or the game's text, and the cache per language.
#include <windows.h>

#include <doctest.h>

#include <cstring>
#include <map>
#include <string>

#include "common/fnv.h"
#include "common/offsets.h"
#include "core/game_text.h"
#include "core/item_text.h"
#include "fake_world.h"

using namespace emf::core;

namespace {

struct Pool {
    alignas(16) uint8_t mem[0x128]{};
    explicit Pool(uint32_t language) {
        put32(0x18, 0x69691905u);
        put32(0x88, 1);
        put32(0xD8, language);
        put64(0x100, 0x1000);
        put64(0xB0, 0x2000);  // the fallback slot's table
    }
    void put32(size_t o, uint32_t v) { std::memcpy(mem + o, &v, 4); }
    void put64(size_t o, uint64_t v) { std::memcpy(mem + o, &v, 8); }
};

std::map<uint32_t, std::string> g_shown, g_english;
int g_lookups = 0;

LocaResult* lookup(LocaResult* out, void*, uint32_t tag) {
    ++g_lookups;
    std::memset(out, 0, sizeof *out);
    auto it = g_shown.find(tag);
    if (it == g_shown.end()) {
        out->error = 0x1F;
        return out;
    }
    out->text = it->second.data();
    out->length = it->second.size();
    return out;
}

bool slot_find(LocaStringView* out, void*, uint32_t tag) {
    auto it = g_english.find(tag);
    if (it == g_english.end()) return false;
    out->text = it->second.data();
    out->length = it->second.size();
    return true;
}

GameText text_over(Pool& p) {
    LocaAccess a;
    a.pool = [&p] { return p.mem; };
    a.lookup = &lookup;
    a.slot_find = &slot_find;
    return GameText(std::move(a));
}

// An item with a name GUID at ItemInfo+0x48; returns its tag.
uint32_t add_named(emf::test::FakeItemRegistry& r, uint32_t id, const char* debug, uint8_t seed) {
    uint8_t* info = r.add(id, seed, 50, debug);
    uint8_t g[16];
    for (int i = 0; i < 16; ++i) g[i] = static_cast<uint8_t>(seed * 3 + i + 1);
    std::memcpy(info + kItemInfoName, g, 16);
    return emf::fnv1a32(g, 16);
}

}  // namespace

TEST_CASE("item_name_tag: FNV-1a-32 of the name GUID at ItemInfo+0x48; none for a null GUID or no info") {
    emf::test::FakeItemRegistry r;
    const uint32_t tag = add_named(r, 0x11, "Material_T1_Wood_Logs", 7);
    CHECK(item_name_tag(item_data_ptr(r.reg, 0x11)) == tag);
    r.add(0x22, 9, 1, "Nameless");  // the GUID at +0x48 stays zero
    CHECK(item_name_tag(item_data_ptr(r.reg, 0x22)) == 0);
    CHECK(item_name_tag(nullptr) == 0);
}

TEST_CASE("ItemNames: the player's language, and English on EMF's screens where they cannot draw the script") {  // Review Focus 5
    emf::test::FakeItemRegistry r;
    const uint32_t tag = add_named(r, 0x11, "Material_T1_Wood_Logs", 7);
    g_english = {{tag, "Wood Logs"}};
    Pool de(3);
    GameText de_text = text_over(de);
    g_shown = {{tag, "Holzscheite"}};
    ItemNames names(de_text, [](uint32_t) { return false; });
    CHECK(names.name(r.reg, 0x11, NameFor::Game) == std::optional<std::string>("Holzscheite"));
    CHECK(names.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("Holzscheite"));
    CHECK_FALSE(names.name(r.reg, 0x99, NameFor::Game));  // an unknown item
    Pool ja(11);
    GameText ja_text = text_over(ja);
    g_shown = {{tag, "\xE4\xB8\xB8\xE5\xA4\xAA"}};  // 丸太
    ItemNames ja_names(ja_text, [](uint32_t) { return false; });
    CHECK(ja_names.name(r.reg, 0x11, NameFor::Game) == std::optional<std::string>("\xE4\xB8\xB8\xE5\xA4\xAA"));
    CHECK(ja_names.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("Wood Logs"));
    ItemNames ja_font(ja_text, [](uint32_t lang) { return lang == 11; });  // part 2's font is there
    CHECK(ja_font.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("\xE4\xB8\xB8\xE5\xA4\xAA"));
}

TEST_CASE("ItemNames: cached per language; a language change (the table key) looks the names up again") {  // Review Focus 1
    emf::test::FakeItemRegistry r;
    const uint32_t tag = add_named(r, 0x11, "Material_T1_Wood_Logs", 7);
    Pool p(3);
    GameText t = text_over(p);
    g_shown = {{tag, "Holzscheite"}};
    ItemNames names(t, [](uint32_t) { return false; });
    g_lookups = 0;
    CHECK(names.name(r.reg, 0x11, NameFor::Game) == std::optional<std::string>("Holzscheite"));
    CHECK(names.name(r.reg, 0x11, NameFor::Game) == std::optional<std::string>("Holzscheite"));
    CHECK(g_lookups == 1);  // the second from the cache
    p.put32(0xD8, 5);        // French now, its table loaded
    p.put64(0x100, 0x3000);
    g_shown = {{tag, "B\xC3\xBB" "ches"}};
    CHECK(names.name(r.reg, 0x11, NameFor::Game) == std::optional<std::string>("B\xC3\xBB" "ches"));
    CHECK(g_lookups == 2);
}

TEST_CASE("ItemNames: no game text (the dedicated server, before the menu): nothing") {  // Review Focus 2
    emf::test::FakeItemRegistry r;
    add_named(r, 0x11, "Material_T1_Wood_Logs", 7);
    GameText none{LocaAccess{}};
    ItemNames names(none, [](uint32_t) { return false; });
    CHECK_FALSE(names.name(r.reg, 0x11, NameFor::Game));
    CHECK_FALSE(names.name(r.reg, 0x11, NameFor::Screen));
    const auto all = names.named(r.reg);
    REQUIRE(all.size() == 1);
    CHECK(all[0].name == "Material_T1_Wood_Logs");
    CHECK(all[0].shown.empty());
}

TEST_CASE("ItemNames: named lists every item with its debug name and its shown name (empty when it has none)") {
    emf::test::FakeItemRegistry r;
    const uint32_t tag = add_named(r, 0x11, "Material_T1_Wood_Logs", 7);
    r.add(0x22, 9, 1, "Nameless");
    Pool p(3);
    GameText t = text_over(p);
    g_shown = {{tag, "Holzscheite"}};
    ItemNames names(t, [](uint32_t) { return false; });
    const auto all = names.named(r.reg);
    REQUIRE(all.size() == 2);
    for (const auto& n : all) {
        if (n.id == 0x11) {
            CHECK(n.name == "Material_T1_Wood_Logs");
            CHECK(n.shown == "Holzscheite");
        } else {
            CHECK(n.name == "Nameless");
            CHECK(n.shown.empty());
        }
    }
}
