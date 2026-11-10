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
#include "core/screen_glyphs.h"
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

// EMF's screens have not said yet what they can draw (before the overlay's first atlas).
const ScreenCanDraw kUnknown = [](std::string_view) -> std::optional<bool> { return std::nullopt; };

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
    ItemNames names(de_text, kUnknown);
    CHECK(names.name(r.reg, 0x11, NameFor::Game) == std::optional<std::string>("Holzscheite"));
    CHECK(names.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("Holzscheite"));
    CHECK_FALSE(names.name(r.reg, 0x99, NameFor::Game));  // an unknown item
    Pool ja(11);
    GameText ja_text = text_over(ja);
    g_shown = {{tag, "\xE4\xB8\xB8\xE5\xA4\xAA"}};  // 丸太
    ItemNames ja_names(ja_text, kUnknown);
    CHECK(ja_names.name(r.reg, 0x11, NameFor::Game) == std::optional<std::string>("\xE4\xB8\xB8\xE5\xA4\xAA"));
    CHECK(ja_names.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("Wood Logs"));
    ItemNames ja_font(ja_text, [](std::string_view) -> std::optional<bool> { return true; });  // part 2: drawable
    CHECK(ja_font.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("\xE4\xB8\xB8\xE5\xA4\xAA"));
}

TEST_CASE("ItemNames: cached per language; a language change (the table key) looks the names up again") {  // Review Focus 1
    emf::test::FakeItemRegistry r;
    const uint32_t tag = add_named(r, 0x11, "Material_T1_Wood_Logs", 7);
    Pool p(3);
    GameText t = text_over(p);
    g_shown = {{tag, "Holzscheite"}};
    ItemNames names(t, kUnknown);
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
    ItemNames names(none, kUnknown);
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
    ItemNames names(t, kUnknown);
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

TEST_CASE("ItemNames: EMF's screens use a name only when they can draw it, else English, in any language") {  // part 2
    emf::test::FakeItemRegistry r;
    const uint32_t tag = add_named(r, 0x11, "Material_T1_Wood_Logs", 7);
    g_english = {{tag, "Wood Logs"}};
    Pool vi(19);
    GameText t = text_over(vi);
    g_shown = {{tag, "G\xE1\xBB\x97"}};  // Gỗ
    std::optional<bool> verdict;
    ItemNames names(t, [&verdict](std::string_view) { return verdict; });
    CHECK(names.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("G\xE1\xBB\x97"));  // not known yet
    verdict = false;
    CHECK(names.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("Wood Logs"));
    CHECK(names.name(r.reg, 0x11, NameFor::Game) == std::optional<std::string>("G\xE1\xBB\x97"));  // the chat: always
    verdict = true;
    CHECK(names.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("G\xE1\xBB\x97"));
    Pool ja(11);
    GameText ja_text = text_over(ja);
    g_shown = {{tag, "\xE4\xB8\xB8\xE5\xA4\xAA"}};
    ItemNames ja_names(ja_text, [&verdict](std::string_view) { return verdict; });
    verdict = false;  // the font lacks a character
    CHECK(ja_names.name(r.reg, 0x11, NameFor::Screen) == std::optional<std::string>("Wood Logs"));
}

TEST_CASE("pump_screen_glyphs: once per text table, the characters of every item's name in the player's language") {
    emf::test::FakeItemRegistry r;
    const uint32_t wood = add_named(r, 0x11, "Material_T1_Wood_Logs", 7);
    const uint32_t berry = add_named(r, 0x22, "Food_Berry", 9);
    Pool ja(11);
    GameText t = text_over(ja);
    const std::string logs = "\xE4\xB8\xB8\xE5\xA4\xAA", berries = "\xE3\x83\x99\xE3\x83\xAA\xE3\x83\xBC";  // 丸太, ベリー
    g_shown = {{wood, logs}, {berry, berries}};
    ItemNames names(t, kUnknown);
    ScreenGlyphs g;
    pump_screen_glyphs(r.reg, names, t, g);
    const GlyphWant w = g.wanted();
    CHECK(w.version == 1);
    CHECK(w.language == 11);
    CHECK(w.key == t.table_key());
    CHECK(w.glyphs == extra_glyphs({logs, berries}));
    pump_screen_glyphs(r.reg, names, t, g);
    CHECK(g.wanted().version == 1);  // the same table: nothing new
    ja.put64(0x100, 0x5000);          // the table loaded again (a language switch and back)
    pump_screen_glyphs(r.reg, names, t, g);
    CHECK(g.wanted().version == 2);
    ScreenGlyphs none_yet;
    pump_screen_glyphs(nullptr, names, t, none_yet);  // no world's registry yet: nothing, tried again later
    CHECK(none_yet.wanted().version == 0);
    GameText no_text{LocaAccess{}};
    pump_screen_glyphs(r.reg, names, no_text, none_yet);  // the dedicated server
    CHECK(none_yet.wanted().version == 0);
}
