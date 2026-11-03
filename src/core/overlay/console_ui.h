#pragma once
// Draws the drop-down console with Dear ImGui (mockup B): top half of the screen, see-through dark, coloured lines,
// an input line with suggestions as you type (console_suggest.h; or Tab completion) and Up/Down history.
// Renderer-agnostic (no Vulkan here).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "console_model.h"
#include "console_suggest.h"
#include "hud_model.h"
#include "imgui.h"
#include "look_layout.h"

namespace emf::core {

struct ConsoleUiState {
    std::string input;
    float open_fraction = 0.0f;  // 0 = closed, 1 = fully down (the slide animation sets it)
    bool focus_input = true;     // focus the input line on the next frame
    bool follow = true;          // stick to the newest line
    uint64_t seen_version = 0;
    float seen_height = 0.0f;    // the scrollback's height last frame
    std::vector<std::string> completions;  // shown under the input while Tab cycles
    size_t completion_index = 0;
    int tab = 0;               // 0 = Console, 1 = Mods, 2 = Settings
    bool switch_tab = false;   // Ctrl+Tab asked for the other tab (applied on the next frame)
    std::string status;        // shown at the right end of the tab bar ("debug bridge on"); "" = none
    // Suggestions as you type ([overlay] suggest; off: Tab cycles through the matches as before).
    bool suggest = true;
    Suggester suggester;        // the list ([overlay] suggest_rows: suggester.set_rows)
    size_t caret = 0;           // the input's caret (bytes) after the last frame it was active
    bool escape = false;        // Esc reached the console (the router sends it only while the list shows): hide it
    bool wants_escape = false;  // after a frame: the list shows, so the next Esc is its (InputRouter::set_esc_to_console)
    bool accept_right = false;  // this frame: Right was pressed with the caret at the end of the line
    std::string ghost_shown;    // after a frame: the grey ghost text drawn after the input ("" = none)
    // The look ([overlay] height, opacity, background, tabs_font).
    float height = 0.50f;   // the console's height, a fraction of the screen
    float opacity = 0.80f;  // its background's
    ImVec4 background{0x08 / 255.0f, 0x0A / 255.0f, 0x0E / 255.0f, 1.0f};
    ImFont* tabs_font = nullptr;  // the tab bar and the Mods tab (null: the console's font)
    // The lines' colours ([overlay] colour_command, _reply, _error, _success).
    ImVec4 colour_command{1.0f, 1.0f, 1.0f, 1.0f};
    ImVec4 colour_reply{0xB3 / 255.0f, 0xBA / 255.0f, 0xC2 / 255.0f, 1.0f};
    ImVec4 colour_error{0xFF / 255.0f, 0x7A / 255.0f, 0x73 / 255.0f, 1.0f};
    ImVec4 colour_success{0x7D / 255.0f, 0xE8 / 255.0f, 0x87 / 255.0f, 1.0f};
};

// The colour a line of this kind is drawn in.
ImVec4 line_colour(const ConsoleUiState& ui, LineKind kind);

// The tab Ctrl+Tab goes to from `tab` (0 Console, 1 Mods, 2 Settings): the next one there is, wrapping to Console.
int next_tab(int tab, bool has_mods, bool has_settings);

// A sample of the on-screen text in the current window, `width` wide: a note (on its band in the game style) and a HUD
// line, drawn as the look draws them (the Settings tab's preview).
void draw_look_preview(const TextLook& look, float width);

// `mods_tab` and `settings_tab` (optional) draw those tabs' content; with either the console shows a tab bar
// (Console | Mods | Settings; Ctrl+Tab goes to the next).
void draw_console(ConsoleModel& model, ConsoleUiState& ui, const CompletionSources& sources, ImVec2 display,
                  const std::function<void(const std::string&)>& submit,
                  const std::function<void()>& mods_tab = std::function<void()>(),
                  const std::function<void()>& settings_tab = std::function<void()>());

// Notes (notify) and the replies to a command typed into the game's chat (the console stays closed), for a few seconds
// at [hud] notes_place. They are the lines added since `from_version`, shown until `until_ms`.
struct ToastState {
    uint64_t from_version = 0;
    uint64_t until_ms = 0;
};
using ConsoleLines = std::vector<std::pair<LineKind, std::string>>;
// The lines the toast shows at now_ms: those added since it started, at most `max` (when more came, a note saying how
// many are left out, then the newest); none once it is over.
ConsoleLines toast_lines(const ConsoleModel& model, const ToastState& toast, uint64_t now_ms, size_t max = 8);
// Game style: bold text with a soft shadow on a band that fades at its ends; plain: the text alone; boxed: in a dark
// box (EMF's earlier look). At look.notes_place.
void draw_notes(const ConsoleLines& lines, ImVec2 display, const TextLook& look);
// The mods' on-screen lines (API v12): game style, text with a soft shadow; plain, the text alone; boxed, one small
// window per place (EMF's earlier look).
void draw_hud(const std::vector<HudLine>& lines, ImVec2 display, const TextLook& look);

}  // namespace emf::core
