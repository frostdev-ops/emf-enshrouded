#include "console_ui.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <iterator>
#include <string>

namespace emf::core {
namespace {

constexpr size_t kInputCap = 1024;

ImVec4 colour(LineKind k) {
    switch (k) {
        case LineKind::Command: return ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
        case LineKind::Error: return ImVec4(1.00f, 0.48f, 0.45f, 1.0f);
        case LineKind::Success: return ImVec4(0.49f, 0.91f, 0.53f, 1.0f);
        default: return ImVec4(0.70f, 0.73f, 0.76f, 1.0f);
    }
}

// The suggestions' colours: names light, argument hints a little dimmer, help grey, the argument being typed gold.
const ImVec4 kSuggestName(0.86f, 0.88f, 0.90f, 1.0f);
const ImVec4 kSuggestPicked(1.00f, 1.00f, 1.00f, 1.0f);
const ImVec4 kSuggestHint(0.66f, 0.71f, 0.77f, 1.0f);
const ImVec4 kSuggestHelp(0.52f, 0.55f, 0.59f, 1.0f);
const ImVec4 kSuggestCurrent(0.75f, 0.69f, 0.45f, 1.0f);
const ImVec4 kSuggestRowBack(0.75f, 0.69f, 0.45f, 0.22f);
const ImVec4 kGhost(0.52f, 0.55f, 0.59f, 1.0f);

ImVec4 piece_colour(HintPiece::Kind k) {
    switch (k) {
        case HintPiece::Kind::Command: return kSuggestPicked;
        case HintPiece::Kind::Current: return kSuggestCurrent;
        case HintPiece::Kind::Help: return kSuggestHelp;
        default: return kSuggestHint;
    }
}

void text(const std::string& s, const ImVec4& c) {
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextUnformatted(s.c_str(), s.c_str() + s.size());
    ImGui::PopStyleColor();
}

// The hint line and the matching rows in a box under the input line (no input of its own). Returns the box's
// bottom edge (`under.y` when nothing shows).
float draw_suggestions(const Suggester& g, ImVec2 under, ImVec2 display) {
    const Suggestions& s = g.current();
    const bool rows = g.showing();
    if (!rows && s.hint.empty()) return under.y;
    ImGui::SetNextWindowPos(under);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2((std::max)(display.x - 2.0f * under.x, 100.0f), FLT_MAX));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##emf_suggest", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!s.hint.empty()) {  // "give <item name> [count]: drop items at your feet (host)", the current argument gold
        for (size_t i = 0; i < s.hint.size(); ++i) {
            if (i) ImGui::SameLine(0.0f, 0.0f);
            const ImVec4 c = piece_colour(s.hint[i].kind);
            text(s.hint[i].text, c);
            if (s.hint[i].kind == HintPiece::Kind::Current) {  // underlined too (one font: no bold)
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                draw->AddLine(ImVec2(a.x, b.y), b, ImGui::GetColorU32(c));
            }
        }
        if (rows) ImGui::Separator();
    }
    if (rows) {  // three columns: the name or value, the command's arguments, the help in grey
        float name_w = 0.0f, hint_w = 0.0f;
        for (const auto& r : s.rows) {
            name_w = (std::max)(name_w, ImGui::CalcTextSize(r.label.c_str()).x);
            hint_w = (std::max)(hint_w, ImGui::CalcTextSize(r.hint.c_str()).x);
        }
        const float gap = ImGui::GetFontSize();
        const float x0 = ImGui::GetCursorPosX();
        const float x_hint = x0 + name_w + gap;
        const float x_help = hint_w > 0.0f ? x_hint + hint_w + gap : x_hint;
        for (size_t i = 0; i < s.rows.size(); ++i) {
            const SuggestRow& r = s.rows[i];
            const bool picked = i == g.selected();
            ImGui::SetCursorPosX(x0);
            if (picked) {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = x_help - x0 + ImGui::CalcTextSize(r.help.c_str()).x;
                draw->AddRectFilled(ImVec2(p.x - 2.0f, p.y), ImVec2(p.x + w + 2.0f, p.y + ImGui::GetTextLineHeight()),
                                    ImGui::GetColorU32(kSuggestRowBack));
            }
            text(r.label, picked ? kSuggestPicked : kSuggestName);
            if (!r.hint.empty()) {
                ImGui::SameLine(x_hint);
                text(r.hint, kSuggestHint);
            }
            if (!r.help.empty()) {
                ImGui::SameLine(x_help);
                text(r.help, kSuggestHelp);
            }
        }
    }
    const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowHeight();
    ImGui::End();
    return bottom;
}

