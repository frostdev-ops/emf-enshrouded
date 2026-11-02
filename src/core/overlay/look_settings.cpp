#include "look_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace emf::core {
namespace {

using Get = std::function<std::string(const char* section, const char* key)>;

// The value without a "; comment" after it and without the spaces and tabs around it.
std::string clean(std::string v) {
    v = v.substr(0, v.find(';'));
    const size_t first = v.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return v.substr(first, v.find_last_not_of(" \t") - first + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Reader {
    const Get& get;
    std::vector<std::string>* problems;

    void problem(const char* section, const char* key, const std::string& value, const std::string& why) const {
        if (problems) problems->push_back("[" + std::string(section) + "] " + key + "=" + value + ": " + why);
    }

    // One of `names` (any case) -> its index; "" -> -1 quietly; anything else -> -1 with a problem line.
    int choice(const char* section, const char* key, const std::vector<std::vector<const char*>>& names,
               const char* fallback) const {
        const std::string v = clean(get(section, key));
        if (v.empty()) return -1;
        const std::string l = lower(v);
        for (size_t i = 0; i < names.size(); ++i)
            for (const char* n : names[i])
                if (l == n) return static_cast<int>(i);
        std::string all;
        for (const auto& n : names) all += std::string(all.empty() ? "" : ", ") + n[0];
        problem(section, key, v, std::string("not one of ") + all + "; the default " + fallback + " is used");
        return -1;
    }

    void number(const char* section, const char* key, int lo, int hi, int& out) const {
        const std::string v = clean(get(section, key));
        if (v.empty()) return;
        std::string digits = v;
        if (!digits.empty() && digits.back() == '%') digits.pop_back();
        char* end = nullptr;
        const long n = digits.empty() ? 0 : std::strtol(digits.c_str(), &end, 10);
        const std::string range = std::to_string(lo) + " to " + std::to_string(hi);
        if (digits.empty() || !end || *end || std::isspace(static_cast<unsigned char>(digits[0]))) {
            problem(section, key, v, "not a whole number from " + range + "; the default " + std::to_string(out) +
                                         " is used");
            return;
        }
        if (n < lo || n > hi) {
            const int kept = n < lo ? lo : hi;
            problem(section, key, v, "outside " + range + "; " + std::to_string(kept) + " is used (not the default)");
            out = kept;
            return;
        }
        out = static_cast<int>(n);
    }

    void boolean(const char* section, const char* key, bool& out) const {
        const int i = choice(section, key, {{"0", "off", "false", "no"}, {"1", "on", "true", "yes"}},
                             out ? "1 (on)" : "0 (off)");
        if (i >= 0) out = i == 1;
    }

    static int hex_digit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    // #rgb or #rrggbb (the # may be left out) -> 0xRRGGBBFF.
    void colour(const char* section, const char* key, uint32_t& out) const {
        const std::string v = clean(get(section, key));
        if (v.empty()) return;
        const std::string h = v[0] == '#' ? v.substr(1) : v;
        uint32_t rgb = 0;
        bool ok = h.size() == 3 || h.size() == 6;
        for (char c : h) ok = ok && hex_digit(c) >= 0;
        if (ok && h.size() == 3) {
            for (char c : h) rgb = (rgb << 8) | static_cast<uint32_t>(hex_digit(c) * 17);
        } else if (ok) {
            for (char c : h) rgb = (rgb << 4) | static_cast<uint32_t>(hex_digit(c));
        }
        if (!ok) {
            char def[8];
            std::snprintf(def, sizeof(def), "#%06x", static_cast<unsigned>(out >> 8));
            problem(section, key, v, std::string("not a colour like #fffff4 or #fff; the default ") + def + " is used");
            return;
        }
        out = (rgb << 8) | 0xFFu;
    }

    // serif / console / consolas / cascadia (those `allowed`) or file:<name>.ttf|.otf, a plain name in emf\fonts.
    void font(const char* section, const char* key, const std::vector<FontChoice::Kind>& allowed, FontChoice& out) const {
        static const char* const kNames[] = {"serif", "console", "consolas", "cascadia"};
        const std::string v = clean(get(section, key));
        if (v.empty()) return;
        const std::string l = lower(v);
        std::string names;
        for (FontChoice::Kind k : allowed) {
            names += std::string(kNames[static_cast<int>(k)]) + ", ";
            if (l == kNames[static_cast<int>(k)]) {
                out = FontChoice{k, {}};
                return;
            }
        }
        const char* fallback = kNames[static_cast<int>(out.kind)];
        if (l.rfind("file:", 0) == 0) {
            const std::string name = clean(v.substr(5));
            const std::string ext = name.size() > 4 ? lower(name.substr(name.size() - 4)) : std::string();
            const bool plain = !name.empty() && name.size() <= 120 && name[0] != '.' &&
                               name.find_first_of("\\/:*?\"<>|") == std::string::npos;
            if (plain && (ext == ".ttf" || ext == ".otf")) {
                out = FontChoice{FontChoice::Kind::File, name};
                return;
            }
            problem(section, key, v,
                    std::string("file: needs the name of a .ttf or .otf file in emf\\fonts (no folders); the default ") +
                        fallback + " is used");
            return;
        }
        problem(section, key, v, "not one of " + names + "file:<name>.ttf; the default " + fallback + " is used");
    }
};

}  // namespace

LookSettings read_look_settings(const std::function<std::string(const char* section, const char* key)>& get,
                                std::vector<std::string>* problems) {
    LookSettings s;
    const Reader r{get, problems};
    using K = FontChoice::Kind;
    if (const int i = r.choice("hud", "style", {{"game"}, {"boxed"}, {"plain"}}, "game"); i >= 0)
        s.style = static_cast<HudStyle>(i);
    r.font("hud", "font", {K::Serif, K::Console}, s.hud_font);
    r.number("hud", "size", 60, 200, s.hud_size);
    r.colour("hud", "colour", s.hud_colour);
    r.boolean("hud", "shadow", s.shadow);
    if (const int i = r.choice("hud", "notes_place", {{"top_centre", "top_center"}, {"top_left"}, {"right"}},
                               "top_centre");
        i >= 0)
        s.notes_place = static_cast<NotesPlace>(i);
    r.number("hud", "notes_seconds", 2, 20, s.notes_seconds);
    r.font("overlay", "font", {K::Consolas, K::Cascadia, K::Serif}, s.console_font);
    r.font("overlay", "tabs_font", {K::Serif, K::Consolas, K::Cascadia}, s.tabs_font);
    r.number("overlay", "text_size", 60, 200, s.text_size);
    r.number("overlay", "height", 25, 90, s.height);
    r.number("overlay", "opacity", 30, 100, s.opacity);
    r.colour("overlay", "background", s.background);
    r.colour("overlay", "accent", s.accent);
    return s;
}

}  // namespace emf::core
