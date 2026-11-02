#pragma once
// The console's Settings tab (in-game look phase 2): EMF's own settings (emf_core.ini, emf_settings.h) on pages left,
// the page's widgets right (the Mods tab's draw_setting), applied at once, "(next start)" where a restart is needed.
// The text page shows a preview of the on-screen text. Drawn headless in the tests.
#include <functional>
#include <string>

#include "core/emf_settings.h"
#include "core/overlay/mods_ui.h"

namespace emf::core {

struct SettingsUiState {
    std::string page = kPageText;  // the page shown (a page that does not exist falls back to the first)
};

// Draws the tab into the current window. `preview` (optional) draws the text page's preview of the on-screen look.
void draw_settings_tab(SettingsStore* s, ModsUiState& ui, SettingsUiState& st,
                       const std::function<void()>& preview = std::function<void()>());

}  // namespace emf::core