std::string common_prefix(const std::vector<std::string>& v) {
    if (v.empty()) return {};
    std::string p = v[0];
    for (const auto& s : v) {
        size_t i = 0;
        while (i < p.size() && i < s.size() && std::tolower(static_cast<unsigned char>(p[i])) ==
                                                    std::tolower(static_cast<unsigned char>(s[i])))
            ++i;
        p.resize(i);
    }
    return p;
}

struct CallbackCtx {
    ConsoleModel* model;
    ConsoleUiState* ui;
    const CompletionSources* sources;
};

void set_buffer(ImGuiInputTextCallbackData* d, const std::string& text) {
    d->DeleteChars(0, d->BufTextLen);
    d->InsertChars(0, text.c_str());
}

void set_line(ImGuiInputTextCallbackData* d, const LineEdit& e) {
    set_buffer(d, e.text);
    d->CursorPos = d->SelectionStart = d->SelectionEnd = static_cast<int>((std::min)(e.caret, e.text.size()));
}

// Called every frame the input line is active (CallbackAlways), or for the frame's one event (Tab, Up/Down, an edit).
int input_callback(ImGuiInputTextCallbackData* d) {
    auto* c = static_cast<CallbackCtx*>(d->UserData);
    ConsoleUiState& ui = *c->ui;
    if (ui.suggest)  // the suggestions follow the line and the caret (they recompute only when either changed)
        ui.suggester.update(std::string_view(d->Buf, static_cast<size_t>(d->BufTextLen)),
                            static_cast<size_t>(d->CursorPos), *c->sources);
    switch (d->EventFlag) {
        case ImGuiInputTextFlags_CallbackCompletion: {
            if (ui.suggest) {
                if (const auto e = ui.suggester.accept(*c->sources)) {  // Tab takes the highlighted suggestion
                    set_line(d, *e);
                    ui.completions.clear();
                    break;
                }
            }
            // no suggestion (or suggestions off): complete and cycle through the matches
            const std::string current(d->Buf, d->BufTextLen);
            const bool cycling = !ui.completions.empty() && ui.completion_index < ui.completions.size() &&
                                 current == ui.completions[ui.completion_index];
            if (cycling) {
                ui.completion_index = (ui.completion_index + 1) % ui.completions.size();
                set_buffer(d, ui.completions[ui.completion_index]);
                break;
            }
            ui.completions = complete(current, *c->sources);
            ui.completion_index = 0;
            if (ui.completions.empty()) break;
            if (ui.completions.size() == 1) {
                set_buffer(d, ui.completions[0] + " ");
                ui.completions.clear();
                break;
            }
            const std::string prefix = common_prefix(ui.completions);
            if (prefix.size() > current.size()) {
                set_buffer(d, prefix);  // extend to what all candidates share; the list stays shown and the next
                                        // Tab (prefix != completions[0]) starts cycling at the first candidate
            } else {
                set_buffer(d, ui.completions[0]);
            }
            break;
        }
        case ImGuiInputTextFlags_CallbackHistory: {
            const bool up = d->EventKey == ImGuiKey_UpArrow;
            if (ui.suggest && ui.suggester.move(up ? -1 : +1)) break;  // in the list while it shows
            const std::string line = up ? c->model->history_prev() : c->model->history_next();
            set_buffer(d, line);
            ui.completions.clear();
            ui.suggester.replaced(line, line.size());  // no list for a line from the history: Up/Down stay its
            break;
        }
        case ImGuiInputTextFlags_CallbackEdit:
            ui.completions.clear();  // typing ends a Tab cycle
            break;
        default:
            if (ui.suggest && ui.accept_right)  // Right at the end of the line takes the suggestion
                if (const auto e = ui.suggester.accept_right(*c->sources)) set_line(d, *e);
            break;
    }
    ui.caret = static_cast<size_t>(d->CursorPos);
    return 0;
}

}  // namespace

