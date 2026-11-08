#pragma once
// The HUD editor's console commands: hud.list, hud.move, hud.scale, hud.hide, hud.show, hud.reset and hud.layout. (The
// built-in "hud" command, the mods' HUD lines, is another command and stays.) They work on the main menu too.
#include <string>
#include <vector>

#include "core/hud_editor/editor.h"

namespace emf::core {

class CommandCenter;

// What the command answers, line by line; argv[0] is the command's name.
std::vector<std::string> run_hud_command(hud::HudEditor& editor, const std::vector<std::string>& argv);
// Registers the seven commands; `editor` must stay alive as long as they can run.
bool add_hud_commands(CommandCenter& center, hud::HudEditor& editor);

}  // namespace emf::core
