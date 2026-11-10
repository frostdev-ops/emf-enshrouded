#include "hud_commands.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "commands.h"
#include "core/hud_editor/elements.h"
#include "core/hud_editor/layout.h"

namespace emf::core {

using namespace hud;

namespace {

// A number, optionally ending in %, all of it, finite.
bool parse_number(const std::string& text, double* out) {
    std::string t = text;
    if (!t.empty() && t.back() == '%') t.pop_back();
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

std::string bad_number(const std::string& cmd, const std::string& x) { return cmd + ": \"" + x + "\" is not a number"; }

std::string no_part(const std::string& x) { return "hud: no HUD part \"" + x + "\"; the parts: " + element_ids(); }

// The gate's line when an edit is not shown now; for a bar (or the order) also when the bars do not work on this build.
void add_not_shown(const HudEditor& editor, std::vector<std::string>& out, bool bar = false) {
    const HudView v = editor.view();
    if (v.gate.state != GateState::On)
        out.push_back("hud: not shown now: " + v.gate.message);
    else if (bar && !v.bars_work)
        out.push_back("hud: not shown now: " + v.bars_note);
}

bool is_bar(int part) { return part >= 0 && hud_elements()[static_cast<size_t>(part)].bar >= 0; }

// A bar by its id, ignoring case; -1 for anything else.
int find_bar(const std::string& word) {
    for (int b = 0; b < static_cast<int>(kBarCount); ++b) {
        const std::string id = bar_id(b);
        if (word.size() != id.size()) continue;
        bool same = true;
        for (size_t i = 0; i < id.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(word[i])) != id[i]) same = false;
        if (same) return b;
    }
    return -1;
}

std::string order_text(const BarOrder& o) {
    std::string s;
    for (size_t i = 0; i < kBarCount; ++i) s += (i ? ", " : "") + std::string(bar_id(o[i]));
    return s;
}

std::string pos_text(const ElementLayout& e) { return "x " + number_text(e.x) + " %, y " + number_text(e.y) + " %"; }

std::vector<std::string> list_lines(HudEditor& editor) {
    const HudView v = editor.view();
    std::vector<std::string> out;
    out.push_back(v.gate.state == GateState::On ? "hud: on, layout \"" + v.layout_name + "\""
                                                : "hud: off: " + v.gate.message);
    if (v.scale_works)
        out.push_back("hud: scale on (pushes " + std::to_string(v.scale.pushes) + ", pops " + std::to_string(v.scale.pops) +
                      ", mismatches " + std::to_string(v.scale.mismatches) + ")");
    else
        out.push_back("hud: scale off: " + v.scale_note);
    if (!v.bars_work) out.push_back("hud: " + v.bars_note);
    if (v.order != kDefaultOrder) out.push_back("hud: order " + order_text(v.order));
    if (v.buffs_up) out.push_back("hud: status effects grow upward");
    const auto parts = hud_elements();
    for (size_t i = 0; i < kHudElementCount; ++i) {
        const ElementLayout& e = v.elements[i];
        std::string line = std::string("  ") + parts[i].id + " (" + parts[i].scope + "): ";
        if (e.is_default()) {
            line += "where the game draws it";
        } else {
            line += pos_text(e) + ", scale " + number_text(e.scale);
            if (e.hidden) line += ", hidden";
        }
        if (!v.live[i].seen) line += ", not seen yet";
        out.push_back(line);
    }
    std::string layouts = "hud: layouts: ";
    for (size_t i = 0; i < v.layouts.size(); ++i) {
        if (i) layouts += ", ";
        layouts += v.layouts[i];
        if (v.layouts[i] == v.layout_name) layouts += " (active)";
    }
    out.push_back(layouts);
    return out;
}

std::string joined(const std::vector<std::string>& argv, size_t from) {
    std::string s;
    for (size_t i = from; i < argv.size(); ++i) s += (i > from ? " " : "") + argv[i];
    return s;
}

}  // namespace

std::vector<std::string> run_hud_command(HudEditor& editor, const std::vector<std::string>& argv) {
    std::vector<std::string> out;
    if (argv.empty()) return out;
    const std::string& cmd = argv[0];
    const size_t n = argv.size();
    if (cmd == "hud.list") return list_lines(editor);

    if (cmd == "hud.move") {
        if (n != 4) return {"usage: hud.move <part> <x> <y> (percent of the screen, -100 to 100)"};
        const int part = find_element(argv[1]);
        if (part == kNoElement) return {no_part(argv[1])};
        double x = 0, y = 0;
        if (!parse_number(argv[2], &x)) return {bad_number(cmd, argv[2])};
        if (!parse_number(argv[3], &y)) return {bad_number(cmd, argv[3])};
        editor.set_offset(static_cast<size_t>(part), clamp_offset(x), clamp_offset(y), Save::Now);
        const HudView v = editor.view();
        std::string line = std::string("hud: ") + hud_elements()[static_cast<size_t>(part)].id + " at " +
                           pos_text(v.elements[static_cast<size_t>(part)]) + " (layout \"" + v.layout_name + "\")";
        const bool clamped = x < kOffsetMin || x > kOffsetMax || y < kOffsetMin || y > kOffsetMax;
        if (clamped) line += " (kept within -100 to 100)";
        out.push_back(line);
        add_not_shown(editor, out, is_bar(part));
        return out;
    }

    if (cmd == "hud.scale") {
        if (n != 3) return {"usage: hud.scale <part> <0.5-2>"};
        const int part = find_element(argv[1]);
        if (part == kNoElement) return {no_part(argv[1])};
        if (is_bar(part))
            return {std::string("hud: ") + hud_elements()[static_cast<size_t>(part)].id +
                    " has no scale of its own; scale vitals instead"};
        double s = 0;
        if (!parse_number(argv[2], &s)) return {bad_number(cmd, argv[2])};
        editor.set_scale(static_cast<size_t>(part), clamp_scale(s), Save::Now);
        const HudView v = editor.view();
        std::string line = std::string("hud: ") + hud_elements()[static_cast<size_t>(part)].id + " scale " +
                           number_text(v.elements[static_cast<size_t>(part)].scale) + " (layout \"" + v.layout_name + "\")";
        if (s < kScaleMin || s > kScaleMax) line += " (kept within 0.5 to 2)";
        if (!v.scale_works) line += " (kept; " + v.scale_note + ")";
        out.push_back(line);
        add_not_shown(editor, out);
        return out;
    }

    if (cmd == "hud.hide" || cmd == "hud.show") {
        const bool hide = cmd == "hud.hide";
        if (n != 2) return {std::string("usage: ") + cmd + " <part>"};
        const int part = find_element(argv[1]);
        if (part == kNoElement) return {no_part(argv[1])};
        editor.set_hidden(static_cast<size_t>(part), hide, Save::Now);
        const std::string id = hud_elements()[static_cast<size_t>(part)].id;
        out.push_back(hide ? "hud: " + id + " hidden (hud.show " + id + " brings it back)" : "hud: " + id + " shown");
        add_not_shown(editor, out, is_bar(part));
        return out;
    }

    if (cmd == "hud.reset") {
        if (n == 1 || (n == 2 && (argv[1] == "all" || argv[1] == "ALL"))) {
            out.push_back("hud: " + editor.reset_all().message);
            return out;
        }
        if (n != 2) return {"usage: hud.reset [part]"};
        const int part = find_element(argv[1]);
        if (part == kNoElement) return {no_part(argv[1])};
        editor.reset(static_cast<size_t>(part), Save::Now);
        out.push_back(std::string("hud: ") + hud_elements()[static_cast<size_t>(part)].id + " is back where the game draws it");
        add_not_shown(editor, out);
        return out;
    }

    if (cmd == "hud.layout") {
        const char* usage = "usage: hud.layout [save|load|delete <name>]";
        if (n == 1) {
            const auto lines = list_lines(editor);
            return {lines.back()};
        }
        const std::string& sub = argv[1];
        const std::string name = joined(argv, 2);
        if (name.empty() || (sub != "save" && sub != "load" && sub != "delete")) return {usage};
        const LayoutResult r = sub == "save" ? editor.save_as(name) : sub == "load" ? editor.load(name) : editor.remove(name);
        return {"hud: " + r.message};
    }

    if (cmd == "hud.swap") {
        const int a = n == 3 ? find_bar(argv[1]) : -1, b = n == 3 ? find_bar(argv[2]) : -1;
        if (a < 0 || b < 0 || a == b) return {"usage: hud.swap <bar> <bar> (two of health, mana, stamina)"};
        editor.swap_bars(a, b, Save::Now);
        const HudView v = editor.view();
        out.push_back("hud: order " + order_text(v.order) + " (layout \"" + v.layout_name + "\")");
        add_not_shown(editor, out, true);
        return out;
    }

    if (cmd == "hud.order") {
        BarOrder o{};
        bool ok = n == 1 + kBarCount;
        for (size_t i = 0; ok && i < kBarCount; ++i) {
            const int b = find_bar(argv[1 + i]);
            ok = b >= 0;
            if (ok) o[i] = static_cast<uint8_t>(b);
        }
        if (!ok || !valid_order(o)) return {"usage: hud.order <bar> <bar> <bar> (health, mana and stamina once each, top first)"};
        editor.set_order(o, Save::Now);
        const HudView v = editor.view();
        out.push_back("hud: order " + order_text(v.order) + " (layout \"" + v.layout_name + "\")");
        add_not_shown(editor, out, true);
        return out;
    }

    if (cmd == "hud.grow") {
        if (n != 2 || (argv[1] != "up" && argv[1] != "down")) return {"usage: hud.grow up|down"};
        const bool up = argv[1] == "up";
        editor.set_buffs_up(up, Save::Now);
        const HudView v = editor.view();
        out.push_back(std::string(up ? "hud: status effects grow upward" : "hud: status effects grow downward, as the game draws them") +
                      " (layout \"" + v.layout_name + "\")");
        add_not_shown(editor, out);
        return out;
    }
    return {"hud: unknown command " + cmd};
}

namespace {

void run_command(int argc, const char* const* argv, void* user) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    for (const auto& line : run_hud_command(*static_cast<HudEditor*>(user), args)) command_reply_line(line);
}
void cmd_list(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }
void cmd_move(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }
void cmd_scale(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }
void cmd_hide(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }
void cmd_show(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }
void cmd_reset(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }
void cmd_layout(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }
void cmd_swap(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }
void cmd_order(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }
void cmd_grow(EmfWorld*, int argc, const char* const* argv, void* user) { run_command(argc, argv, user); }

}  // namespace

