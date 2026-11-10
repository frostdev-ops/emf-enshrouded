// The game's text (LocaPool): the sanity checks, the clean copy, the languages, and GameText over a fake pool.
#include <doctest.h>

#include <cstring>
#include <map>
#include <string>

#include "core/game_text.h"

using namespace emf::core;

namespace {

// A fake LocaPool: the three sanity values and the active table pointer at their offsets.
struct FakePool {
    alignas(16) uint8_t mem[0x128]{};
    FakePool(uint32_t language = 1, uint64_t table = 0x1234) {
        set32(0x18, 0x69691905u);
        set32(0x88, 1);
        set32(0xD8, language);
        set64(0x100, table);
    }
    void set32(size_t off, uint32_t v) { std::memcpy(mem + off, &v, 4); }
    void set64(size_t off, uint64_t v) { std::memcpy(mem + off, &v, 8); }
};

FakePool* g_pool = nullptr;
std::map<uint32_t, std::string> g_active, g_english;
uint8_t g_error = 0;
bool g_fault = false, g_swap_table = false;
int g_slot_calls = 0;
void* g_slot_seen = nullptr;

LocaResult* fake_lookup(LocaResult* out, void* pool, uint32_t tag) {
    std::memset(out, 0, sizeof *out);
    if (g_fault) {
        volatile int* p = nullptr;
        *p = 1;  // an access violation inside the game's code
    }
    if (g_swap_table && g_pool) g_pool->set64(0x100, 0x9999);  // a language change finished meanwhile
    (void)pool;
    if (g_error) {
        out->error = g_error;
        return out;
    }
    auto it = g_active.find(tag);
    if (it == g_active.end()) {
        out->error = 0x1F;
        return out;
    }
    out->text = it->second.data();
    out->length = it->second.size();
    return out;
}

bool fake_slot_find(LocaStringView* out, void* slot, uint32_t tag) {
    ++g_slot_calls;
    g_slot_seen = slot;
    auto it = g_english.find(tag);
    if (it == g_english.end()) return false;
    out->text = it->second.data();
    out->length = it->second.size();
    return true;
}

GameText make(FakePool* pool) {
    g_pool = pool;
    g_error = 0;
    g_fault = false;
    g_swap_table = false;
    g_slot_calls = 0;
    g_active = {{0x54F69AC6u, "Wood Logs"}, {0x1u, "<lorename>Jezmina</lorename>"}};
    g_english = {{0x54F69AC6u, "Wood Logs"}};
    LocaAccess a;
    a.pool = [pool] { return pool ? pool->mem : nullptr; };
    a.lookup = &fake_lookup;
    a.slot_find = &fake_slot_find;
    return GameText(std::move(a));
}

}  // namespace

TEST_CASE("clean_game_text: markup removed, code points turned into characters, kept short and valid") {  // Review Focus 3
    CHECK(clean_game_text("Wood Logs") == std::optional<std::string>("Wood Logs"));
    CHECK(clean_game_text("<lorename>Jezmina</lorename>") == std::optional<std::string>("Jezmina"));
    CHECK(clean_game_text("<b>Bronze</b> Sword") == std::optional<std::string>("Bronze Sword"));
    CHECK(clean_game_text("20<u+00d7>") == std::optional<std::string>("20\xC3\x97"));
    CHECK(clean_game_text("a <b") == std::optional<std::string>("a <b"));  // an unterminated tag stays
    CHECK(clean_game_text("  Torch  ") == std::optional<std::string>("Torch"));
    CHECK_FALSE(clean_game_text(""));
    CHECK_FALSE(clean_game_text("<b></b>"));
    CHECK_FALSE(clean_game_text("\xff\xfe"));
    std::string long_text;
    for (int i = 0; i < 400; ++i) long_text += "\xC3\xA9";  // é
    const auto c = clean_game_text(long_text);
    REQUIRE(c);
    CHECK(c->size() <= 255);
    CHECK(c->size() % 2 == 0);  // cut at a character boundary
}

