#include "look_layout.h"

#include "core/hud_editor/layout.h"
#include "core/hud_editor/snapshot.h"

#include <algorithm>
#include <cmath>

namespace emf::core {
namespace {

constexpr float kHudLineStep = 1.15f;  // a HUD line's distance to the next, in line heights
constexpr float kTopRightY = 0.24f;    // the top-right lines start under the game's quest tracker (seen in game)
constexpr float kNotesTopY = 0.075f;   // the top-centre notes start under the game's compass
constexpr float kNotesRightY = 0.40f;  // the right-hand notes: a little above the middle of the screen
constexpr float kBandFade = 4.0f;      // the band's fade at each end, in line heights
constexpr float kBandPadY = 0.35f;     // the band above and below the text, in line heights

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t, float alpha) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha);
}

}  // namespace

ImU32 look_colour(uint32_t rgba) {
    return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}

ImVec4 look_colour4(uint32_t rgba) {
    return ImVec4(((rgba >> 24) & 0xFF) / 255.0f, ((rgba >> 16) & 0xFF) / 255.0f, ((rgba >> 8) & 0xFF) / 255.0f,
                  (rgba & 0xFF) / 255.0f);
}

float shadow_offset(float pixel_size) { return std::clamp(std::round(pixel_size / 18.0f), 1.0f, 3.0f); }

TextPasses text_passes(HudStyle style, bool shadow, float pixel_size) {
    TextPasses p;
    if (style == HudStyle::Game && shadow) {
        for (const ImVec2 d : {ImVec2(-1, 0), ImVec2(1, 0), ImVec2(0, -1), ImVec2(0, 1)})  // the halo
            p.pass[p.count++] = {d, kShadowHaloAlpha, true};
        const float o = shadow_offset(pixel_size);
        p.pass[p.count++] = {ImVec2(o, o), kShadowAlpha, true};  // the drop shadow
    }
    p.pass[p.count++] = {ImVec2(0, 0), 1.0f, false};  // the text, on top
    return p;
}

void utf8_pop_back(std::string& s) {
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();  // continuation bytes
    if (!s.empty()) s.pop_back();  // and the character's first byte
}

std::vector<std::string> wrap_rows(const std::string& text, float width,
                                   const std::function<float(const std::string&)>& measure, size_t max_rows) {
    std::vector<std::string> rows;
    if (text.empty() || max_rows == 0) return rows;
    bool more = false;  // text was left over
    const auto add = [&](const std::string& row) {
        if (rows.size() == max_rows) {
            more = true;
            return false;
        }
        rows.push_back(row);
        return true;
    };
    size_t at = 0;
    while (at <= text.size() && !more) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string para = text.substr(at, nl - at);
        if (!para.empty() && para.back() == '\r') para.pop_back();
        std::string row;
        size_t w = 0;
        while (w <= para.size() && !more) {
            size_t sp = para.find(' ', w);
            if (sp == std::string::npos) sp = para.size();
            std::string word = para.substr(w, sp - w);
            w = sp + 1;
            if (word.empty()) {
                if (sp >= para.size()) break;
                continue;
            }
            const std::string with = row.empty() ? word : row + " " + word;
            if (measure(with) <= width) {
                row = with;
            } else {
                if (!row.empty() && !add(row)) break;
                row.clear();
                while (measure(word) > width && !more) {  // a word wider than a row: cut between characters
                    std::string head = word;  // the most that fits, at least one character
                    while (measure(head) > width) {
                        std::string shorter = head;
                        utf8_pop_back(shorter);
                        if (shorter.empty()) break;
                        head = shorter;
                    }
                    if (!add(head)) break;
                    word = word.substr(head.size());
                }
                row = word;
            }
            if (sp >= para.size()) break;
        }
        if (!more && (!row.empty() || para.empty())) add(row);
        at = nl + 1;
        if (nl >= text.size()) break;
    }
    if (more && !rows.empty()) {  // say that text was left out
        std::string& last = rows.back();
        while (!last.empty() && measure(last + "...") > width) utf8_pop_back(last);
        last += "...";
    }
    return rows;
}

