// The HUD editor's gate: on or off, and the words that say why.
#include <doctest.h>

#include <array>
#include <string>

#include "common/signatures.h"
#include "core/hud_editor/gate.h"

using namespace emf;
using namespace emf::core::hud;

namespace {
std::array<Resolved, kSigCount> all_usable() {
    std::array<Resolved, kSigCount> r{};
    for (auto& x : r) x = Resolved{0x1000, 1, true};
    return r;
}
}  // namespace

TEST_CASE("decide_gate: each state with its message, in order of precedence") {
    GateInput in{false, true, "1287369"};
    CHECK(decide_gate(in).state == GateState::On);
    CHECK(decide_gate(in).message.empty());
    CHECK(decide_gate(in).show_editor);
    in.enabled = false;
    CHECK(decide_gate(in).state == GateState::Disabled);
    CHECK(decide_gate(in).message == "The HUD editor is off (\"HUD editor\" below); your HUD is the game's own.");
    CHECK(decide_gate(in).show_editor);
    in.enabled = true;
    in.other_hud_mod = "ShroudHud";
    CHECK(decide_gate(in).state == GateState::OtherHudMod);
    CHECK(decide_gate(in).message == "ShroudHud is installed; EMF's HUD editor is off so the two do not fight. Remove one of them, or switch on \"Use anyway with another HUD mod\" below.");
    in.allow_with_other = true;
    CHECK(decide_gate(in).state == GateState::On);
    CHECK(decide_gate(in).message == "Running next to ShroudHud (\"Use anyway\" is on): if a HUD part jumps or shows twice, remove one of them.");
    in.fault_code = 0xC0000005;
    CHECK(decide_gate(in).state == GateState::Faulted);
    CHECK(decide_gate(in).message == "The HUD editor stopped after an error in the game's UI code (exception 0xC0000005). It stays off until the game restarts; your HUD is the game's own until then.");
    in.hook_error = "MH_ERROR_NOT_EXECUTABLE";
    CHECK(decide_gate(in).state == GateState::HookFailed);
    CHECK(decide_gate(in).message == "The HUD editor could not hook the game's UI code (MH_ERROR_NOT_EXECUTABLE); your HUD is untouched.");
    in.build_known = false;
    CHECK(decide_gate(in).state == GateState::UnknownBuild);
    CHECK(decide_gate(in).message == "The HUD editor does not know this game build (1287369); your HUD is untouched.");
    CHECK_FALSE(decide_gate(in).show_editor);
    in.server_exe = true;
    CHECK(decide_gate(in).state == GateState::Server);
    CHECK(decide_gate(in).message == "The dedicated server has no HUD.");
    CHECK_FALSE(decide_gate(in).show_editor);
}

TEST_CASE("decide_gate: the fault code is 8 upper-case hex digits") {
    GateInput in{false, true, "1287369"};
    in.fault_code = 0xC0;
    CHECK(decide_gate(in).message.find("(exception 0x000000C0)") != std::string::npos);
}

TEST_CASE("hud_build_known: only the game exe of 1287369") {
    CHECK(hud_build_known(7));
    CHECK_FALSE(hud_build_known(8));
    CHECK_FALSE(hud_build_known(5));
    CHECK_FALSE(hud_build_known(-1));
}

TEST_CASE("hud_signatures_ready: the build and both UI scope signatures") {
    auto sigs = all_usable();
    CHECK(hud_signatures_ready(sigs, true, 7));
    CHECK_FALSE(hud_signatures_ready(sigs, true, 5));
    CHECK_FALSE(hud_signatures_ready(sigs, true, 8));
    CHECK_FALSE(hud_signatures_ready(sigs, true, -1));
    sigs[static_cast<size_t>(Sig::UiScopeOpen)].count = 2;
    CHECK_FALSE(hud_signatures_ready(sigs, true, 7));
    sigs = all_usable();
    sigs[static_cast<size_t>(Sig::UiScopeClose)].count = 0;
    CHECK_FALSE(hud_signatures_ready(sigs, true, 7));
}

TEST_CASE("hud_scale_ready: the build and both transform signatures, apart from the scope ones") {
    auto sigs = all_usable();
    CHECK(hud_scale_ready(sigs, true, 7));
    CHECK_FALSE(hud_scale_ready(sigs, true, 5));
    sigs[static_cast<size_t>(Sig::UiPopTransform)].count = 0;
    CHECK_FALSE(hud_scale_ready(sigs, true, 7));
    CHECK(hud_signatures_ready(sigs, true, 7));
    sigs = all_usable();
    sigs[static_cast<size_t>(Sig::UiPushTransform)].count = 2;
    CHECK_FALSE(hud_scale_ready(sigs, true, 7));
}

TEST_CASE("build_label: the number, or the start of the exe's sha256") {
    CHECK(build_label(1287369, "5d9ef2f140b8ed49aaaa") == "1287369");
    CHECK(build_label(0, "5d9ef2f140b8ed49aaaa") == "sha256 5d9ef2f140b8");
    CHECK(build_label(0, "5d9e") == "sha256 5d9e");
    CHECK(build_label(0, "") == "unknown");
}

TEST_CASE("other_hud_mod_in: ShroudHud by its DLL name (any case) or by its export") {
    auto none = [](const char*) { return false; };
    auto has = [](const char* e) { return std::string(e) == "ShroudHudNativeFrameEnd"; };
    CHECK(other_hud_mod_in({L"kernel32.dll", L"SHROUDHUD.DLL"}, none) == "ShroudHud");
    CHECK(other_hud_mod_in({L"MyHud.dll"}, has) == "ShroudHud");
    CHECK(other_hud_mod_in({L"kernel32.dll"}, none) == "");
    CHECK(other_hud_mod_in({}, none) == "");
}

TEST_CASE("known_hud_mods: ShroudHud and its export") {
    REQUIRE(known_hud_mods().size() == 1);
    CHECK(std::string(known_hud_mods()[0].name) == "ShroudHud");
    CHECK(std::string(known_hud_mods()[0].export_name) == "ShroudHudNativeFrameEnd");
}

TEST_CASE("loaded_other_hud_mod: none in the test process") { CHECK(loaded_other_hud_mod().empty()); }
