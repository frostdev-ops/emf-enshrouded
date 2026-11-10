// The HUD editor's console commands, over the editor rig.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/commands.h"
#include "core/hud_commands.h"
#include "hud_editor_rig.h"

using namespace emf::core;
using namespace emf::core::hud;
using hud_test::HudRig;

namespace {
using Lines = std::vector<std::string>;
Lines run(HudEditor& e, const Lines& argv) { return run_hud_command(e, argv); }
}  // namespace

TEST_CASE("hud.move: moves a part in percent, clamps with a note, saves") {
    HudRig r;
    HudEditor& e = r.make();
    CHECK(run(e, {"hud.move", "vitals", "5", "-2.5"}) == Lines{"hud: vitals at x 5 %, y -2.5 % (layout \"Default\")"});
    const int writes = r.writes;
    CHECK(run(e, {"hud.move", "compass", "150%", "0"}) ==
          Lines{"hud: compass at x 100 %, y 0 % (layout \"Default\") (kept within -100 to 100)"});
    CHECK(r.writes > writes);
    CHECK(e.view().elements[3].x == 100.0f);
    CHECK(e.view().elements[0].x == 5.0f);
}

TEST_CASE("hud.move: usage, unknown part, bad number") {
    HudRig r;
    HudEditor& e = r.make();
    CHECK(run(e, {"hud.move", "vitals"}) == Lines{"usage: hud.move <part> <x> <y> (percent of the screen, -100 to 100)"});
    CHECK(run(e, {"hud.move", "nope", "1", "1"}) ==
          Lines{"hud: no HUD part \"nope\"; the parts: vitals, buffs, hotbar, compass, shroud, quests, oxygen, loot, boss, "
                "crosshair, chat, notifications, multitool, health, mana, stamina"});
    CHECK(run(e, {"hud.move", "vitals", "abc", "1"}) == Lines{"hud.move: \"abc\" is not a number"});
    CHECK(run(e, {"hud.move", "vitals", "1", "nan"}) == Lines{"hud.move: \"nan\" is not a number"});
    CHECK(run(e, {"hud.move", "vitals", "1", "5x"}) == Lines{"hud.move: \"5x\" is not a number"});
    CHECK(e.view().elements[0].is_default());
}

TEST_CASE("hud.scale: scales a part, clamps with a note, says why when scale is not available") {
    HudRig r;
    HudEditor& e = r.make();
    CHECK(run(e, {"hud.scale", "hotbar", "0.75"}) == Lines{"hud: hotbar scale 0.75 (layout \"Default\")"});
    CHECK(e.snapshots().current()->edits[2].scale == 0.75f);
    CHECK(run(e, {"hud.scale", "compass", "3"}) == Lines{"hud: compass scale 2 (layout \"Default\") (kept within 0.5 to 2)"});
    r.scale = ScaleState{false, false};
    const Lines l = run(e, {"hud.scale", "compass", "1.5"});
    REQUIRE(l.size() == 1);
    CHECK(l[0] ==
          "hud: compass scale 1.5 (layout \"Default\") (kept; Scale is not available on this game build; move and hide "
          "work.)");
    CHECK(run(e, {"hud.scale", "compass"}) == Lines{"usage: hud.scale <part> <0.5-2>"});
}

TEST_CASE("hud.hide and hud.show") {
    HudRig r;
    HudEditor& e = r.make();
    CHECK(run(e, {"hud.hide", "buffs"}) == Lines{"hud: buffs hidden (hud.show buffs brings it back)"});
    CHECK(e.view().elements[1].hidden);
    CHECK(run(e, {"hud.show", "buffs"}) == Lines{"hud: buffs shown"});
    CHECK_FALSE(e.view().elements[1].hidden);
    CHECK(run(e, {"hud.hide"}) == Lines{"usage: hud.hide <part>"});
    CHECK(run(e, {"hud.show"}) == Lines{"usage: hud.show <part>"});
}

TEST_CASE("hud.reset: one part, or all with the \"Before reset\" note") {
    HudRig r;
    HudEditor& e = r.make();
    run(e, {"hud.move", "vitals", "5", "5"});
    run(e, {"hud.move", "compass", "1", "1"});
    CHECK(run(e, {"hud.reset", "compass"}) == Lines{"hud: compass is back where the game draws it"});
    CHECK(e.view().elements[3].is_default());
    CHECK(e.view().elements[0].x == 5.0f);
    CHECK(run(e, {"hud.reset"}) ==
          Lines{"hud: every part is back where the game draws it; the layout before is saved as \"Before reset\" "
                "(hud.layout load \"Before reset\" brings it back)"});
    CHECK(run(e, {"hud.reset", "all"}) == Lines{"hud: the HUD is already the game's own"});
    CHECK(run(e, {"hud.reset", "nope"})[0].rfind("hud: no HUD part", 0) == 0);
}