ImVec2 hud_line_position(HudPlace place, size_t index, size_t count, ImVec2 text, float line_h, ImVec2 screen) {
    const float pad = line_h;
    const float step = line_h * kHudLineStep;
    count = (std::max<size_t>)(count, 1);
    index = (std::min)(index, count - 1);
    const float x = place == HudPlace::BottomLeft ? pad : (std::max)(pad, screen.x - pad - text.x);  // never off the left
    if (place == HudPlace::TopRight) return ImVec2(x, screen.y * kTopRightY + step * static_cast<float>(index));
    const float last = screen.y - pad - line_h;  // the bottom lines grow upwards from here
    return ImVec2(x, last - step * static_cast<float>(count - 1 - index));
}

LinesEdit lines_edit(const hud::HudSnapshot* snap, HudPlace place, ImVec2 display) {
    LinesEdit e;
    if (!snap || !snap->active) return e;
    const int i = hud::lines_element(static_cast<int>(place));
    if (i < 0) return e;
    const hud::ElementEdit& ed = snap->edits[static_cast<size_t>(i)];
    e.offset = ImVec2(hud::percent_to_units(ed.x_percent, display.x), hud::percent_to_units(ed.y_percent, display.y));
    e.scale = hud::clamp_scale(ed.scale);
    e.hidden = ed.hidden;
    return e;
}

float note_wrap_width(NotesPlace, ImVec2 screen) { return screen.x * 0.50f; }

NoteBand note_band(NotesPlace place, ImVec2 text, float line_h, ImVec2 screen) {
    NoteBand b;
    const float pad_x = line_h, pad_y = line_h * kBandPadY;
    const float solid = text.x + 2.0f * pad_x;
    const float height = text.y + 2.0f * pad_y;
    switch (place) {
        case NotesPlace::TopLeft: {  // from the left edge, at EMF's old place
            const float fade = (std::max)(0.0f, (std::min)(line_h * kBandFade, screen.x - solid));
            b.min = ImVec2(0.0f, line_h - pad_y);
            b.max = ImVec2(solid + fade, b.min.y + height);
            b.fade_right = fade;
            b.text_min = ImVec2(pad_x, line_h);
            break;
        }
        case NotesPlace::Right: {  // from the right edge
            const float fade = (std::max)(0.0f, (std::min)(line_h * kBandFade, screen.x - solid));
            b.max = ImVec2(screen.x, screen.y * kNotesRightY + height);
            b.min = ImVec2(screen.x - solid - fade, screen.y * kNotesRightY);
            b.fade_left = fade;
            b.text_min = ImVec2(screen.x - pad_x - text.x, b.min.y + pad_y);
            break;
        }
        default: {  // top centre, under the compass, fading at both ends
            const float fade = (std::max)(0.0f, (std::min)(line_h * kBandFade, (screen.x - solid) * 0.5f));
            const float width = solid + 2.0f * fade;
            b.min = ImVec2(screen.x * 0.5f - width * 0.5f, screen.y * kNotesTopY);
            b.max = ImVec2(b.min.x + width, b.min.y + height);
            b.fade_left = b.fade_right = fade;
            b.text_min = ImVec2(screen.x * 0.5f - text.x * 0.5f, b.min.y + pad_y);
            break;
        }
    }
    b.text_max = ImVec2(b.text_min.x + text.x, b.text_min.y + text.y);
    return b;
}

float note_line_x(NotesPlace place, const NoteBand& band, float line_w) {
    switch (place) {
        case NotesPlace::TopLeft: return band.text_min.x;
        case NotesPlace::Right: return band.text_max.x - line_w;
        default: return (band.text_min.x + band.text_max.x) * 0.5f - line_w * 0.5f;
    }
}

void draw_note_band(ImDrawList* dl, const NoteBand& band, uint32_t rgba) {
    const ImU32 full = look_colour(rgba), none = look_colour(rgba & 0xFFFFFF00u);
    const float a = band.min.x + band.fade_left, b = band.max.x - band.fade_right;
    if (band.fade_left > 0.0f)
        dl->AddRectFilledMultiColor(band.min, ImVec2(a, band.max.y), none, full, full, none);
    if (b > a) dl->AddRectFilledMultiColor(ImVec2(a, band.min.y), ImVec2(b, band.max.y), full, full, full, full);
    if (band.fade_right > 0.0f)
        dl->AddRectFilledMultiColor(ImVec2(b, band.min.y), band.max, full, none, none, full);
}