bool add_hud_commands(CommandCenter& center, HudEditor& editor) {
    bool ok = true;
    ok &= center.add("hud.list", ": the HUD editor's state, every HUD part with its offset, scale and whether it is hidden, and the layouts", cmd_list, &editor, nullptr, true);
    ok &= center.add("hud.move", "<part> <x> <y>: move a HUD part, in percent of the screen from where the game draws it (e.g. hud.move vitals 5 -2.5)", cmd_move, &editor, nullptr, true);
    ok &= center.add("hud.scale", "<part> <0.5-2>: a HUD part's size, around the screen edge it is nearest (e.g. hud.scale hotbar 0.75)", cmd_scale, &editor, nullptr, true);
    ok &= center.add("hud.hide", "<part>: hide a HUD part (hud.show brings it back)", cmd_hide, &editor, nullptr, true);
    ok &= center.add("hud.show", "<part>: show a hidden HUD part again", cmd_show, &editor, nullptr, true);
    ok &= center.add("hud.reset", "[part]: put one HUD part, or every part, back where the game draws it", cmd_reset, &editor, nullptr, true);
    ok &= center.add("hud.layout", "[save|load|delete <name>]: the HUD layouts in emf\\hud (save as a new name, switch, delete); without words, the list", cmd_layout, &editor, nullptr, true);
    ok &= center.add("hud.swap", "health|mana|stamina health|mana|stamina: two of the vitals' bars change places (e.g. hud.swap health mana: mana on top)", cmd_swap, &editor, nullptr, true);
    ok &= center.add("hud.order", "<bar> <bar> <bar>: the vitals' bars from the top (e.g. hud.order mana health stamina)", cmd_order, &editor, nullptr, true);
    ok &= center.add("hud.grow", "up|down: the status effects list grows upward from the food icons' bottom edge, or down as the game draws it", cmd_grow, &editor, nullptr, true);
    return ok;
}

}  // namespace emf::core
