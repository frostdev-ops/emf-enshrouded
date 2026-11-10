// The HUD page's keyboard model and the two keys (open the page, reset the HUD).
#include <doctest.h>

#include <windows.h>

#include <set>

#include "core/hud_editor/keys.h"

using namespace emf::core::hud;

TEST_CASE("page_command: Tab steps, arrows move 0.5 % (Shift 5 %), + and - scale, H hides, R resets") {
    CHECK(page_command(PageKey::Tab, false).action == PageAction::Next);
    CHECK(page_command(PageKey::Tab, true).action == PageAction::Previous);
    const auto up = page_command(PageKey::Up, false);
    CHECK(up.action == PageAction::Move);
    CHECK(up.dx == 0);
    CHECK(up.dy == -0.5f);
    CHECK(page_command(PageKey::Down, false).dy == 0.5f);
    CHECK(page_command(PageKey::Left, false).dx == -0.5f);
    CHECK(page_command(PageKey::Right, true).dx == 5.0f);
    CHECK(page_command(PageKey::Up, true).dy == -5.0f);
    CHECK(page_command(PageKey::Plus, false).action == PageAction::ScaleUp);
    CHECK(page_command(PageKey::Minus, false).action == PageAction::ScaleDown);
    CHECK(page_command(PageKey::H, false).action == PageAction::ToggleHide);
    CHECK(page_command(PageKey::R, true).action == PageAction::Reset);
    CHECK(page_command(PageKey::Other, false).action == PageAction::None);
}

TEST_CASE("step_element: wraps both ways") {
    CHECK(step_element(18, false) == 0);
    CHECK(step_element(0, true) == 18);
    CHECK(step_element(3, false) == 4);
    CHECK(step_element(3, true) == 2);
}

TEST_CASE("key_setting_vk: the key setting's text") {
    CHECK(key_setting_vk("F9") == static_cast<uint32_t>(VK_F9));
    CHECK(key_setting_vk("Pause") == static_cast<uint32_t>(VK_PAUSE));
    CHECK(key_setting_vk("") == 0);
    CHECK(key_setting_vk("nonsense") == 0);
}

TEST_CASE("Hotkeys: one press per push, each key on its own") {
    Hotkeys h;
    std::set<uint32_t> down;
    auto is_down = [&](uint32_t vk) { return down.count(vk) > 0; };
    h.set_keys(VK_F9, VK_PAUSE);
    h.poll(is_down);  // nothing held
    down = {VK_F9};
    CHECK(h.poll(is_down).page);
    CHECK_FALSE(h.poll(is_down).page);
    CHECK_FALSE(h.poll(is_down).page);
    down = {};
    CHECK_FALSE(h.poll(is_down).page);
    down = {VK_F9};
    CHECK(h.poll(is_down).page);
    // Pause pressed while F9 is held: reset only
    down = {VK_F9, VK_PAUSE};
    const auto p = h.poll(is_down);
    CHECK(p.reset);
    CHECK_FALSE(p.page);
}

TEST_CASE("Hotkeys: a key changed while held does not fire; 0 never fires") {
    Hotkeys h;
    std::set<uint32_t> down{VK_F9};
    auto is_down = [&](uint32_t vk) { return down.count(vk) > 0; };
    h.set_keys(VK_F9, VK_PAUSE);
    h.poll(is_down);  // F9 held when the keys were set: that poll only records it
    h.set_keys(VK_F9, 0);
    CHECK_FALSE(h.poll(is_down).page);  // unchanged key, still held
    down = {VK_PAUSE};
    CHECK_FALSE(h.poll(is_down).reset);  // reset is no key now
    h.set_keys(VK_PAUSE, VK_F9);         // the keys swapped while Pause is held
    CHECK_FALSE(h.poll(is_down).page);
    down = {};
    h.poll(is_down);
    down = {VK_PAUSE};
    CHECK(h.poll(is_down).page);
}