TEST_CASE("hud.layout: list, save with spaces in the name, load, delete") {
    HudRig r;
    HudEditor& e = r.make();
    CHECK(run(e, {"hud.layout"}) == Lines{"hud: layouts: Default (active)"});
    CHECK(run(e, {"hud.layout", "save", "Big", "Base"}) == Lines{"hud: saved as \"Big Base\""});
    CHECK(run(e, {"hud.layout"}) == Lines{"hud: layouts: Big Base (active), Default"});
    CHECK(run(e, {"hud.layout", "load", "default"}) == Lines{"hud: layout \"Default\" is in use"});
    CHECK(run(e, {"hud.layout", "delete", "big", "base"}) == Lines{"hud: layout \"Big Base\" deleted"});
    CHECK(run(e, {"hud.layout", "load", "gone"}) == Lines{"hud: no layout \"gone\"; layouts: Default"});
    CHECK(run(e, {"hud.layout", "save"}) == Lines{"usage: hud.layout [save|load|delete <name>]"});
    CHECK(run(e, {"hud.layout", "rename", "x"}) == Lines{"usage: hud.layout [save|load|delete <name>]"});
}

TEST_CASE("hud.list: the state, every part, the layouts") {
    HudRig r;
    HudEditor& e = r.make();
    run(e, {"hud.move", "vitals", "5", "-2.5"});
    run(e, {"hud.hide", "buffs"});
    r.live[0].seen = true;
    const Lines l = run(e, {"hud.list"});
    REQUIRE(l.size() == 19);
    CHECK(l[0] == "hud: on, layout \"Default\"");
    CHECK(l[1] == "hud: scale on (pushes 0, pops 0, mismatches 0)");
    CHECK(l[2] == "  vitals (playerStatusParent): x 5 %, y -2.5 %, scale 1");
    CHECK(l[3] == "  buffs (buffRow): x 0 %, y 0 %, scale 1, hidden, not seen yet");
    CHECK(l[4] == "  hotbar (doActionbar): where the game draws it, not seen yet");
    CHECK(l[15] == "  health (bar:health): where the game draws it, not seen yet");
    CHECK(l[18].rfind("hud: layouts: Default (active)", 0) == 0);
}

TEST_CASE("hud.list: off, with the gate's words") {
    HudRig r;
    r.gate.build_known = false;
    HudEditor& e = r.make();
    const Lines l = run(e, {"hud.list"});
    CHECK(l[0] == "hud: off: The HUD editor does not know this game build (1287369); your HUD is untouched.");
}

TEST_CASE("hud commands: an edit while off says the HUD does not show it now") {
    HudRig r;
    r.gate.build_known = false;
    HudEditor& e = r.make();
    const Lines l = run(e, {"hud.hide", "vitals"});
    REQUIRE(l.size() == 2);
    CHECK(l[1] ==
          "hud: not shown now: The HUD editor does not know this game build (1287369); your HUD is untouched.");
}

TEST_CASE("add_hud_commands: ten commands that run without a world") {
    HudRig r;
    HudEditor& e = r.make();
    CommandCenter c;
    REQUIRE(add_hud_commands(c, e));
    int found = 0;
    for (const auto& [name, help] : c.list())
        if (name.rfind("hud.", 0) == 0) ++found;
    CHECK(found == 10);
    std::vector<std::string> out;
    CommandOutcome outcome = CommandOutcome::Refused;
    c.run_without_world("hud.list", [&](const std::string& s) { out.push_back(s); }, "no world", &outcome);
    CHECK(outcome == CommandOutcome::Ran);
    CHECK_FALSE(out.empty());
}

