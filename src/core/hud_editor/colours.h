#pragma once
// The vitals' bar colours (docs/engine_parts/re_ui_scopes.md, "Bar colours"): the player's colours as text, and EMF's
// copy of the game's colour block for one bar (keen::FbUiAnimatedHealthBarColors: six keen::UiColor, 0x30 bytes). The
// bar detour passes the game's bar function that copy instead of the game's own block. Pure functions.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace emf::core::hud {

// The parts of a bar a player can colour.
enum class BarPart : uint8_t { Fill, Empty, End, Damage, Heal };
constexpr size_t kBarPartCount = 5;
// "fill", "empty", "end", "damage", "heal".
const char* bar_part_id(BarPart p);
// A part by its id, ignoring case.
std::optional<BarPart> find_bar_part(std::string_view word);

// A player's colour, or the game's own (not set).
struct Colour {
    uint32_t rgba = 0;       // 0xRRGGBBAA; AA is 0xff and not used when !has_alpha
    bool set = false;        // false: the game's own colour
    bool has_alpha = false;  // false: the game's alpha for this part
    bool operator==(const Colour&) const = default;
};

struct BarColours {
    std::array<Colour, kBarPartCount> part{};  // by BarPart
    bool any() const;
    bool operator==(const BarColours&) const = default;
};

// "#rrggbb" (the game's alpha kept), "#rrggbbaa", the same without '#', or "game" (the game's own colour); hex digits in
// any case. Neither field set: not a colour.
struct ColourText {
    std::optional<Colour> colour;
    bool game = false;
};
ColourText parse_colour(std::string_view text);
// "#rrggbb" or "#rrggbbaa" in lower case; "game" when not set.
std::string colour_text(const Colour& c);

// The game's keen::UiColor: value is the bytes R, G, B, A (a little-endian u32 reads 0xAABBGGRR); intensity a brightness
// factor (1.0 by default).
struct UiColour {
    uint32_t value;
    float intensity;
};
static_assert(sizeof(UiColour) == 8);
using StyleBlock = std::array<UiColour, 6>;
static_assert(sizeof(StyleBlock) == 0x30);
enum StyleField : size_t { kFrame = 0, kRange = 1, kFill = 2, kFillEnd = 3, kDamage = 4, kHeal = 5 };

// How far the bright end of the fill moves toward white from the fill when the player set the fill and not the end.
constexpr float kEndLighten = 0.40f;

uint32_t rgba_to_game(uint32_t rgba);
uint32_t game_to_rgba(uint32_t value);
// The game's colour with the player's: not set gives `game`; else the player's red, green and blue, the player's alpha
// when it has one (else the game's), and always the game's intensity.
UiColour with_colour(UiColour game, const Colour& c);
// R, G and B each moved toward 255 by the fraction (0..1, rounded); alpha kept.
uint32_t lighten(uint32_t rgba, float toward_white);
// EMF's copy for one bar: fill, empty (the game's range), end, damage and heal from the player's colours where set; the
// frame is never changed here (the panel is a separate write). The end follows the fill (kEndLighten toward white, the
// game's alpha for the end) when the fill is set and the end is not.
StyleBlock recoloured(const StyleBlock& game, const BarColours& c);
// The game's own text form of a colour: "rrggbb", then "-aa" when alpha is not 0xff, then "*i.ii" when the intensity is
// not 1.0 (for example "4fd17c-c0*1.20").
std::string ui_colour_text(UiColour c);

}  // namespace emf::core::hud
