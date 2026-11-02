#pragma once
// The game-style look's layout and drawing helpers: where HUD lines and notes go on screens of any size, the notes'
// band that fades at its ends, the text's soft shadow, and the navy and gold theme for the console. The layout is pure
// (unit-tested at 720p, 1080p and 4K); the drawing helpers only add to an ImDrawList.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "hud_model.h"
#include "imgui.h"
#include "look_settings.h"

namespace emf::core {

// The shadow's strength (tune it here after looking at bright snow and sky in the game).
inline constexpr float kShadowAlpha = 0.70f;      // the drop shadow below and right of the text
inline constexpr float kShadowHaloAlpha = 0.30f;  // a dark 1 px ring around the glyphs, for bright backgrounds
// The notes' band: #1b1c22 at about 70 % in its middle, fading to nothing at its ends (0xRRGGBBAA).
inline constexpr uint32_t kBandColour = 0x1B1C22B3u;

// How on-screen text (HUD lines, notes, wheels) is drawn: from LookSettings and the atlas's fonts.
struct TextLook {
    HudStyle style = HudStyle::Game;
    bool shadow = true;
    uint32_t colour = 0xFFFFF4FFu;  // 0xRRGGBBAA, for text whose mod chose no colour
    uint32_t accent = 0xC0B074FFu;
    NotesPlace notes_place = NotesPlace::TopCentre;
    ImFont* font = nullptr;  // the HUD font (null: ImGui's current font)
    ImFont* bold = nullptr;  // titles and notes (null: `font`)
};

// One drawing of a text: moved by `offset`, in black at `alpha` times the text's own alpha (a shadow), or the text.
struct TextPass {
    ImVec2 offset;
    float alpha = 1.0f;
    bool shadow = false;
};
// The passes for a text of `pixel_size`: in game style with the shadow on, a halo and a drop shadow, then the text; in
// any other case the text alone. A fixed array: drawn for every text, every frame.
struct TextPasses {
    TextPass pass[6];
    size_t count = 0;
    size_t size() const { return count; }
    const TextPass* begin() const { return pass; }
    const TextPass* end() const { return pass + count; }
    const TextPass& operator[](size_t i) const { return pass[i]; }
    const TextPass& back() const { return pass[count - 1]; }
};
TextPasses text_passes(HudStyle style, bool shadow, float pixel_size);
// Removes the last UTF-8 character of `s` (all its bytes).
void utf8_pop_back(std::string& s);
// How far the drop shadow sits from the text: 1 px at 1080p, 2 at 4K, at most 3.
float shadow_offset(float pixel_size);

// The top-left corner of HUD line `index` of the `count` lines at `place`, the line `text` big, on a `screen`-sized
// screen with lines `line_h` high. The right places align lines to the right edge, the bottom places grow upwards;
// the mods' order stays top to bottom everywhere. The margin to the screen's edges is one line height.
ImVec2 hud_line_position(HudPlace place, size_t index, size_t count, ImVec2 text, float line_h, ImVec2 screen);

// Where a note shows: the band (fades included), how far it fades out at each end, and the text block inside it.
struct NoteBand {
    ImVec2 min, max;
    float fade_left = 0.0f, fade_right = 0.0f;
    ImVec2 text_min, text_max;
};
// The band for a block of note text `text` big. Top centre: centred under the game's compass, fading at both ends;
// top left: from the left edge, at EMF's old place; right: from the right edge, a little above the middle.
NoteBand note_band(NotesPlace place, ImVec2 text, float line_h, ImVec2 screen);
// `text` as rows at most `width` wide (measured by `measure`): broken at spaces and at line breaks; a word wider than a
// row is cut between UTF-8 characters. At most `max_rows`: when text is left over, the last row ends in "...".
inline constexpr size_t kNoteMaxRows = 3;
std::vector<std::string> wrap_rows(const std::string& text, float width,
                                   const std::function<float(const std::string&)>& measure,
                                   size_t max_rows = kNoteMaxRows);
// The widest a note's line may be before it wraps.
float note_wrap_width(NotesPlace place, ImVec2 screen);
// The x of a line `line_w` wide inside the band's text block: centred, left or right as the place.
float note_line_x(NotesPlace place, const NoteBand& band, float line_w);

// Draws the band: solid in the middle, fading to nothing at its ends.
void draw_note_band(ImDrawList* dl, const NoteBand& band, uint32_t rgba = kBandColour);
// Draws `text` at `pos` with the look's passes in `rgba` (0xRRGGBBAA), wrapped at `wrap` pixels (0: never).
void draw_look_text(ImDrawList* dl, const TextLook& look, ImFont* font, float size, ImVec2 pos, uint32_t rgba,
                    const char* begin, const char* end = nullptr, float wrap = 0.0f);

// The console's navy and gold theme on top of ImGui's dark one: [overlay] accent replaces ImGui's blue (tabs, the
// selection, the scrollbar, checkboxes, sliders, buttons, text selection), [overlay] background is the windows' colour.
void apply_look_theme(ImGuiStyle& style, const LookSettings& look);

// 0xRRGGBBAA as ImGui's colours.
ImU32 look_colour(uint32_t rgba);
ImVec4 look_colour4(uint32_t rgba);

}  // namespace emf::core