TEST_CASE("hud.swap, hud.order, hud.grow") {
    HudRig r;
    HudEditor& e = r.make();
    CHECK(run(e, {"hud.swap", "health", "mana"}) == Lines{"hud: order mana, health, stamina (layout \"Default\")"});
    CHECK(e.view().order == BarOrder{1, 0, 2});
    CHECK(run(e, {"hud.swap", "Stamina", "HEALTH"}) == Lines{"hud: order mana, stamina, health (layout \"Default\")"});
    CHECK(run(e, {"hud.order", "stamina", "mana", "health"}) == Lines{"hud: order stamina, mana, health (layout \"Default\")"});
    const Lines order_usage{"usage: hud.order <bar> <bar> <bar> (health, mana and stamina once each, top first)"};
    CHECK(run(e, {"hud.order", "mana", "mana", "health"}) == order_usage);
    CHECK(run(e, {"hud.order", "mana", "health"}) == order_usage);
    const Lines swap_usage{"usage: hud.swap <bar> <bar> (two of health, mana, stamina)"};
    CHECK(run(e, {"hud.swap", "health", "health"}) == swap_usage);
    CHECK(run(e, {"hud.swap", "health", "fire"}) == swap_usage);
    CHECK(run(e, {"hud.swap", "health"}) == swap_usage);
    CHECK(e.view().order == BarOrder{2, 1, 0});
    CHECK(run(e, {"hud.grow", "up"}) == Lines{"hud: status effects grow upward (layout \"Default\")"});
    CHECK(e.view().buffs_up);
    CHECK(run(e, {"hud.grow", "down"}) == Lines{"hud: status effects grow downward, as the game draws them (layout \"Default\")"});
    CHECK_FALSE(e.view().buffs_up);
    CHECK(run(e, {"hud.grow", "sideways"}) == Lines{"usage: hud.grow up|down"});
    CHECK(run(e, {"hud.grow"}) == Lines{"usage: hud.grow up|down"});
}

TEST_CASE("hud.scale on a bar is refused with the reason") {
    HudRig r;
    HudEditor& e = r.make();
    CHECK(run(e, {"hud.scale", "mana", "1.5"}) == Lines{"hud: mana has no scale of its own; scale vitals instead"});
    CHECK(e.view().elements[static_cast<size_t>(bar_element(kBarMana))].scale == 1.0f);
}

TEST_CASE("hud.move on a bar; bar commands say so when the bars are not available on this build") {
    HudRig r;
    HudEditor& e = r.make();
    CHECK(run(e, {"hud.move", "mana", "0", "5"}) == Lines{"hud: mana at x 0 %, y 5 % (layout \"Default\")"});
    HudRig off;
    off.bars = false;
    HudEditor& f = off.make();
    const std::string note = "hud: not shown now: Moving health, mana and stamina on their own is not available on this game build.";
    CHECK(run(f, {"hud.move", "mana", "0", "5"}) == Lines{"hud: mana at x 0 %, y 5 % (layout \"Default\")", note});
    CHECK(run(f, {"hud.swap", "health", "mana"}) == Lines{"hud: order mana, health, stamina (layout \"Default\")", note});
    CHECK(run(f, {"hud.move", "vitals", "1", "1"}) == Lines{"hud: vitals at x 1 %, y 1 % (layout \"Default\")"});
}

TEST_CASE("hud.list: the order and grow upward when set, and the bars' note when they are not available") {
    HudRig r;
    HudEditor& e = r.make();
    run(e, {"hud.swap", "health", "mana"});
    run(e, {"hud.grow", "up"});
    const Lines l = run(e, {"hud.list"});
    REQUIRE(l.size() == 21);
    CHECK(l[2] == "hud: order mana, health, stamina");
    CHECK(l[3] == "hud: status effects grow upward");
    HudRig off;
    off.bars = false;
    HudEditor& f = off.make();
    const Lines m = run(f, {"hud.list"});
    REQUIRE(m.size() == 20);
    CHECK(m[2] == "hud: Moving health, mana and stamina on their own is not available on this game build.");
}

TEST_CASE("hud.grow says so when growing upward cannot show") {  // review: Important 1
    HudRig r;
    r.scale = ScaleState{true, true};
    HudEditor& e = r.make();
    CHECK(run(e, {"hud.grow", "up"}) ==
          Lines{"hud: status effects grow upward (layout \"Default\")",
                "hud: not shown now: Status effects growing upward stopped with scale for this session (see emf_core.log)."});
    CHECK(run(e, {"hud.move", "mana", "0", "5"}) ==
          Lines{"hud: mana at x 0 %, y 5 % (layout \"Default\")",
                "hud: not shown now: Moving health, mana and stamina on their own stopped with scale for this session (see "
                "emf_core.log)."});
}
