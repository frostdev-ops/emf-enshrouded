#pragma once
// The overlay's fonts. plan_fonts picks the file of each face along the fallback chain (the requested font, then the
// bundled serif or Cascadia Mono, then Windows' Consolas, then ImGui's built-in font) and its pixel size from the
// window's height; build_font_atlas turns a plan into ImGui fonts. The atlas is built again when the window's size
// changes the base size (FontRebuild), between frames.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"
#include "look_settings.h"

namespace emf::core {

// What each face is for: the console's text, the tab bar and the Mods tab, the HUD lines and wheels, and their titles
// (and notes) in bold.
enum class FontRole : uint8_t { Console, Tabs, Hud, HudBold };

struct FontFace {
    FontRole role = FontRole::Console;
    std::wstring file;          // the font file; "" = ImGui's built-in font
    float pixel_size = 13.0f;
    bool operator==(const FontFace&) const = default;
};

struct FontPlan {
    std::vector<FontFace> faces;        // one per FontRole, in its order
    float pixel_size = 13.0f;           // the base size (base_pixel_size)
    std::vector<std::string> problems;  // each file that could not be used, once (for the log)
    const FontFace& face(FontRole r) const { return faces.at(static_cast<size_t>(r)); }
};

// Where the font files are.
struct FontPaths {
    std::wstring fonts_dir;  // <game>\emf\fonts: the bundled fonts and the player's own
    std::wstring consolas;   // Windows' Consolas (%WINDIR%\Fonts\consola.ttf)
};
// The bundled fonts' file names in emf\fonts (third_party/fonts).
inline constexpr wchar_t kSerifFile[] = L"Alegreya-Regular.ttf";
inline constexpr wchar_t kSerifBoldFile[] = L"Alegreya-Bold.ttf";
inline constexpr wchar_t kCascadiaFile[] = L"CascadiaMono-Regular.ttf";

// The console's base pixel size: the swapchain's height / 54, kept within 13-40 px (EMF's size before the look
// settings).
float base_pixel_size(int swapchain_height);
// On-screen text's base (HUD lines, notes, wheels): the swapchain's height / 38, kept within 16-58 px. Bigger than the
// console's: a book serif's small x-height reads smaller than a monospace font at the same pixel size, and the game's
// own small status text is about this big.
float hud_base_pixel_size(int swapchain_height);
// Serif tabs (the tab bar and the Mods tab) are this much bigger than the console's text.
inline constexpr float kSerifTabsScale = 1.3f;
// `percent` of a base size in whole pixels, at least 8.
float scaled_pixel_size(float base, int percent);

// The pixel sizes of the faces: the console ([overlay] text_size), the tab bar and the Mods tab (the console's size;
// 1.3 x that in a serif or a player's font, whose small x-height would look tiny next to the console's lines), and
// on-screen text ([hud] size; at the console's base with style=boxed, EMF's earlier look). The atlas is built again
// only when these change.
struct FontSizes {
    float text = 0.0f;
    float hud = 0.0f;
    float tabs = 0.0f;
    bool operator==(const FontSizes&) const = default;
};
FontSizes font_sizes(const LookSettings& look, int swapchain_height);

// The plan for a swapchain `swapchain_height` pixels high. `file_ok` says whether a font file can be used
// (font_file_ok in the game; tests pass their own). Pure.
FontPlan plan_fonts(const LookSettings& look, int swapchain_height, const FontPaths& paths,
                    const std::function<bool(const std::wstring&)>& file_ok);

// The glyphs every face gets (pairs, 0-terminated): Basic Latin and Latin-1, Latin Extended-A, Cyrillic.
extern const ImWchar kFontRanges[];

// When the atlas is (re)built: at once the first time; for new sizes once they held for kSettleMs (dragging a window's
// edge changes the size every frame); never while the sizes are those built.
class FontRebuild {
public:
    static constexpr uint64_t kSettleMs = 250;
    bool due(const FontSizes& sizes, uint64_t now_ms);
    void built(const FontSizes& sizes);  // the atlas now has these sizes
    void request();                      // a font (not a size) changed: due at once, until built

private:
    bool requested_ = false;
    bool any_ = false;
    FontSizes built_, pending_;
    uint64_t since_ = 0;
};

// A font file EMF can hand to ImGui: at most `max_bytes`, a TrueType or OpenType font (or collection) whose tables lie
// inside the file and that stb_truetype reads without a fault. Garbage, a cut-off file or a text file is refused; it
// never throws or crashes.
inline constexpr size_t kMaxFontBytes = 32u << 20;
bool font_file_ok(const std::wstring& path, size_t max_bytes = kMaxFontBytes);
bool font_bytes_ok(const unsigned char* data, size_t size);
// The whole file into `out` (false when it cannot be read or is larger than max_bytes).
bool read_font_file(const std::wstring& path, size_t max_bytes, std::vector<unsigned char>& out);

// The ImGui fonts for each role (never null after build_font_atlas).
struct OverlayFonts {
    ImFont* console = nullptr;
    ImFont* tabs = nullptr;
    ImFont* hud = nullptr;
    ImFont* hud_bold = nullptr;
};
// The tallest atlas the item names' characters may make (4096 wide: 128 MB of RGBA at the most): Vulkan's
// maxImageDimension2D is 16384 on many GPUs, and the atlas grows with the text size and the screen's height.
inline constexpr int kMaxAtlasHeight = 8192;

// The characters the item names need beyond kFontRanges (ScreenGlyphs), and the extra font that has the ones EMF's
// fonts lack (Japanese, Korean, Chinese, Thai; null: none). The atlas points into the font's bytes: they stay the
// caller's and alive until the next build.
struct ExtraGlyphs {
    std::vector<uint32_t> glyphs;  // sorted
    const unsigned char* font = nullptr;
    size_t font_size = 0;
    int max_atlas_height = kMaxAtlasHeight;  // taller with them: built without them (their names show in English)
};

// The characters of `glyphs` the font in `data` has (its first face, as ImGui reads it); empty for a file stb_truetype
// cannot read. Never throws or crashes.
std::vector<uint32_t> font_has_glyphs(const unsigned char* data, size_t size, const std::vector<uint32_t>& glyphs);

// atlas.Build() under a fault guard: an ImGui assert (EMF's raises an exception) or a fault while building gives false,
// never a crash; the atlas must then be cleared.
bool guarded_atlas_build(ImFontAtlas& atlas, std::string* why = nullptr);
// Clears `atlas` (between frames only) and adds the plan's faces, one ImFont per distinct file and size, each with
// kFontRanges and `extra`'s characters (those its file has); the console's face is the atlas's first font (ImGui's
// default). With an extra font, each face then merges the characters it has. A file that cannot be read or is not a
// font becomes ImGui's built-in font at its size, with a line in *problems; so does an extra font that cannot be used
// (left out). An atlas that cannot be built with the extra font is built again without it. Render thread only (the
// glyph ranges live in statics until the next build).
OverlayFonts build_font_atlas(ImFontAtlas& atlas, const FontPlan& plan, std::vector<std::string>* problems,
                              const ExtraGlyphs* extra = nullptr);

// The characters of `glyphs` every distinct face of `fonts` has (none above U+FFFF).
std::vector<uint32_t> drawable_glyphs(const OverlayFonts& fonts, const std::vector<uint32_t>& glyphs);

}  // namespace emf::core
