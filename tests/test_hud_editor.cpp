// The HUD editor: settings, layouts from emf\hud, the gate, hooks once, edits published to the hooks, saves.
#include <doctest.h>

#include <windows.h>

#include <fstream>

#include "hud_editor_rig.h"

using namespace emf::core;
using namespace emf::core::hud;
using hud_test::HudRig;

namespace {
void write_layout(HudRig& r, const std::string& name, size_t part, float x) {
    HudLayout l;
    l.name = name;
    l.elements[part].x = x;
    std::string err;
    REQUIRE(write_layout_file(r.dir, l, 1287369, &err));
}
}  // namespace

TEST_CASE("editor: a first start is on with an empty Default, hooked once, logged once") {
    HudRig r;
    HudEditor& e = r.make();
    const HudView v = e.view();
    CHECK(v.gate.state == GateState::On);
    CHECK(v.layout_name == "Default");
    CHECK(v.layouts == std::vector<std::string>{"Default"});
    CHECK(r.installs == 1);
    CHECK(e.snapshots().current()->active);
    CHECK(r.count_logged("HUD editor: on (layout \"Default\", 13 parts)") == 1);
    CHECK(r.files().empty());
    CHECK(r.writes == 0);
}

TEST_CASE("editor: the active layout comes from the setting and emf\\hud") {
    HudRig r;
    write_layout(r, "Default", 0, 1);
    write_layout(r, "Building", 3, 12.5f);
    r.set("active_layout", "Building");
    HudEditor& e = r.make();
    const HudView v = e.view();
    CHECK(v.layout_name == "Building");
    CHECK(v.elements[3].x == 12.5f);
    CHECK(v.elements[0].x == 0.0f);
    CHECK(v.layouts == std::vector<std::string>{"Building", "Default"});
    CHECK(e.snapshots().current()->edits[3].x_percent == 12.5f);
}

TEST_CASE("editor: broken files and notes are logged, the rest still load") {
    HudRig r;
    std::filesystem::create_directories(r.dir);
    std::ofstream(std::filesystem::path(r.dir) / L"bad.json") << "{";
    write_layout(r, "Default", 0, 2);
    HudEditor& e = r.make();
    CHECK(r.logged("HUD editor: emf\\hud\\bad.json: not JSON"));
    CHECK(e.view().elements[0].x == 2.0f);
}

TEST_CASE("editor: an unknown build is off, never hooks, and still keeps edits") {
    HudRig r;
    r.gate.build_known = false;
    HudEditor& e = r.make();
    CHECK(e.view().gate.state == GateState::UnknownBuild);
    CHECK(r.installs == 0);
    CHECK_FALSE(e.snapshots().current()->active);
    e.set_offset(0, 5, 5, Save::Now);
    const auto files = r.files();
    REQUIRE(files.size() == 1);
    CHECK(files[0].layout->elements[0].x == 5.0f);
    CHECK_FALSE(e.snapshots().current()->active);
}

TEST_CASE("editor: switched off at start hooks nothing until switched on") {
    HudRig r;
    r.set("enabled", "0");
    HudEditor& e = r.make();
    CHECK(e.view().gate.state == GateState::Disabled);
    CHECK(r.installs == 0);
    r.set("enabled", "1");
    e.tick();
    CHECK(e.view().gate.state == GateState::On);
    CHECK(r.installs == 1);
    CHECK(e.snapshots().current()->active);
    r.set("enabled", "0");
    e.tick();
    CHECK_FALSE(e.snapshots().current()->active);
    CHECK(r.installs == 1);
}

TEST_CASE("editor: ShroudHud keeps it off; 'use anyway' turns it on; a HUD mod loaded later turns it off") {
    {
        HudRig r;
        r.other_mod = "ShroudHud";
        HudEditor& e = r.make();
        CHECK(e.view().gate.state == GateState::OtherHudMod);
        CHECK(r.installs == 0);
        r.set("allow_with_shroudhud", "1");
        e.tick();
        CHECK(e.view().gate.state == GateState::On);
        CHECK(r.installs == 1);
    }
    {
        HudRig r;
        HudEditor& e = r.make();
        CHECK(e.view().gate.state == GateState::On);
        r.other_mod = "ShroudHud";
        r.now += 4999;
        e.tick();
        CHECK(e.view().gate.state == GateState::On);
        r.now += 1;
        e.tick();
        CHECK(e.view().gate.state == GateState::OtherHudMod);
        CHECK_FALSE(e.snapshots().current()->active);
    }
}

TEST_CASE("editor: a fault turns it off and is logged once") {
    HudRig r;
    HudEditor& e = r.make();
    r.fault = 0xC0000005;
    e.tick();
    e.tick();
    CHECK(e.view().gate.state == GateState::Faulted);
    CHECK_FALSE(e.snapshots().current()->active);
    CHECK(r.count_logged("exception 0xC0000005") == 1);
}

TEST_CASE("editor: a hook that fails is not tried again") {
    HudRig r;
    r.install_ok = false;
    r.install_why = "x";
    HudEditor& e = r.make();
    CHECK(e.view().gate.state == GateState::HookFailed);
    CHECK(e.view().gate.message.find("(x)") != std::string::npos);
    e.tick();
    e.tick();
    e.tick();
    CHECK(r.installs == 1);
    CHECK_FALSE(e.snapshots().current()->active);
}

