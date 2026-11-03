#include "window_ui.h"

#include <cstdio>

#include "wheel_ui.h"  // wheel_label_lines: "Name (detail)"

namespace emf::core {

namespace {

ImU32 rgba_u32(uint32_t rgba) {
    return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}
ImVec4 rgba_vec(uint32_t rgba) { return ImGui::ColorConvertU32ToFloat4(rgba_u32(rgba)); }

// A square slot: frame, its look (empty, locked, selected), the text centred and cut to fit, the count in the corner.
bool draw_slot(const WindowWidget& w, bool clickable, const TextLook& look) {
    const float side = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
    const ImVec2 size(side, side);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = false;
    if (clickable)
        pressed = ImGui::InvisibleButton("slot", size, ImGuiButtonFlags_MouseButtonLeft);
    else
        ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + side, p.y + side);
    const bool hovered = clickable && ImGui::IsItemHovered();
    ImU32 fill = ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    if (w.flags & EMF_UI_EMPTY) fill = ImGui::GetColorU32(ImGuiCol_FrameBg, 0.4f);
    if (w.flags & EMF_UI_LOCKED) fill = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.25f);
    dl->AddRectFilled(p, q, fill, 4.0f);
    const bool lit = (w.flags & EMF_UI_SELECTED) != 0;
    dl->AddRect(p, q, lit ? rgba_u32(look.accent) : ImGui::GetColorU32(ImGuiCol_Border), 4.0f, 0, lit ? 2.5f : 1.0f);
    if (w.flags & EMF_UI_LOCKED) {  // a locked slot: two strokes across
        const ImU32 c = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.5f);
        dl->AddLine(ImVec2(p.x + 6, p.y + 6), ImVec2(q.x - 6, q.y - 6), c, 1.5f);
        dl->AddLine(ImVec2(q.x - 6, p.y + 6), ImVec2(p.x + 6, q.y - 6), c, 1.5f);
    }
    const bool dim = (w.flags & (EMF_UI_EMPTY | EMF_UI_LOCKED | EMF_UI_DIM | EMF_UI_DISABLED)) != 0;
    const ImU32 text_col = w.rgba ? rgba_u32(w.rgba) : ImGui::GetColorU32(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    const std::string text = wheel_fit(w.text, side - 8.0f, [](const std::string& s) {
        return ImGui::CalcTextSize(s.c_str()).x;
    });
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    dl->AddText(ImVec2(p.x + (side - ts.x) * 0.5f, p.y + (side - ts.y) * 0.5f), text_col, text.c_str());
    if (w.count) {
        char n[16];
        std::snprintf(n, sizeof n, "%u", w.count);
        const ImVec2 ns = ImGui::CalcTextSize(n);
        dl->AddText(ImVec2(q.x - ns.x - 4.0f, q.y - ns.y - 2.0f), ImGui::GetColorU32(ImGuiCol_Text), n);
    }
    return pressed;
}

}  // namespace

std::string window_imgui_name(const WindowRequest& w) { return w.title + "###emf.window." + w.id; }

std::optional<WindowClick> draw_window(const WindowRequest& w, WindowUiState& st, bool input, ImVec2 display,
                                       const TextLook& look) {
    if (st.id != w.id) st = WindowUiState{w.id};
    st.hit.clear();
    if (look.font) ImGui::PushFont(look.font);
    const float em = ImGui::GetFontSize();
    const float width = (w.width > 0 ? w.width : kDefaultWindowWidth) * em;
    ImVec2 pos(display.x * 0.5f, display.y * 0.5f), pivot(0.5f, 0.5f);
    if (w.place == EMF_UI_PLACE_LEFT) {
        pos.x = display.x * 0.05f;
        pivot.x = 0.0f;
    } else if (w.place == EMF_UI_PLACE_RIGHT) {
        pos.x = display.x * 0.95f;
        pivot.x = 1.0f;
    }
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, display.y * 0.85f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (!input) flags |= ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, input ? 1.0f : 0.6f);
    bool open = true;
    std::optional<WindowClick> click;
    const std::string name = window_imgui_name(w);
    if (ImGui::Begin(name.c_str(), input ? &open : nullptr, flags)) {
        for (size_t i = 0; i < w.widgets.size(); ++i) {
            const WindowWidget& x = w.widgets[i];
            ImGui::PushID(static_cast<int>(i));
            if (i > 0 && (x.flags & EMF_UI_SAME_LINE) && x.type != EMF_UI_SEPARATOR) ImGui::SameLine();
            const bool disabled = (x.flags & EMF_UI_DISABLED) != 0;
            const bool clickable = input && !disabled && !x.key.empty() &&
                                   (x.type == EMF_UI_BUTTON || x.type == EMF_UI_ROW || x.type == EMF_UI_SLOT);
            int colours = 0;
            if (x.rgba) {
                ImGui::PushStyleColor(ImGuiCol_Text, rgba_vec(x.rgba));
                ++colours;
            } else if (x.flags & (EMF_UI_DIM | EMF_UI_DISABLED)) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ++colours;
            }
            bool pressed = false;
            switch (x.type) {
                case EMF_UI_TEXT:
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(x.text.c_str());
                    ImGui::PopTextWrapPos();
                    break;
                case EMF_UI_HEADING:
                    if (look.bold) ImGui::PushFont(look.bold);
                    ImGui::TextUnformatted(x.text.c_str());
                    if (look.bold) ImGui::PopFont();
                    break;
                case EMF_UI_BUTTON: {
                    if (x.flags & EMF_UI_SELECTED) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        ++colours;
                    }
                    if (disabled) ImGui::BeginDisabled();
                    pressed = ImGui::Button(x.text.empty() ? " " : x.text.c_str());
                    if (disabled) ImGui::EndDisabled();
                    break;
                }
                case EMF_UI_ROW: {
                    const auto lines = wheel_label_lines(x.text);
                    if (disabled) ImGui::BeginDisabled();
                    pressed = ImGui::Selectable(lines.first.empty() ? " " : lines.first.c_str(),
                                                (x.flags & EMF_UI_SELECTED) != 0);
                    if (disabled) ImGui::EndDisabled();
                    if (!lines.second.empty()) {
                        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                        const ImVec2 ds = ImGui::CalcTextSize(lines.second.c_str());
                        ImGui::GetWindowDrawList()->AddText(ImVec2(max.x - ds.x, min.y),
                                                            ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                                            lines.second.c_str());
                    }
                    break;
                }
                case EMF_UI_SLOT: pressed = draw_slot(x, clickable, look); break;
                case EMF_UI_BAR:
                    ImGui::ProgressBar(static_cast<float>(x.value) / 1000.0f, ImVec2(-1.0f, 0.0f),
                                       x.text.empty() ? "" : x.text.c_str());
                    break;
                case EMF_UI_SEPARATOR: ImGui::Separator(); break;
                default: break;
            }
            if (colours) ImGui::PopStyleColor(colours);
            if (input && !x.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", x.tooltip.c_str());
            if (clickable) {
                st.hit.emplace_back(x.key, ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
                const bool right = ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
                if (!click && (pressed || right)) click = WindowClick{x.key, !pressed && right};
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    if (look.font) ImGui::PopFont();
    if (input && !open) st.close_clicked = true;
    return click;
}

}  // namespace emf::core
