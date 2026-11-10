#include "core/hud_editor/gate.h"

#include <windows.h>
#include <psapi.h>  // K32EnumProcessModules, K32GetModuleBaseNameW (kernel32 in PSAPI_VERSION 2)

#include <cstdio>

namespace emf::core::hud {

namespace {

constexpr KnownHudMod kKnownHudMods[] = {{L"ShroudHud.dll", "ShroudHud", "ShroudHudNativeFrameEnd"}};

bool equals_nocase(const std::wstring& a, const wchar_t* b) { return _wcsicmp(a.c_str(), b) == 0; }

bool sigs_usable(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index, Sig a, Sig b) {
    return hud_build_known(build_index) && is_usable(sigs[static_cast<size_t>(a)], known_build) &&
           is_usable(sigs[static_cast<size_t>(b)], known_build);
}

}  // namespace

Gate decide_gate(const GateInput& in) {
    Gate g;
    if (in.server_exe) {
        g.state = GateState::Server;
        g.message = "The dedicated server has no HUD.";
        return g;
    }
    if (!in.build_known) {
        g.state = GateState::UnknownBuild;
        g.message = "The HUD editor does not know this game build (" + in.build_label + "); your HUD is untouched.";
        return g;
    }
    g.show_editor = true;
    if (!in.hook_error.empty()) {
        g.state = GateState::HookFailed;
        g.message = "The HUD editor could not hook the game's UI code (" + in.hook_error + "); your HUD is untouched.";
        return g;
    }
    if (in.fault_code != 0) {
        char hex[16];
        std::snprintf(hex, sizeof hex, "%08X", static_cast<unsigned>(in.fault_code));
        g.state = GateState::Faulted;
        g.message = std::string("The HUD editor stopped after an error in the game's UI code (exception 0x") + hex +
                    "). It stays off until the game restarts; your HUD is the game's own until then.";
        return g;
    }
    if (!in.enabled) {
        g.state = GateState::Disabled;
        g.message = "The HUD editor is off (\"HUD editor\" below); your HUD is the game's own.";
        return g;
    }
    if (!in.other_hud_mod.empty() && !in.allow_with_other) {
        g.state = GateState::OtherHudMod;
        g.message = in.other_hud_mod +
                    " is installed; EMF's HUD editor is off so the two do not fight. Remove one of them, or switch on "
                    "\"Use anyway with another HUD mod\" below.";
        return g;
    }
    g.state = GateState::On;
    if (!in.other_hud_mod.empty())
        g.message = "Running next to " + in.other_hud_mod +
                    " (\"Use anyway\" is on): if a HUD part jumps or shows twice, remove one of them.";
    return g;
}

bool hud_build_known(int build_index) { return build_index >= 0 && build_index == known_build_index(1287369u, false); }

bool hud_signatures_ready(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index) {
    return sigs_usable(sigs, known_build, build_index, Sig::UiScopeOpen, Sig::UiScopeClose);
}

bool hud_scale_ready(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index) {
    return sigs_usable(sigs, known_build, build_index, Sig::UiPushTransform, Sig::UiPopTransform);
}

bool hud_bars_ready(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index) {
    return hud_scale_ready(sigs, known_build, build_index) &&
           sigs_usable(sigs, known_build, build_index, Sig::PlayerVitalBar, Sig::PlayerVitalBar);
}

std::string build_label(uint32_t build_number, const std::string& sha256) {
    if (build_number != 0) return std::to_string(build_number);
    if (sha256.empty()) return "unknown";
    return "sha256 " + sha256.substr(0, 12);
}

std::span<const KnownHudMod> known_hud_mods() { return kKnownHudMods; }

std::string other_hud_mod_in(const std::vector<std::wstring>& module_names,
                             const std::function<bool(const char* export_name)>& some_module_exports) {
    for (const KnownHudMod& m : kKnownHudMods)
        for (const std::wstring& n : module_names)
            if (equals_nocase(n, m.dll)) return m.name;
    for (const KnownHudMod& m : kKnownHudMods)
        if (some_module_exports && some_module_exports(m.export_name)) return m.name;
    return "";
}

std::string loaded_other_hud_mod() {
    // By the DLL's name (one lookup), then by an export of any loaded module: no module names are read, which is slow
    // with hundreds of modules and this runs on the game's thread.
    for (const KnownHudMod& m : kKnownHudMods)
        if (GetModuleHandleW(m.dll)) return m.name;
    HMODULE mods[1024];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &needed)) return "";
    const size_t n = (needed < sizeof mods ? needed : sizeof mods) / sizeof(HMODULE);
    for (const KnownHudMod& m : kKnownHudMods)
        for (size_t i = 0; i < n; ++i)
            if (GetProcAddress(mods[i], m.export_name)) return m.name;
    return "";
}

}  // namespace emf::core::hud
