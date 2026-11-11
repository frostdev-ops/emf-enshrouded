#pragma once
// The HUD editor's gate: whether it may touch the game's HUD, and the words that say why not. Pure logic apart from
// loaded_other_hud_mod(), which looks at the live process.
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "common/signatures.h"

namespace emf::core::hud {

enum class GateState { On, Server, UnknownBuild, HookFailed, Faulted, Disabled, OtherHudMod };

struct GateInput {
    bool server_exe = false;
    bool build_known = false;
    std::string build_label;
    bool enabled = true;  // the "HUD editor" setting
    std::string other_hud_mod;  // the name of a HUD mod that is loaded, or empty
    bool allow_with_other = false;  // "Use anyway with another HUD mod"
    uint32_t fault_code = 0;  // the exception code of a fault in a detour, 0 for none
    std::string hook_error;  // why the hooks could not be installed, or empty
};

struct Gate {
    GateState state = GateState::UnknownBuild;
    std::string message;  // what the page shows; empty when all is well
    bool show_editor = false;  // false only for Server and UnknownBuild
};

// Precedence: Server, UnknownBuild, HookFailed, Faulted, Disabled, OtherHudMod (unless allowed), On.
Gate decide_gate(const GateInput& in);

// True only for the game exe of build 1287369 (the node offsets were only checked there).
bool hud_build_known(int build_index);
// The build is known and both UI scope signatures are usable.
bool hud_signatures_ready(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index);
// The build is known and both layer transform signatures are usable.
bool hud_scale_ready(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index);
// The bars (health, mana, stamina) can move on their own: the bar signature, and the transform stack they move through.
bool hud_bars_ready(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index);
// The panel colour: the bars (health's frame colour is read there) and the panel's uiLayerOpen call (PanelLayerOpen).
bool hud_panel_ready(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index);
// "1287369"; without a build number "sha256 <12 hex digits>"; without either "unknown".
std::string build_label(uint32_t build_number, const std::string& sha256);

struct KnownHudMod {
    const wchar_t* dll;
    const char* name;
    const char* export_name;
};
std::span<const KnownHudMod> known_hud_mods();
// The name of a known HUD mod among the module names, or one of the modules exporting a known mod's export; else "".
std::string other_hud_mod_in(const std::vector<std::wstring>& module_names,
                             const std::function<bool(const char* export_name)>& some_module_exports);
// The same for this process's loaded modules.
std::string loaded_other_hud_mod();

}  // namespace emf::core::hud
