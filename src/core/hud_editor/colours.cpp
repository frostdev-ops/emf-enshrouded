#include "core/hud_editor/colours.h"

#include <cmath>
#include <cstdio>

namespace emf::core::hud {

namespace {

constexpr const char* kPartIds[kBarPartCount] = {"fill", "empty", "end", "damage", "heal"};

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool equals_nocase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

constexpr size_t field_of(BarPart p) {
    switch (p) {
        case BarPart::Fill: return kFill;
        case BarPart::Empty: return kRange;
        case BarPart::End: return kFillEnd;
        case BarPart::Damage: return kDamage;
        case BarPart::Heal: return kHeal;
    }
    return kFill;
}

}  // namespace

const char* bar_part_id(BarPart p) {
    const size_t i = static_cast<size_t>(p);
    return i < kBarPartCount ? kPartIds[i] : "";
}

std::optional<BarPart> find_bar_part(std::string_view word) {
    for (size_t i = 0; i < kBarPartCount; ++i)
        if (equals_nocase(word, kPartIds[i])) return static_cast<BarPart>(i);
    return std::nullopt;
}

bool BarColours::any() const {
    for (const Colour& c : part)
        if (c.set) return true;
    return false;
}

ColourText parse_colour(std::string_view text) {
    ColourText r;
    if (equals_nocase(text, "game")) {
        r.game = true;
        return r;
    }
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8) return r;
    uint32_t v = 0;
    for (char c : text) {
        const int d = hex_digit(c);
        if (d < 0) return r;
        v = v << 4 | static_cast<uint32_t>(d);
    }
    Colour c;
    c.set = true;
    c.has_alpha = text.size() == 8;
    c.rgba = c.has_alpha ? v : (v << 8 | 0xFFu);
    r.colour = c;
    return r;
}

std::string colour_text(const Colour& c) {
    if (!c.set) return "game";
    char buf[16];
    if (c.has_alpha)
        std::snprintf(buf, sizeof buf, "#%08x", c.rgba);
    else
        std::snprintf(buf, sizeof buf, "#%06x", c.rgba >> 8);
    return buf;
}

uint32_t rgba_to_game(uint32_t rgba) {
    return (rgba >> 24 & 0xFFu) | (rgba >> 8 & 0xFF00u) | (rgba << 8 & 0xFF0000u) | (rgba << 24 & 0xFF000000u);
}

uint32_t game_to_rgba(uint32_t value) { return rgba_to_game(value); }

UiColour with_colour(UiColour game, const Colour& c) {
    if (!c.set) return game;
    const uint32_t alpha = c.has_alpha ? (c.rgba & 0xFFu) : (game_to_rgba(game.value) & 0xFFu);
    return UiColour{rgba_to_game((c.rgba & 0xFFFFFF00u) | alpha), game.intensity};
}

uint32_t lighten(uint32_t rgba, float toward_white) {
    float f = toward_white;
    if (!(f > 0.0f)) return rgba;
    if (f > 1.0f) f = 1.0f;
    uint32_t out = rgba & 0xFFu;
    for (int shift : {24, 16, 8}) {
        const uint32_t c = rgba >> shift & 0xFFu;
        const uint32_t moved = c + static_cast<uint32_t>(std::lround(static_cast<float>(255u - c) * f));
        out |= (moved > 255u ? 255u : moved) << shift;
    }
    return out;
}

StyleBlock recoloured(const StyleBlock& game, const BarColours& c) {
    StyleBlock out = game;
    for (size_t i = 0; i < kBarPartCount; ++i) {
        const BarPart p = static_cast<BarPart>(i);
        const size_t f = field_of(p);
        out[f] = with_colour(game[f], c.part[i]);
    }
    const Colour& fill = c.part[static_cast<size_t>(BarPart::Fill)];
    const Colour& end = c.part[static_cast<size_t>(BarPart::End)];
    if (fill.set && !end.set) {
        Colour follow;
        follow.set = true;
        follow.has_alpha = false;  // the game's alpha for the end
        follow.rgba = lighten(fill.rgba, kEndLighten);
        out[kFillEnd] = with_colour(game[kFillEnd], follow);
    }
    return out;
}

std::string ui_colour_text(UiColour c) {
    const uint32_t rgba = game_to_rgba(c.value);
    char buf[48];
    int n = std::snprintf(buf, sizeof buf, "%06x", rgba >> 8);
    if ((rgba & 0xFFu) != 0xFFu) n += std::snprintf(buf + n, sizeof buf - static_cast<size_t>(n), "-%02x", rgba & 0xFFu);
    if (c.intensity != 1.0f)
        std::snprintf(buf + n, sizeof buf - static_cast<size_t>(n), "*%.2f", static_cast<double>(c.intensity));
    return buf;
}

}  // namespace emf::core::hud