void draw_look_text(ImDrawList* dl, const TextLook& look, ImFont* font, float size, ImVec2 pos, uint32_t rgba,
                    const char* begin, const char* end, float wrap) {
    if (!font) font = ImGui::GetFont();
    if (size <= 0.0f) size = font->FontSize;
    const float alpha = (rgba & 0xFF) / 255.0f;
    for (const TextPass& p : text_passes(look.style, look.shadow, size)) {
        const ImU32 c = p.shadow ? IM_COL32(0, 0, 0, static_cast<int>(p.alpha * alpha * 255.0f + 0.5f)) : look_colour(rgba);
        dl->AddText(font, size, ImVec2(pos.x + p.offset.x, pos.y + p.offset.y), c, begin, end, wrap);
    }
}

void apply_look_theme(ImGuiStyle& style, const LookSettings& look) {
    ImVec4* c = style.Colors;
    const ImVec4 bg = look_colour4(look.background), a = look_colour4(look.accent), white(1, 1, 1, 1);
    c[ImGuiCol_WindowBg] = ImVec4(bg.x, bg.y, bg.z, 1.0f);  // the console sets its own opacity
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(bg.x, bg.y, bg.z, 0.96f);
    c[ImGuiCol_Border] = mix(bg, a, 0.30f, 0.50f);
    c[ImGuiCol_FrameBg] = mix(bg, white, 0.08f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = mix(bg, a, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgActive] = mix(bg, a, 0.32f, 1.0f);
    c[ImGuiCol_TitleBg] = mix(bg, white, 0.03f, 1.0f);
    c[ImGuiCol_TitleBgActive] = mix(bg, a, 0.25f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(bg.x, bg.y, bg.z, 0.5f);
    c[ImGuiCol_MenuBarBg] = mix(bg, white, 0.05f, 1.0f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(bg.x * 0.6f, bg.y * 0.6f, bg.z * 0.6f, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = mix(bg, a, 0.45f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = mix(bg, a, 0.65f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = mix(bg, a, 0.85f, 1.0f);
    c[ImGuiCol_CheckMark] = a;
    c[ImGuiCol_SliderGrab] = mix(bg, a, 0.80f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = a;
    c[ImGuiCol_Button] = mix(bg, a, 0.30f, 1.0f);
    c[ImGuiCol_ButtonHovered] = mix(bg, a, 0.50f, 1.0f);
    c[ImGuiCol_ButtonActive] = mix(bg, a, 0.70f, 1.0f);
    // the selection stays dark, so coloured text on it (the Mods tab's status colours) stays readable, also on HDR
    c[ImGuiCol_Header] = mix(bg, a, 0.30f, 1.0f);
    c[ImGuiCol_HeaderHovered] = mix(bg, a, 0.22f, 1.0f);
    c[ImGuiCol_HeaderActive] = mix(bg, a, 0.38f, 1.0f);
    c[ImGuiCol_Separator] = mix(bg, a, 0.25f, 0.60f);
    c[ImGuiCol_SeparatorHovered] = mix(bg, a, 0.60f, 0.80f);
    c[ImGuiCol_SeparatorActive] = a;
    c[ImGuiCol_ResizeGrip] = ImVec4(a.x, a.y, a.z, 0.20f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(a.x, a.y, a.z, 0.60f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(a.x, a.y, a.z, 0.90f);
    c[ImGuiCol_Tab] = mix(bg, a, 0.18f, 1.0f);
    c[ImGuiCol_TabHovered] = mix(bg, a, 0.50f, 1.0f);
    c[ImGuiCol_TabSelected] = mix(bg, a, 0.35f, 1.0f);
    c[ImGuiCol_TabSelectedOverline] = a;
    c[ImGuiCol_TabDimmed] = mix(bg, a, 0.10f, 1.0f);
    c[ImGuiCol_TabDimmedSelected] = mix(bg, a, 0.25f, 1.0f);
    c[ImGuiCol_TabDimmedSelectedOverline] = mix(bg, a, 0.50f, 1.0f);
    c[ImGuiCol_TextLink] = a;
    c[ImGuiCol_TextSelectedBg] = ImVec4(a.x, a.y, a.z, 0.35f);
    c[ImGuiCol_NavCursor] = a;
}

}  // namespace emf::core
