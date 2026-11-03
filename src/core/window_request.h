#pragma once
// A mod's window (API v24 window_open / window_update), as the core copies it: every string owned here.
#include <cstdint>
#include <string>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

struct WindowWidget {
    uint32_t type = EMF_UI_TEXT;
    uint32_t flags = 0;
    std::string key, text, tooltip;
    uint32_t count = 0;
    uint32_t value = 0;  // bar: 0..1000
    uint32_t rgba = 0;
};

struct WindowRequest {
    std::string id, title;
    std::vector<WindowWidget> widgets;
    uint16_t close_vk = 0;  // a key that also closes it (0 = Esc only)
    uint32_t place = EMF_UI_PLACE_CENTRE;
    float width = 0;        // in text heights
    uint8_t world_kind = EMF_WORLD_SERVER;  // where its events are delivered
    EmfUiCallback cb = nullptr;
    void* user = nullptr;
    std::string mod;        // the module that opened it (its file name), for logs and for closing a mod's window
};

constexpr size_t kMaxWindowWidgets = 256;
constexpr size_t kMaxWindowIdLen = 48;
constexpr size_t kMaxWindowTitleLen = 64;
constexpr size_t kMaxWidgetKeyLen = 48;
constexpr size_t kMaxWidgetTextLen = 256;
constexpr size_t kMaxWidgetTooltipLen = 512;
constexpr float kDefaultWindowWidth = 28.0f;

}  // namespace emf::core
