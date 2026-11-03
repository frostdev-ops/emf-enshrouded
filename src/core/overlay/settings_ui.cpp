#include "settings_ui.h"

#include <algorithm>
#include <vector>

#include "imgui.h"

namespace emf::core {

void draw_settings_tab(SettingsStore* s, ModsUiState& ui, SettingsUiState& st, const std::function<void()>& preview) {
    static const char* const kPages[] = {kPageText, kPageConsole, kPageStart};
    if (std::find(std::begin(kPages), std::end(kPages), st.page) == std::end(kPages)) st.page = kPageText;
    if (!s) {
        ImGui::TextDisabled("EMF's settings are not available.");
        return;
    }
    const int frame = ImGui::GetFrameCount();
    forget_stale_edits(ui, frame);  // edits and captures of a tab or page left half-way
    const float list_w = (std::max)(ImGui::GetFontSize() * 12.0f, ImGui::GetContentRegionAvail().x * 0.18f);
    ImGui::BeginChild("##settings_pages", ImVec2(list_w, 0), ImGuiChildFlags_Borders);
    for (const char* page : kPages)
        if (ImGui::Selectable(page, st.page == page)) {
            if (st.page != page) ui.message.clear();
            st.page = page;
        }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##settings_page", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const std::vector<SettingDef> defs = s->defs(kEmfSettingsId);
    if (st.page == kPageText && preview) {
        preview();
        ImGui::Spacing();
    }
    for (const SettingDef& d : defs)
        if (d.group == st.page) draw_setting(*s, kEmfSettingsId, d, defs, ui, frame);
    if (st.page == kPageStart) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled(
            "Switching the overlay off, chat admins and the debug bridge: emf_core.ini or EMF.exe's Server tab.");
        ImGui::PopTextWrapPos();
    }
    if (!ui.message.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ui.message_error ? ImVec4(1.0f, 0.48f, 0.45f, 1.0f) : ImVec4(0.49f, 0.91f, 0.53f, 1.0f), "%s",
                           ui.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

}  // namespace emf::core
