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

// The base pixel size: the swapchain's height / 54, kept within 13-40 px (EMF's size before the look settings).
float base_pixel_size(int swapchain_height);
// `percent` of the base size in whole pixels, at least 8.
float scaled_pixel_size(float base, int percent);

// The plan for a swapchain `swapchain_height` pixels high. `file_ok` says whether a font file can be used
// (font_file_ok in the game; tests pass their own). Pure.
FontPlan plan_fonts(const LookSettings& look, int swapchain_height, const FontPaths& paths,
                    const std::function<bool(const std::wstring&)>& file_ok);

// The glyphs every face gets (pairs, 0-terminated): Basic Latin and Latin-1, Latin Extended-A, Cyrillic.
extern const ImWchar kFontRanges[];

// Whether the atlas is due for a (re)build at the base size `pixel_size`: true the first time and once for each new
// size, never twice in a row for the same size.
class FontRebuild {
public:
    bool due(float pixel_size);

private:
    float built_ = 0.0f;
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
// Clears `atlas` (between frames only) and adds the plan's faces, one ImFont per distinct file and size, each with
// kFontRanges; the console's face is the atlas's first font (ImGui's default). A file that cannot be read or is not a
// font becomes ImGui's built-in font at its size, with a line in *problems. Builds the atlas.
OverlayFonts build_font_atlas(ImFontAtlas& atlas, const FontPlan& plan, std::vector<std::string>* problems);

}  // namespace emf::core