TEST_CASE("editor: edits clamp, publish at once and save now") {
    HudRig r;
    HudEditor& e = r.make();
    e.set_offset(0, 150, -2.5f, Save::Now);
    CHECK(e.view().elements[0].x == 100.0f);
    CHECK(e.view().elements[0].y == -2.5f);
    CHECK(e.snapshots().current()->edits[0].x_percent == 100.0f);
    CHECK(e.snapshots().current()->edits[0].y_percent == -2.5f);
    CHECK(r.writes == 1);
    REQUIRE(r.files().size() == 1);
    CHECK(r.files()[0].layout->elements[0].x == 100.0f);
    e.nudge(0, -0.5f, 0, Save::Now);
    CHECK(e.view().elements[0].x == 99.5f);
    e.set_hidden(3, true, Save::Now);
    CHECK(e.snapshots().current()->edits[3].hidden);
    e.set_scale(1, 1.234f, Save::Now);
    CHECK(e.view().elements[1].scale == doctest::Approx(1.23f));
    CHECK(e.snapshots().current()->edits[1].scale == doctest::Approx(1.23f));
    e.set_scale(1, 9, Save::Now);
    CHECK(e.view().elements[1].scale == 2.0f);
    e.reset(0, Save::Now);
    CHECK(e.view().elements[0].is_default());
    e.set_offset(99, 1, 1, Save::Now);  // no such part: nothing happens
}

TEST_CASE("editor: Later edits are written once, 500 ms after the last one") {
    HudRig r;
    HudEditor& e = r.make();
    const uint64_t t = r.now;
    e.set_offset(0, 1, 1, Save::Later);
    r.now = t + 100;
    e.set_offset(0, 2, 1, Save::Later);
    r.now = t + 200;
    e.set_offset(0, 3, 1, Save::Later);
    CHECK(r.writes == 0);
    r.now = t + 699;
    e.tick();
    CHECK(r.writes == 0);
    r.now = t + 700;
    e.tick();
    CHECK(r.writes == 1);
    e.tick();
    CHECK(r.writes == 1);
    CHECK(r.files()[0].layout->elements[0].x == 3.0f);
}

TEST_CASE("editor: flush writes a pending Later save at once") {
    HudRig r;
    HudEditor& e = r.make();
    e.set_offset(0, 1, 1, Save::Later);
    e.flush();
    CHECK(r.writes == 1);
    e.flush();
    CHECK(r.writes == 1);
}

TEST_CASE("editor: settings changed elsewhere are followed at the next tick") {  // Review Focus 5
    HudRig r;
    write_layout(r, "Building", 3, 12.5f);
    HudEditor& e = r.make();
    r.set("active_layout", "Nope");
    e.tick();
    CHECK(e.view().layout_name == "Default");
    CHECK(r.count_logged("\"Nope\"") == 1);
    e.tick();
    CHECK(r.count_logged("\"Nope\"") == 1);
    r.set("active_layout", "Building");
    e.tick();
    CHECK(e.view().layout_name == "Building");
    CHECK(e.view().elements[3].x == 12.5f);
}

TEST_CASE("editor: a missing layout at start falls back to Default with one log line") {
    HudRig r;
    r.set("active_layout", "Gone");
    HudEditor& e = r.make();
    CHECK(e.view().layout_name == "Default");
    CHECK(r.logged("HUD editor: layout \"Gone\" is not in emf\\hud; using \"Default\""));
    CHECK(r.store.get(kEmfSettingsId, "hud_editor.active_layout") == "Gone");  // never written back
}

TEST_CASE("editor: view shows what the hooks saw") {
    HudRig r;
    HudEditor& e = r.make();
    r.live[4] = LiveInfo{true, 99, {786, 56, 512, 30}};
    const HudView v = e.view();
    CHECK(v.live[4].seen);
    CHECK(v.live[4].last_seen_ms == 99);
    CHECK(v.live[4].rect[0] == 786.0f);
    CHECK(v.live[4].rect[3] == 30.0f);
    CHECK_FALSE(v.live[0].seen);
}

TEST_CASE("editor: scale works, is not available, or stops after a mismatch (logged once); its values are kept") {
    HudRig r;
    HudEditor& e = r.make();
    CHECK(e.view().scale_works);
    CHECK(e.view().scale_note.empty());
    r.scale = ScaleState{false, false};
    CHECK_FALSE(e.view().scale_works);
    CHECK(e.view().scale_note == "Scale is not available on this game build; move and hide work.");
    e.set_scale(2, 1.5f, Save::Now);
    CHECK(e.view().elements[2].scale == 1.5f);
    r.scale = ScaleState{true, true};
    e.tick();
    e.tick();
    CHECK_FALSE(e.view().scale_works);
    CHECK(e.view().scale_note ==
          "Scale stopped for this session: the game's drawing did not match what EMF expects (see emf_core.log). Move and "
          "hide still work.");
    CHECK(r.count_logged("HUD editor: scale is off until the game restarts") == 1);
    CHECK(e.view().elements[2].scale == 1.5f);
}
