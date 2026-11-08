#include "core/hud_editor/keys.h"

#include <windows.h>

#include "core/hud_editor/elements.h"
#include "emf/keys.hpp"

namespace emf::core::hud {

PageCommand page_command(PageKey key, bool shift) {
    const float step = shift ? kNudgeBig : kNudge;
    PageCommand c;
    switch (key) {
        case PageKey::Tab: c.action = shift ? PageAction::Previous : PageAction::Next; break;
        case PageKey::Left: c = {PageAction::Move, -step, 0}; break;
        case PageKey::Right: c = {PageAction::Move, step, 0}; break;
        case PageKey::Up: c = {PageAction::Move, 0, -step}; break;
        case PageKey::Down: c = {PageAction::Move, 0, step}; break;
        case PageKey::Plus: c.action = PageAction::ScaleUp; break;
        case PageKey::Minus: c.action = PageAction::ScaleDown; break;
        case PageKey::H: c.action = PageAction::ToggleHide; break;
        case PageKey::R: c.action = PageAction::Reset; break;
        case PageKey::Other: break;
    }
    return c;
}

size_t step_element(size_t selected, bool back) {
    if (back) return selected == 0 ? kHudElementCount - 1 : selected - 1;
    return selected + 1 >= kHudElementCount ? 0 : selected + 1;
}

uint32_t key_setting_vk(const std::string& value) {
    if (value.empty()) return 0;
    const std::wstring w(value.begin(), value.end());
    return emf::keys::parse_key(w);
}

void Hotkeys::set_keys(uint32_t page_vk, uint32_t reset_vk) {
    if (page_vk == page_vk_ && reset_vk == reset_vk_) return;
    page_vk_ = page_vk;
    reset_vk_ = reset_vk;
    changed_ = true;
}

Hotkeys::Pressed Hotkeys::poll(const std::function<bool(uint32_t vk)>& down) {
    Pressed p;
    const bool page_now = page_vk_ != 0 && down && down(page_vk_);
    const bool reset_now = reset_vk_ != 0 && down && down(reset_vk_);
    if (!changed_) {
        p.page = page_now && !page_was_down_;
        p.reset = reset_now && !reset_was_down_;
    }
    changed_ = false;
    page_was_down_ = page_now;
    reset_was_down_ = reset_now;
    return p;
}

}  // namespace emf::core::hud