void draw_console(ConsoleModel& model, ConsoleUiState& ui, const CompletionSources& sources, ImVec2 display,
                  const std::function<void(const std::string&)>& submit, const std::function<void()>& mods_tab) {
    ui.wants_escape = false;  // set again below while the suggestions show
    if (ui.open_fraction <= 0.0f) return;
    const float height = (std::max)(display.y * 0.5f * (std::min)(ui.open_fraction, 1.0f), 1.0f);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(display.x, height));
    ImGui::SetNextWindowBgAlpha(0.80f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.055f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::Begin("##emf_console", nullptr, flags);

    if (mods_tab) {
        ImGuiIO& tio = ImGui::GetIO();
        if (tio.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) ui.switch_tab = true;
        const int want = ui.switch_tab ? 1 - ui.tab : -1;
        ui.switch_tab = false;
        if (ImGui::BeginTabBar("##emf_tabs")) {
            if (ImGui::BeginTabItem("Console", nullptr, want == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (ui.tab != 0) ui.focus_input = true;
                ui.tab = 0;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Mods", nullptr, want == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                ui.tab = 1;
                ImGui::EndTabItem();
            }
            if (!ui.status.empty())  // a label on the right, not a tab (a click does nothing)
                ImGui::TabItemButton(ui.status.c_str(), ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip);
            ImGui::EndTabBar();
        }
        if (ui.tab == 1) {
            ui.escape = false;
            mods_tab();
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            return;
        }
    }

    // Scrollback.
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##scrollback", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    ImGuiIO& io = ImGui::GetIO();
    // the wheel scrolls the scrollback wherever the cursor is (ImGui scrolls it itself when the cursor is over it)
    if (io.MouseWheel != 0.0f && !ImGui::IsWindowHovered()) ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * line_h * 3.0f);
    const float page = ImGui::GetWindowHeight() * 0.9f;
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) ImGui::SetScrollY(ImGui::GetScrollY() - page);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) ImGui::SetScrollY(ImGui::GetScrollY() + page);
    const size_t count = model.size();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(count), line_h);
    while (clipper.Step()) {
        const auto lines = model.snapshot(static_cast<size_t>(clipper.DisplayStart),
                                          static_cast<size_t>(clipper.DisplayEnd - clipper.DisplayStart));
        for (const auto& [kind, text] : lines) {
            ImGui::PushStyleColor(ImGuiCol_Text, colour(kind));
            ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
            ImGui::PopStyleColor();
        }
    }
    const uint64_t version = model.version();
    if (ImGui::GetScrollY() < ImGui::GetScrollMaxY() - line_h) {
        if (io.MouseWheel > 0.0f || ImGui::IsKeyPressed(ImGuiKey_PageUp)) ui.follow = false;
    } else {
        ui.follow = true;
    }
    const float view_h = ImGui::GetWindowHeight();  // a resize moves the bottom too
    if (ui.follow && (version != ui.seen_version || view_h != ui.seen_height)) ImGui::SetScrollHereY(1.0f);
    ui.seen_version = version;
    ui.seen_height = view_h;
    ImGui::EndChild();

    // Input line.
    if (ui.escape) {  // Esc while the suggestions showed: they hide (the next Esc closes the console)
        ui.escape = false;
        ui.suggester.escape();
    }
    char buf[kInputCap];
    const size_t n = (std::min)(ui.input.size(), kInputCap - 1);
    std::memcpy(buf, ui.input.data(), n);
    buf[n] = '\0';
    ImGui::PushItemWidth(-1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 0.07f));
    if (ui.focus_input) {
        ImGui::SetKeyboardFocusHere();
        ui.focus_input = false;
    }
    CallbackCtx ctx{&model, &ui, &sources};
    ImGuiInputTextFlags in_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
                                   ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackEdit;
    if (ui.suggest) in_flags |= ImGuiInputTextFlags_CallbackAlways;  // the caret, every frame the line is active
    // A new press of Right with the caret at the end of the line (last frame's caret: the press cannot move it
    // further). Not its auto-repeats: holding Right to walk to the end of the line must not take a row there.
    ui.accept_right = ui.suggest && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && !io.KeyCtrl && !io.KeyShift &&
                      !io.KeyAlt && ui.caret == ui.input.size();
    const bool entered = ImGui::InputText("##input", buf, kInputCap, in_flags, input_callback, &ctx);
    ui.accept_right = false;
    ImGui::PopStyleColor();
    ImGui::PopItemWidth();
    ui.input = buf;
    if (ui.suggest && !entered && ImGui::IsItemActive()) {  // grey ghost text completes the top match after the caret
        const std::string ghost = ui.suggester.ghost();
        if (!ghost.empty()) {
            const ImGuiStyle& style = ImGui::GetStyle();
            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            const float x = lo.x + style.FramePadding.x + ImGui::CalcTextSize(ui.input.c_str()).x;
            if (x + ImGui::CalcTextSize(ghost.c_str()).x <= hi.x - style.FramePadding.x)  // only while nothing scrolls
                ImGui::GetWindowDrawList()->AddText(ImVec2(x, lo.y + style.FramePadding.y), ImGui::GetColorU32(kGhost),
                                                    ghost.c_str());
        }
    }
    if (entered) {
        std::string line = ui.input;
        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (!line.empty()) {
            model.push_history(line);
            submit(line);
        }
        ui.input.clear();
        ui.completions.clear();
        ui.suggester.clear();
        ui.caret = 0;
        ui.follow = true;
        ui.focus_input = true;
    }
    if (!entered && !ImGui::IsAnyItemActive())
        ui.focus_input = true;  // keep typing going to the input line while the console is open (also after a click)
    const ImVec2 under = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Suggestions as you type under the panel; the next Esc is theirs while rows show.
    float below = under.y;
    if (ui.suggest) {
        below = draw_suggestions(ui.suggester, under, display);
        ui.wants_escape = ui.suggester.showing();
    }
    // The Tab cycle's list under the panel (and under the suggestions).
    if (ui.completions.size() > 1) {
        ImGui::SetNextWindowPos(ImVec2(under.x, below > under.y ? below + 2.0f : under.y));
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::Begin("##emf_completions", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        for (size_t i = 0; i < ui.completions.size(); ++i) {
            const bool current = i == ui.completion_index;
            ImGui::PushStyleColor(ImGuiCol_Text, current ? ImVec4(1, 1, 1, 1) : ImVec4(0.70f, 0.73f, 0.76f, 1));
            ImGui::TextUnformatted(ui.completions[i].c_str());
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }
}

ConsoleLines toast_lines(const ConsoleModel& model, const ToastState& toast, uint64_t now_ms, size_t max) {
    if (now_ms >= toast.until_ms) return {};
    const uint64_t version = model.version();
    if (version <= toast.from_version) return {};
    const uint64_t fresh = version - toast.from_version;
    const size_t size = model.size();
    if (fresh <= max && fresh <= size) return model.snapshot(size - static_cast<size_t>(fresh), static_cast<size_t>(fresh));
    if (max < 2) return model.snapshot(size - (std::min)(size, max), (std::min)(size, max));
    // More than fits: the newest lines, after a note saying how many are left out (e.g. a long /help).
    const size_t keep = (std::min)(max - 1, size);
    ConsoleLines out{{LineKind::Normal, "... " + std::to_string(fresh - keep) + " more lines in the console"}};
    auto tail = model.snapshot(size - keep, keep);
    out.insert(out.end(), std::make_move_iterator(tail.begin()), std::make_move_iterator(tail.end()));
    return out;
}

void draw_toast(const ConsoleLines& lines, ImVec2 display) {
    if (lines.empty()) return;
    const float pad = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(pad, pad));
    ImGui::SetNextWindowBgAlpha(0.80f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.055f, 1.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##emf_toast", nullptr, flags);
    ImGui::PushTextWrapPos(display.x * 0.6f);
    for (const auto& [kind, text] : lines) {
        ImGui::PushStyleColor(ImGuiCol_Text, colour(kind));
        ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
    ImGui::PopStyleColor();
}

void draw_hud(const std::vector<HudLine>& lines, ImVec2 display) {
    if (lines.empty()) return;
    const float pad = ImGui::GetFontSize();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    static const char* const kNames[] = {"##emf_hud_top_right", "##emf_hud_bottom_right", "##emf_hud_bottom_left"};
    for (int place = 0; place < 3; ++place) {
        bool any = false;
        for (const auto& l : lines) any |= static_cast<int>(l.place) == place;
        if (!any) continue;
        // the game's own HUD keeps the top left, the top centre and the bottom centre; its quest tracker sits in the
        // top right (seen in game), so top-right lines start below it
        const ImVec2 pos = place == 0 ? ImVec2(display.x - pad, display.y * 0.24f)
                         : place == 1 ? ImVec2(display.x - pad, display.y - pad)
                                      : ImVec2(pad, display.y - pad);
        const ImVec2 pivot = place == 0 ? ImVec2(1.0f, 0.0f) : place == 1 ? ImVec2(1.0f, 1.0f) : ImVec2(0.0f, 1.0f);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.055f, 1.0f));
        ImGui::Begin(kNames[place], nullptr, flags);
        for (const auto& l : lines) {
            if (static_cast<int>(l.place) != place) continue;
            const ImVec4 c = l.rgba ? ImVec4(((l.rgba >> 24) & 0xFF) / 255.0f, ((l.rgba >> 16) & 0xFF) / 255.0f,
                                             ((l.rgba >> 8) & 0xFF) / 255.0f, (l.rgba & 0xFF) / 255.0f)
                                    : ImVec4(0.86f, 0.86f, 0.80f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::TextUnformatted(l.text.c_str(), l.text.c_str() + l.text.size());
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
}

}  // namespace emf::core