TEST_CASE("language_code and language_needs_extra_font") {
    CHECK(std::string(language_code(1)) == "en_us");
    CHECK(std::string(language_code(3)) == "de_de");
    CHECK(std::string(language_code(23)) == "es_419");
    CHECK(std::string(language_code(34)) == "uk_ua");
    CHECK(std::string(language_code(0)).empty());
    CHECK(std::string(language_code(35)).empty());
    for (uint32_t id : {11u, 13u, 17u, 20u, 21u}) CHECK(language_needs_extra_font(id));
    for (uint32_t id : {1u, 3u, 9u, 18u, 34u, 0u}) CHECK_FALSE(language_needs_extra_font(id));
}

TEST_CASE("loca_pool_sane: the three values the game writes when it creates the pool") {
    FakePool p;
    CHECK(loca_pool_sane(p.mem));
    p.set32(0x18, 1);
    CHECK_FALSE(loca_pool_sane(p.mem));
    FakePool q;
    q.set32(0x88, 2);
    CHECK_FALSE(loca_pool_sane(q.mem));
    FakePool r(99);
    CHECK_FALSE(loca_pool_sane(r.mem));
    CHECK_FALSE(loca_pool_sane(nullptr));
}

TEST_CASE("GameText: the player's language, its text, cleaned") {
    FakePool p(3);
    GameText t = make(&p);
    CHECK(t.language() == 3);
    CHECK(t.text(0x54F69AC6u) == std::optional<std::string>("Wood Logs"));
    CHECK(t.text(0x1u) == std::optional<std::string>("Jezmina"));
    CHECK_FALSE(t.text(0x2u));  // unknown tag (0x1F)
    CHECK(t.table_key() != 0);
}

TEST_CASE("GameText: a language without a table reports en_us (1)") {
    FakePool p(30, 0);
    GameText t = make(&p);
    CHECK(t.language() == 1);
}

TEST_CASE("GameText: the game's errors, a fault and a table change during the call give nothing") {  // Review Focus 1
    FakePool p(3);
    GameText t = make(&p);
    for (uint8_t e : {uint8_t{3}, uint8_t{0x12}, uint8_t{0x1F}}) {
        g_error = e;
        CHECK_FALSE(t.text(0x54F69AC6u));
    }
    g_error = 0;
    g_fault = true;
    CHECK_FALSE(t.text(0x54F69AC6u));
    g_fault = false;
    g_swap_table = true;
    CHECK_FALSE(t.text(0x54F69AC6u));
    g_swap_table = false;
    CHECK(t.text(0x54F69AC6u) == std::optional<std::string>("Wood Logs"));  // works again afterwards
    CHECK_FALSE(t.text(0));                                                 // the null tag
}

TEST_CASE("GameText: no pool, an insane pool, or no functions: nothing, never a fault") {  // Review Focus 2
    GameText none = make(nullptr);
    CHECK(none.language() == 0);
    CHECK_FALSE(none.text(0x54F69AC6u));
    CHECK_FALSE(none.english(0x54F69AC6u));
    CHECK(none.table_key() == 0);
    FakePool bad(3);
    bad.set32(0x18, 0);
    GameText b = make(&bad);
    CHECK(b.language() == 0);
    CHECK_FALSE(b.text(0x54F69AC6u));
    GameText empty{LocaAccess{}};
    CHECK(empty.language() == 0);
    CHECK_FALSE(empty.text(0x54F69AC6u));
}

TEST_CASE("GameText: English from the fallback slot, or from the active table while English is active") {  // Review Focus 5
    FakePool de(3);
    GameText t = make(&de);
    g_active[0x54F69AC6u] = "Holzscheite";
    CHECK(t.text(0x54F69AC6u) == std::optional<std::string>("Holzscheite"));
    CHECK(t.english(0x54F69AC6u) == std::optional<std::string>("Wood Logs"));
    CHECK(g_slot_calls == 1);
    CHECK(g_slot_seen == static_cast<void*>(de.mem + 0x90));
    FakePool en(1);
    GameText e = make(&en);
    g_english.clear();  // the fallback slot is empty while English is active
    CHECK(e.english(0x54F69AC6u) == std::optional<std::string>("Wood Logs"));
    CHECK(g_slot_calls == 0);
}
