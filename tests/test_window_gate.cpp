// Mod windows (API v24): the one window mods may have, from any thread to the render thread, input capture switched
// under the gate's lock, every accepted window handed back exactly once.
#include <doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "core/overlay/window_gate.h"

using namespace emf::core;

namespace {
struct Rig {
    std::vector<std::pair<bool, uint16_t>> captures;  // (on, close key) as the gate switched them
    WindowGate gate{[this](bool on, uint16_t vk) { captures.emplace_back(on, vk); }};
    WindowGate::Refusal open(const std::string& id, const std::string& title = "T", uint16_t vk = 0,
                             std::optional<WindowRequest>* replaced = nullptr, bool ready = true, bool hooked = true,
                             bool wheel = false) {
        WindowRequest r;
        r.id = id;
        r.title = title;
        r.close_vk = vk;
        r.mod = "beastmaster.dll";
        r.user = this;
        std::optional<WindowRequest> dummy;
        return gate.open(std::move(r), [ready] { return ready; }, hooked, wheel, replaced ? replaced : &dummy);
    }
};
}  // namespace

TEST_CASE("WindowGate: open captures with the close key; the same id replaces; another id is refused") {
    Rig g;
    CHECK_FALSE(g.gate.busy());
    CHECK(g.open("bm.stable", "Beasts", 'B') == WindowGate::Refusal::None);
    CHECK(g.gate.busy());
    REQUIRE(g.captures.size() == 1u);
    CHECK(g.captures[0] == std::make_pair(true, uint16_t('B')));
    const uint64_t v1 = g.gate.version();
    REQUIRE(g.gate.current());
    CHECK(g.gate.current()->title == "Beasts");

    std::optional<WindowRequest> replaced;
    CHECK(g.open("bm.stable", "Beasts 2", 'N', &replaced) == WindowGate::Refusal::None);
    REQUIRE(replaced);  // the old one is handed back (its cb gets CLOSED, BY_MOD)
    CHECK(replaced->title == "Beasts");
    CHECK(g.gate.current()->title == "Beasts 2");
    CHECK(g.gate.version() > v1);
    CHECK(g.captures.back() == std::make_pair(true, uint16_t('N')));  // the new close key

    replaced.reset();
    CHECK(g.open("other.window") == WindowGate::Refusal::OtherWindow);
    CHECK_FALSE(replaced);
    CHECK(g.gate.current()->id == "bm.stable");
}

TEST_CASE("WindowGate: refused when the overlay cannot take it, or a wheel is open") {
    Rig g;
    CHECK(g.open("a.b", "T", 0, nullptr, false) == WindowGate::Refusal::NotReady);
    CHECK(g.open("a.b", "T", 0, nullptr, true, false) == WindowGate::Refusal::NoWindow);
    CHECK(g.open("a.b", "T", 0, nullptr, true, true, true) == WindowGate::Refusal::WheelBusy);
    CHECK_FALSE(g.gate.busy());
    CHECK(g.captures.empty());
}

TEST_CASE("WindowGate: update changes the contents and keeps the callback; never reopens a closed window") {
    Rig g;
    WindowRequest contents;
    contents.id = "bm.stable";
    contents.title = "New";
    CHECK_FALSE(g.gate.update(contents));  // nothing open
    REQUIRE(g.open("bm.stable") == WindowGate::Refusal::None);
    const uint64_t v = g.gate.version();
    CHECK(g.gate.update(contents));
    CHECK(g.gate.current()->title == "New");
    CHECK(g.gate.current()->user == &g);  // kept from the open
    CHECK(g.gate.current()->mod == "beastmaster.dll");
    CHECK(g.gate.version() > v);
    contents.id = "bm.other";
    CHECK_FALSE(g.gate.update(contents));
    REQUIRE(g.gate.finish());  // the player closed it
    contents.id = "bm.stable";
    CHECK_FALSE(g.gate.update(contents));
    CHECK_FALSE(g.gate.busy());
}

TEST_CASE("WindowGate: close, finish, turned_off and close_mod hand the window back exactly once") {
    Rig g;
    std::optional<WindowRequest> closed;
    CHECK_FALSE(g.gate.close("bm.stable", &closed));
    REQUIRE(g.open("bm.stable") == WindowGate::Refusal::None);
    CHECK_FALSE(g.gate.close("bm.other", &closed));
    CHECK(g.gate.close("bm.stable", &closed));
    REQUIRE(closed);
    CHECK(closed->id == "bm.stable");
    CHECK(g.captures.back() == std::make_pair(false, uint16_t(0)));
    CHECK_FALSE(g.gate.busy());
    CHECK_FALSE(g.gate.finish());
    CHECK_FALSE(g.gate.turned_off());

    REQUIRE(g.open("bm.stable") == WindowGate::Refusal::None);
    CHECK(g.gate.finish());
    CHECK_FALSE(g.gate.finish());

    REQUIRE(g.open("bm.stable") == WindowGate::Refusal::None);
    CHECK(g.gate.turned_off());
    CHECK_FALSE(g.gate.turned_off());
    CHECK_FALSE(g.gate.current());

    REQUIRE(g.open("bm.stable") == WindowGate::Refusal::None);
    CHECK_FALSE(g.gate.close_mod("other.dll"));
    CHECK(g.gate.busy());
    CHECK(g.gate.close_mod("beastmaster.dll"));
    CHECK_FALSE(g.gate.busy());
    size_t ons = 0, offs = 0;
    for (const auto& c : g.captures) (c.first ? ons : offs)++;
    CHECK(ons == offs);  // capture never outlives a window
}

TEST_CASE("WindowGate: each window gets an open token; a replacement and every hand-back clear it; update keeps it") {
    Rig g;
    REQUIRE(g.open("bm.stable") == WindowGate::Refusal::None);
    const auto first = g.gate.current()->open;
    REQUIRE(static_cast<bool>(first));
    CHECK(first->load());
    std::optional<WindowRequest> replaced;
    REQUIRE(g.open("bm.stable", "T2", 0, &replaced) == WindowGate::Refusal::None);
    REQUIRE(replaced);
    CHECK_FALSE(first->load());  // a click for the old window is dropped from now on
    const auto second = g.gate.current()->open;
    REQUIRE(static_cast<bool>(second));
    const bool new_token = second.get() != first.get();
    CHECK(new_token);
    CHECK(second->load());
    WindowRequest contents;
    contents.id = "bm.stable";
    REQUIRE(g.gate.update(contents));
    const bool same_token = g.gate.current()->open.get() == second.get();
    CHECK(same_token);
    std::optional<WindowRequest> closed;
    REQUIRE(g.gate.close("bm.stable", &closed));
    CHECK_FALSE(second->load());
    REQUIRE(g.open("bm.stable") == WindowGate::Refusal::None);
    auto t = g.gate.current()->open;
    REQUIRE(g.gate.finish());
    CHECK_FALSE(t->load());
    REQUIRE(g.open("bm.stable") == WindowGate::Refusal::None);
    t = g.gate.current()->open;
    REQUIRE(g.gate.turned_off());
    CHECK_FALSE(t->load());
}
