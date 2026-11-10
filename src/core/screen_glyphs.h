#pragma once
// The characters EMF's screens can draw. The game thread collects the characters the item names need beyond the
// overlay's base ranges (want); the render thread adds them to the font atlas and says which ones every face has
// (baked); ItemNames asks can_draw before it puts a name on EMF's screens, so a name the atlas cannot draw shows in
// English instead of "?" boxes.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emf::core {

// The code points of UTF-8 text; a byte that does not start a valid sequence gives U+FFFD and moves on one byte.
std::vector<uint32_t> utf8_code_points(std::string_view s);

// The overlay's base glyphs, which every face always gets (kFontRanges): Basic Latin and Latin-1 (0x20-0xFF), Latin
// Extended-A (0x100-0x17F), Cyrillic and its Supplement (0x400-0x52F).
bool in_base_ranges(uint32_t cp);

// At most this many characters are added to the atlas (a few hundred for a language's item names; the cap keeps a
// broken table from making the atlas huge).
inline constexpr size_t kMaxExtraGlyphs = 4000;

// The characters of `texts` outside the base ranges (U+FFFD and control characters left out): sorted, unique, the
// kMaxExtraGlyphs smallest.
std::vector<uint32_t> extra_glyphs(const std::vector<std::string>& texts);

// What the game thread asks for: the characters of the item names of one text table (key) in one language.
struct GlyphWant {
    uint64_t key = 0;
    uint32_t language = 0;
    std::vector<uint32_t> glyphs;
    uint64_t version = 0;  // 0 until the first want
};

class ScreenGlyphs {
public:
    // The game thread: these are the characters of the table `key` (bumps the version the render thread watches).
    void want(uint64_t key, uint32_t language, std::vector<uint32_t> glyphs);
    GlyphWant wanted() const;
    uint64_t wanted_version() const { return version_.load(std::memory_order_acquire); }  // each frame, cheap
    uint64_t collected_key() const;
    // The render thread, after building the atlas for the want `version`: the characters every face can draw.
    void baked(uint64_t version, std::vector<uint32_t> drawable);
    // Whether EMF's screens can draw `utf8`: nothing until the first bake; then true when each character is in the base
    // ranges or in the newest bake (never one above U+FFFF: ImGui's characters are 16-bit here).
    std::optional<bool> can_draw(std::string_view utf8) const;

private:
    mutable std::mutex mu_;
    GlyphWant want_;
    std::atomic<uint64_t> version_{0};
    bool baked_any_ = false;
    uint64_t baked_version_ = 0;
    std::vector<uint32_t> drawable_;  // sorted
};

// The core's instance.
ScreenGlyphs& screen_glyphs();

}  // namespace emf::core
