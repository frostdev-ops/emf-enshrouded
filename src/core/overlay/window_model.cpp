#include "window_model.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "core/api_sizes.h"

namespace emf::core {

namespace {

constexpr uint32_t kWidgetMinSize = EMF_FIELD_END(EmfUiWidget, rgba);  // the fields phase 1 draws

bool id_ok(const char* id) {
    if (!id) return false;
    const size_t n = std::strlen(id);
    if (n == 0 || n > kMaxWindowIdLen) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = id[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// The string at `p` (NULL = ""), refused above `max` bytes.
bool copy_text(const char* p, size_t max, std::string* out) {
    if (!p) {
        out->clear();
        return true;
    }
    const size_t n = strnlen(p, max + 1);
    if (n > max) return false;
    out->assign(p, n);
    return true;
}

// Fields of widget `i` read through the mod's stride: those past widget_size are 0.
EmfUiWidget widget_at(const EmfWindowDef& def, uint32_t i) {
    EmfUiWidget w{};
    const auto* base = reinterpret_cast<const uint8_t*>(def.widgets) + static_cast<size_t>(i) * def.widget_size;
    std::memcpy(&w, base, def.widget_size < sizeof w ? def.widget_size : sizeof w);
    return w;
}

}  // namespace

bool copy_window_def(const EmfWindowDef* def, bool for_update, WindowRequest* out, std::string* why) {
    const auto refuse = [why](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!def || def->size < kWindowDefV24Size) return refuse("no window definition, or its size field is too small");
    if (!id_ok(def->id)) return refuse("id must be 1-48 of A-Z a-z 0-9 _ . -");
    if (def->count > kMaxWindowWidgets) return refuse("at most 256 widgets");
    if (def->count && !def->widgets) return refuse("widgets is NULL with a count");
    if (def->count && def->widget_size < kWidgetMinSize)
        return refuse("widget_size is smaller than an EmfUiWidget (pass sizeof(EmfUiWidget))");
    if (def->world_kind != EMF_WORLD_SERVER && def->world_kind != EMF_WORLD_CLIENT)
        return refuse("world_kind must be EMF_WORLD_SERVER or EMF_WORLD_CLIENT");
    if (def->place > EMF_UI_PLACE_RIGHT) return refuse("place must be EMF_UI_PLACE_CENTRE, _LEFT or _RIGHT");
    if (!std::isfinite(def->width) || def->width < 0 || def->width > 200)
        return refuse("width must be 0 (the default) to 200 text heights");
    const uint32_t ck = def->close_key;
    if (ck > 0xFF || (ck >= VK_LBUTTON && ck <= VK_XBUTTON2) || (ck >= VK_LSHIFT && ck <= VK_RMENU))
        return refuse("close_key must be 0 or a keyboard key's generic virtual-key code (no mouse buttons)");
    if (!for_update && !def->cb) return refuse("no callback");

    WindowRequest r;
    r.mod = out->mod;
    r.id = def->id;
    if (!copy_text(def->title, kMaxWindowTitleLen, &r.title)) return refuse("title is longer than 64 bytes");
    r.widgets.reserve(def->count);
    for (uint32_t i = 0; i < def->count; ++i) {
        const EmfUiWidget w = widget_at(*def, i);
        const std::string at = "widget " + std::to_string(i) + ": ";
        if (w.type > EMF_UI_SEPARATOR) return refuse(at + "unknown type " + std::to_string(w.type));
        WindowWidget c;
        c.type = w.type;
        c.flags = w.flags;
        if (!copy_text(w.key, kMaxWidgetKeyLen, &c.key)) return refuse(at + "key is longer than 48 bytes");
        if (!copy_text(w.text, kMaxWidgetTextLen, &c.text)) return refuse(at + "text is longer than 256 bytes");
        if (!copy_text(w.tooltip, kMaxWidgetTooltipLen, &c.tooltip))
            return refuse(at + "tooltip is longer than 512 bytes");
        c.count = w.count;
        c.value = w.value > 1000 ? 1000 : w.value;
        c.rgba = w.rgba;
        r.widgets.push_back(std::move(c));
    }
    r.close_vk = static_cast<uint16_t>(ck);
    r.place = def->place;
    r.width = def->width > 0 ? def->width : kDefaultWindowWidth;
    r.world_kind = def->world_kind;
    if (!for_update) {
        r.cb = def->cb;
        r.user = def->user;
    }
    *out = std::move(r);
    return true;
}

}  // namespace emf::core
