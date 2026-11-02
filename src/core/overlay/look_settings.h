#pragma once
// How EMF's on-screen text and the console look: [hud] (HUD lines, notes, wheels) and the look keys of [overlay] in
// emf\emf_core.ini, read once at start. No ImGui here (unit-tested).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace emf::core {

// [hud] style: game = the game's look (serif, soft shadow, no box; notes on a band that fades at both ends);
// boxed = EMF's earlier look (dark boxes at the console's text size; unless set, [hud] font=console, colour=#dbdbcc,
// notes_place=top_left and [overlay] tabs_font=consolas); plain = the text alone (no shadow, no band, no box).
enum class HudStyle : uint8_t { Game, Boxed, Plain };
// [hud] notes_place: where notes (and the replies to a chat command) show.
enum class NotesPlace : uint8_t { TopCentre, TopLeft, Right };

// A font setting: serif (the bundled Alegreya), console (the console's font, [hud] only), consolas (Windows' own),
// cascadia (the bundled Cascadia Mono), or file:<name> (a .ttf or .otf in emf\fonts).
struct FontChoice {
    enum class Kind : uint8_t { Serif, Console, Consolas, Cascadia, File };
    Kind kind = Kind::Serif;
    std::string file;  // Kind::File: the file name in emf\fonts (UTF-8)
    bool operator==(const FontChoice&) const = default;
};

struct LookSettings {
    // [hud]
    HudStyle style = HudStyle::Game;
    FontChoice hud_font{FontChoice::Kind::Serif};
    int hud_size = 100;                // % of the base size (60-200)
    uint32_t hud_colour = 0xFFFFF4FFu;  // 0xRRGGBBAA: lines whose mod chose no colour, and notes
    bool shadow = true;                // style=game only
    NotesPlace notes_place = NotesPlace::TopCentre;
    int notes_seconds = 6;  // 2-20
    // [overlay]
    FontChoice console_font{FontChoice::Kind::Consolas};
    FontChoice tabs_font{FontChoice::Kind::Serif};  // the tab bar and the Mods tab
    int text_size = 100;                            // % of the base size (60-200)
    int height = 50;                                // the console's height, % of the screen (25-90)
    int opacity = 80;                               // the console's background, % (30-100)
    uint32_t background = 0x080A0EFFu;
    uint32_t accent = 0xC0B074FFu;  // tabs, the selection, the scrollbar, the wheel's ring
    // the console's lines: the commands typed, replies, errors and successes
    uint32_t colour_command = 0xFFFFFFFFu;
    uint32_t colour_reply = 0xB3BAC2FFu;
    uint32_t colour_error = 0xFF7A73FFu;
    uint32_t colour_success = 0x7DE887FFu;
    bool operator==(const LookSettings&) const = default;
};

// Reads the look through `get(section, key)` (the raw value, "" when the key is not set). A "; comment" after a value
// and the spaces around it are dropped. A value that cannot be read is replaced by the default; a number out of range
// is kept inside it. Each such value adds one line to *problems (for the log).
LookSettings read_look_settings(const std::function<std::string(const char* section, const char* key)>& get,
                                std::vector<std::string>* problems);

// Whether going from `a` to `b` needs the font atlas built again: a font, the style (boxed draws HUD lines in the
// console's font and size) or a text size changed. Colours, opacity, height and places do not.
bool fonts_differ(const LookSettings& a, const LookSettings& b);

}  // namespace emf::core
