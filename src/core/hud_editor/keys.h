#pragma once
// The HUD page's keyboard model (Tab, arrows, + and -, H, R) and the two global keys: open the page, reset the HUD.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace emf::core::hud {

enum class PageKey { Tab, Left, Right, Up, Down, Plus, Minus, H, R, Other };
enum class PageAction { None, Next, Previous, Move, ScaleUp, ScaleDown, ToggleHide, Reset };
struct PageCommand {
    PageAction action = PageAction::None;
    float dx = 0, dy = 0;  // Move: percent
};
constexpr float kNudge = 0.5f, kNudgeBig = 5.0f, kScaleStep = 0.05f;

PageCommand page_command(PageKey key, bool shift);
// The next (or previous) part, wrapping over kHudElementCount.
size_t step_element(size_t selected, bool back);
// parse_key on the setting's text; "" or an unknown key gives 0 (no key).
uint32_t key_setting_vk(const std::string& value);

// One press per push of each key. A key set (or changed) while held starts as "was down", so the press that set it does
// not fire.
class Hotkeys {
public:
    void set_keys(uint32_t page_vk, uint32_t reset_vk);  // 0 = none
    struct Pressed {
        bool page = false, reset = false;
    };
    Pressed poll(const std::function<bool(uint32_t vk)>& down);

private:
    uint32_t page_vk_ = 0, reset_vk_ = 0;
    bool page_was_down_ = false, reset_was_down_ = false;
    bool changed_ = false;  // keys changed since the last poll: that poll only records the state
};

}  // namespace emf::core::hud
