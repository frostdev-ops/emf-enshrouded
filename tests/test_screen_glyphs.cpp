// The characters EMF's screens can draw: UTF-8 code points, the base ranges, the characters item names need beyond
// them, and ScreenGlyphs between the game thread (what is wanted) and the render thread (what was baked).
#include <doctest.h>

#include <string>
#include <vector>

#include "core/screen_glyphs.h"

using namespace emf::core;

TEST_CASE("utf8_code_points: ASCII, CJK, and an invalid byte as U+FFFD") {
    CHECK(utf8_code_points("Wood") == std::vector<uint32_t>{0x57, 0x6F, 0x6F, 0x64});
    CHECK(utf8_code_points("\xE4\xB8\xB8\xE5\xA4\xAA") == std::vector<uint32_t>{0x4E38, 0x592A});  // 丸太
    CHECK(utf8_code_points("\xFF") == std::vector<uint32_t>{0xFFFD});
    CHECK(utf8_code_points("a\xE4\xB8") == std::vector<uint32_t>{0x61, 0xFFFD, 0xFFFD});  // cut off: one per byte
    CHECK(utf8_code_points("\xF0\x9F\x98\x80") == std::vector<uint32_t>{0x1F600});
    CHECK(utf8_code_points("").empty());
}

TEST_CASE("in_base_ranges: the overlay's base glyphs (Latin, Latin-1, Latin Extended-A, Cyrillic)") {
    for (uint32_t c : {0x41u, 0xE9u, 0x17Fu, 0x416u, 0x52Fu, 0x20u}) CHECK(in_base_ranges(c));
    for (uint32_t c : {0x1EA1u, 0x4E38u, 0x2019u, 0x1Fu, 0x180u, 0x530u}) CHECK_FALSE(in_base_ranges(c));
}

TEST_CASE("extra_glyphs: sorted, unique, outside the base ranges, at most kMaxExtraGlyphs (the smallest kept)") {
    const std::vector<std::string> names = {"Wood Logs", "B\xE1\xBB\x93ng", "\xE4\xB8\xB8\xE5\xA4\xAA",
                                            "\xE4\xB8\xB8\xE3\x81\x84"};  // Bồng, 丸太, 丸い
    CHECK(extra_glyphs(names) == std::vector<uint32_t>{0x1ED3, 0x3044, 0x4E38, 0x592A});
    std::vector<std::string> many;
    for (uint32_t c = 0x4E00; c < 0x4E00 + kMaxExtraGlyphs + 10; ++c) {
        std::string s;
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
        many.push_back(s);
    }
    const auto kept = extra_glyphs(many);
    REQUIRE(kept.size() == kMaxExtraGlyphs);
    CHECK(kept.front() == 0x4E00);
    CHECK(kept.back() == 0x4E00 + kMaxExtraGlyphs - 1);
}

TEST_CASE("ScreenGlyphs: unknown until the first bake; then base characters and the baked ones are drawable") {
    ScreenGlyphs g;
    CHECK_FALSE(g.can_draw("Wood").has_value());
    g.baked(1, {0x4E38});
    CHECK(g.can_draw("Wood") == std::optional<bool>(true));
    CHECK(g.can_draw("\xE4\xB8\xB8") == std::optional<bool>(true));                     // 丸
    CHECK(g.can_draw("\xE4\xB8\xB8\xE5\xA4\xAA") == std::optional<bool>(false));       // 丸太: 太 is not baked
    CHECK(g.can_draw("\xF0\x9F\x98\x80") == std::optional<bool>(false));               // above U+FFFF
    CHECK(g.can_draw("\xFF") == std::optional<bool>(false));                           // garbage
    g.baked(2, {});  // a newer bake without it
    CHECK(g.can_draw("\xE4\xB8\xB8") == std::optional<bool>(false));
}

TEST_CASE("ScreenGlyphs: want bumps the version and notes the collected table") {
    ScreenGlyphs g;
    CHECK(g.collected_key() == 0);
    const uint64_t v0 = g.wanted().version;
    g.want(7, 11, {0x4E38, 0x592A});
    const GlyphWant w = g.wanted();
    CHECK(w.version == v0 + 1);
    CHECK(w.key == 7);
    CHECK(w.language == 11);
    CHECK(w.glyphs == std::vector<uint32_t>{0x4E38, 0x592A});
    CHECK(g.collected_key() == 7);
}
