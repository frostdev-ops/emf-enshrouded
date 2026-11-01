#include "console_suggest.h"

#include <algorithm>
#include <functional>

#include "core/item_names.h"

namespace emf::core {
namespace {

char low(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = low(c);
    return out;
}

bool istarts(std::string_view s, std::string_view p) {
    if (p.size() > s.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
        if (low(s[i]) != low(p[i])) return false;
    return true;
}

bool iequal(std::string_view a, std::string_view b) { return a.size() == b.size() && istarts(a, b); }

std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

// A count or other number of the command's, not part of a name: digits without a leading zero (01 belongs to a name).
bool plain_number(std::string_view w) {
    if (w.empty() || (w.size() > 1 && w[0] == '0')) return false;
    return std::all_of(w.begin(), w.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// How a name fits what was typed: it starts with it (any case), holds it, or neither.
enum class Fit { None, Inside, Start };

Fit fit(std::string_view name, std::string_view typed) {
    if (istarts(name, typed)) return Fit::Start;
    if (!typed.empty() && lower(name).find(lower(typed)) != std::string::npos) return Fit::Inside;
    return Fit::None;
}

// The indexes 0..n-1 whose fit_of is Start, then those whose fit_of is Inside, each in their order.
std::vector<size_t> rank(size_t n, const std::function<Fit(size_t)>& fit_of) {
    std::vector<size_t> start, inside;
    for (size_t i = 0; i < n; ++i) {
        const Fit f = fit_of(i);
        if (f == Fit::Start) start.push_back(i);
        else if (f == Fit::Inside) inside.push_back(i);
    }
    start.insert(start.end(), inside.begin(), inside.end());
    return start;
}

// Words that stand for something to type, not words to type as they are, in "a|b" alternatives.
bool stand_in(const std::string& w) {
    static const char* const kWords[] = {"count", "name",  "entity", "item",   "number", "n",      "k",     "m",
                                         "id",    "value", "text",   "player", "amount", "metres", "meters",
                                         "radius", "x",    "y",      "z",      "guid",   "words",  "word",  "hours",
                                         "setting", "mod", "key",    "page",   "label",  "villager", "chest",
                                         "species", "offset", "bytes", "part"};
    return std::any_of(std::begin(kWords), std::end(kWords), [&](const char* k) { return w == k; });
}

bool literal_word(std::string_view w) {
    if (w.empty() || w[0] == '#') return false;
    for (char c : w)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.'))
            return false;
    return !stand_in(lower(w));
}

// The first word of each "a|b" alternative (split outside inner brackets) that is a word to type as it is.
std::vector<std::string> literal_choices(std::string_view name) {
    std::vector<std::string_view> alts;
    int depth = 0;
    size_t from = 0;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '<' || c == '[') ++depth;
        else if ((c == '>' || c == ']') && depth > 0) --depth;
        else if (c == '|' && depth == 0) {
            alts.push_back(name.substr(from, i - from));
            from = i + 1;
        }
    }
    alts.push_back(name.substr(from));
    std::vector<std::string> out;
    if (alts.size() < 2) return out;
    for (std::string_view a : alts) {
        a = trim(a);
        const std::string w(a.substr(0, a.find(' ')));
        if (literal_word(w) && std::find(out.begin(), out.end(), w) == out.end()) out.push_back(w);
    }
    return out;
}

bool item_arg(const HintToken& t) { return lower(t.name).find("item") != std::string::npos; }
bool hash_arg(const HintToken& t) { return t.optional && !t.name.empty() && t.name[0] == '#'; }

struct Word {
    size_t start = 0, end = 0;  // bytes of the line
};

// The words of line[0, caret), the last being the word being typed: the one the caret touches, else an empty word at
// the caret.
std::vector<Word> words_to(std::string_view line, size_t caret) {
    std::vector<Word> out;
    size_t i = 0;
    while (i < caret) {
        while (i < caret && line[i] == ' ') ++i;
        if (i >= caret) break;
        size_t j = i;
        while (j < caret && line[j] != ' ') ++j;
        out.push_back({i, j});
        i = j;
    }
    if (out.empty() || out.back().end < caret) out.push_back({caret, caret});
    return out;
}

// The argument (an index into `args`) the word being typed fills, and the word that argument starts at.
struct Located {
    int arg = -1;
    size_t first = 0;
};

// A choice of argument `t` starts with `w` (any case).
bool starts_choice(const HintToken& t, std::string_view w) {
    return std::any_of(t.choices.begin(), t.choices.end(), [&](const std::string& c) { return istarts(c, w); });
}

// Walks the words after the command (words[1..]) through one way of calling it: each word starts the next argument,
// except that an item name goes on over words that are not plain numbers (and over a space while what was typed
// names no one item yet: resolves(first, end) says whether words [first, end) do), unless the words so far name one
// item and the word starts a choice of the next argument ("torch a": all); the last argument takes the rest of the
// line, and an optional "[#k]" is passed by a word without a '#'.
Located locate(const std::vector<HintToken>& tokens, const std::vector<size_t>& args, const std::vector<Word>& words,
               std::string_view line, const std::function<bool(size_t, size_t)>& resolves) {
    if (args.empty() || words.size() < 2) return {};
    const auto text_of = [&](size_t i) { return line.substr(words[i].start, words[i].end - words[i].start); };
    size_t a = 0, first = 1;
    const auto pass_hash = [&](size_t i) {
        const std::string_view t = text_of(i);
        while (a + 1 < args.size() && hash_arg(tokens[args[a]]) && !t.empty() && t[0] != '#') ++a;
    };
    pass_hash(1);
    for (size_t i = 2; i < words.size(); ++i) {
        bool same = a + 1 >= args.size();
        if (!same && item_arg(tokens[args[a]])) {
            const std::string_view t = text_of(i);
            if (t.empty()) same = !resolves(first, i);
            else if (plain_number(t)) same = false;
            else same = !(starts_choice(tokens[args[a + 1]], t) && resolves(first, i));
        }
        if (!same) {
            ++a;
            first = i;
            pass_hash(i);
        }
    }
    return {static_cast<int>(a), first};
}

const ModSource* find_mod(const CompletionSources& src, std::string_view id) {
    for (const auto& m : src.mod_settings)
        if (iequal(m.id, id)) return &m;
    return nullptr;
}

// One row that is exactly what was typed suggests nothing.
void drop_exact(Suggestions& s) {
    if (s.rows.size() == 1 && iequal(s.rows[0].insert, trim(s.typed))) s.rows.clear();
}

void value_rows(Suggestions& s, const HintToken& arg, const std::string& cmd, std::string_view mod,
                std::string_view key, const CompletionSources& src, size_t max_rows) {
    const std::string_view typed = trim(s.typed);
    const std::string name = lower(arg.name);
    const auto add = [&](const std::string& label, const std::string& help) {
        if (s.rows.size() < max_rows) s.rows.push_back({label, "", help, label});
    };
    if (item_arg(arg)) {  // thousands of names: only once something is typed, by the item-name rules
        if (!typed.empty())
            for (size_t i : search_items(src.items, typed)) add(src.items[i], "");
    } else if (name.find("component") != std::string::npos) {
        if (!typed.empty())
            for (size_t i : rank(src.components.size(), [&](size_t i) { return fit(src.components[i], typed); }))
                add(src.components[i], "");
    } else if (name == "mod") {
        const auto& mods = src.mod_settings;
        for (size_t i : rank(mods.size(), [&](size_t i) { return fit(mods[i].id, typed); }))
            add(mods[i].id, mods[i].name);
    } else if (cmd == "set" && name == "key") {
        if (const ModSource* m = find_mod(src, mod)) {
            std::vector<SettingSource> keys = m->settings;
            SettingSource enabled("enabled");
            enabled.label = "the mod's switch (from the next game start)";
            keys.push_back(enabled);
            for (size_t i : rank(keys.size(), [&](size_t i) { return fit(keys[i].key, typed); }))
                add(keys[i].key, keys[i].label);
        }
    } else if (cmd == "set" && name == "value") {
        std::vector<std::pair<std::string, std::string>> values;
        if (const ModSource* m = find_mod(src, mod)) {
            if (iequal(key, "enabled")) values = {{"on", ""}, {"off", ""}};
            for (const auto& setting : m->settings)
                if (iequal(setting.key, key)) values = setting.values;
        }
        for (size_t i : rank(values.size(), [&](size_t i) {
                 return (std::max)(fit(values[i].first, typed), fit(values[i].second, typed));
             }))
            add(values[i].first, values[i].second);
    } else if (!arg.choices.empty()) {
        for (size_t i : rank(arg.choices.size(), [&](size_t i) { return fit(arg.choices[i], typed); }))
            add(arg.choices[i], "");
    }
}

}  // namespace

HelpParts split_help(std::string_view help) {
    int depth = 0;
    for (size_t i = 0; i < help.size(); ++i) {
        const char c = help[i];
        if (c == '<' || c == '[') {
            ++depth;
        } else if ((c == '>' || c == ']') && depth > 0) {
            --depth;
        } else if (c == ':' && depth == 0 && (i + 1 == help.size() || help[i + 1] == ' ')) {
            const std::string_view args = trim(help.substr(0, i));
            if (!args.empty() && args[0] != '<' && args[0] != '[') break;  // "shows it: on or off" is all text
            return {std::string(args), std::string(trim(help.substr(i + 1)))};
        }
    }
    const std::string_view t = trim(help);
    if (!t.empty() && (t[0] == '<' || t[0] == '[')) return {std::string(t), ""};
    return {"", std::string(t)};
}

std::vector<HintToken> parse_hint(std::string_view args) {
    std::vector<HintToken> out;
    size_t i = 0;
    while (i < args.size()) {
        while (i < args.size() && args[i] == ' ') ++i;
        if (i >= args.size()) break;
        size_t j = i;
        int depth = 0;
        while (j < args.size() && (depth > 0 || args[j] != ' ')) {
            if (args[j] == '<' || args[j] == '[') ++depth;
            else if ((args[j] == '>' || args[j] == ']') && depth > 0) --depth;
            ++j;
        }
        HintToken t;
        t.text = std::string(args.substr(i, j - i));
        if (t.text == "|") {
            t.kind = HintToken::Kind::Or;
        } else if (t.text[0] == '<' || t.text[0] == '[') {
            t.kind = HintToken::Kind::Arg;
            t.optional = t.text[0] == '[';
            const char close = t.optional ? ']' : '>';
            const size_t end = t.text.size() > 1 && t.text.back() == close ? t.text.size() - 1 : t.text.size();
            t.name = t.text.substr(1, end - 1);
            t.choices = literal_choices(t.name);
        }
        out.push_back(std::move(t));
        i = j;
    }
    return out;
}

Suggestions suggest(std::string_view line, size_t caret, const CompletionSources& src, size_t max_rows) {
    Suggestions s;
    caret = (std::min)(caret, line.size());
    const auto words = words_to(line, caret);
    const auto text_of = [&](size_t i) { return line.substr(words[i].start, words[i].end - words[i].start); };
    s.to = caret;
    while (s.to < line.size() && line[s.to] != ' ') ++s.to;
    s.from = words.back().start;
    s.typed = std::string(line.substr(s.from, caret - s.from));

    if (words.size() == 1) {  // the command
        if (s.typed.empty()) return s;
        for (size_t i : rank(src.commands.size(), [&](size_t i) { return fit(src.commands[i], s.typed); })) {
            if (s.rows.size() >= max_rows) break;
            const HelpParts h = split_help(i < src.command_help.size() ? src.command_help[i] : std::string());
            s.rows.push_back({src.commands[i], h.args, h.text, src.commands[i]});
        }
        drop_exact(s);
        return s;
    }

    const std::string cmd = lower(text_of(0));
    size_t ci = src.commands.size();
    for (size_t i = 0; i < src.commands.size(); ++i)
        if (iequal(src.commands[i], cmd)) ci = i;
    if (ci == src.commands.size()) return s;
    const HelpParts help = split_help(ci < src.command_help.size() ? src.command_help[ci] : std::string());
    const auto tokens = parse_hint(help.args);
    std::vector<std::vector<size_t>> forms(1);  // each way to call it: its arguments (indexes into tokens)
    for (size_t k = 0; k < tokens.size(); ++k) {
        if (tokens[k].kind == HintToken::Kind::Or) forms.emplace_back();
        else if (tokens[k].kind == HintToken::Kind::Arg) forms.back().push_back(k);
    }
    const auto resolves = [&](size_t first, size_t end) {
        const std::string_view t = line.substr(words[first].start, words[end - 1].end - words[first].start);
        return match_item(src.items, t).id != 0;
    };
    std::vector<Located> where;
    for (const auto& f : forms) where.push_back(locate(tokens, f, words, line, resolves));

    s.hint.push_back({HintPiece::Kind::Command, src.commands[ci]});
    size_t form = 0, arg = 0;
    for (const auto& t : tokens) {
        s.hint.push_back({HintPiece::Kind::Text, " "});
        if (t.kind == HintToken::Kind::Arg) {
            const bool current = where[form].arg == static_cast<int>(arg);
            s.hint.push_back({current ? HintPiece::Kind::Current : HintPiece::Kind::Arg, t.text});
            ++arg;
        } else {
            s.hint.push_back({HintPiece::Kind::Text, t.text});
            if (t.kind == HintToken::Kind::Or) {
                ++form;
                arg = 0;
            }
        }
    }
    if (!help.text.empty()) s.hint.push_back({HintPiece::Kind::Help, ": " + help.text});

    const Located& at = where[0];
    if (at.arg < 0) return s;
    s.from = words[at.first].start;
    s.typed = std::string(line.substr(s.from, caret - s.from));
    const std::string_view mod = words.size() > 2 ? text_of(1) : std::string_view();
    const std::string_view key = words.size() > 3 ? text_of(2) : std::string_view();
    value_rows(s, tokens[forms[0][static_cast<size_t>(at.arg)]], cmd, mod, key, src, max_rows);
    drop_exact(s);
    return s;
}

LineEdit take_row(std::string_view line, const Suggestions& s, size_t row) {
    if (row >= s.rows.size() || s.from > line.size()) return {std::string(line), line.size()};
    const std::string& insert = s.rows[row].insert;
    std::string out(line.substr(0, s.from));
    out += insert;
    const std::string_view tail = line.substr((std::min)((std::max)(s.to, s.from), line.size()));
    if (tail.empty()) out += ' ';
    out += tail;
    return {out, s.from + insert.size() + 1};
}

std::string ghost_of(std::string_view line, size_t caret, const Suggestions& s, size_t row) {
    if (caret != line.size() || row >= s.rows.size()) return {};
    const std::string& insert = s.rows[row].insert;
    if (insert.size() <= s.typed.size() || !istarts(insert, s.typed)) return {};
    return insert.substr(s.typed.size());
}

SettingSource setting_source(const SettingDef& def) {
    SettingSource out(def.key);
    out.label = def.label.empty() ? def.key : def.label;
    switch (def.type) {
        case SettingType::Bool: out.values = {{"on", ""}, {"off", ""}}; break;
        case SettingType::Choice:
            for (const auto& c : def.choices) out.values.emplace_back(c, "");
            break;
        case SettingType::Select:
            for (const auto& o : def.options) out.values.emplace_back(o.value, o.label == o.value ? "" : o.label);
            break;
        default: break;
    }
    return out;
}

void Suggester::set_rows(size_t rows) {
    rows_ = std::clamp(rows, kMinRows, kMaxRows);
    stale_ = true;
}

void Suggester::update(std::string_view line, size_t caret, const CompletionSources& src) {
    const bool edited = line != text_;
    if (!edited && caret == caret_ && !stale_ && src.version == version_) return;
    if (edited) {
        text_ = std::string(line);
        hidden_ = false;  // typing shows the list again
    }
    caret_ = (std::min)(caret, text_.size());
    compute(src);
}

void Suggester::compute(const CompletionSources& src) {
    version_ = src.version;
    stale_ = false;
    now_ = suggest(text_, caret_, src, rows_);
    selected_ = 0;
    ++computed_;
}

std::string Suggester::ghost() const { return showing() ? ghost_of(text_, caret_, now_, selected_) : std::string(); }

bool Suggester::move(int delta) {
    if (!showing()) return false;
    const size_t n = now_.rows.size();
    selected_ = delta > 0 ? (selected_ + 1) % n : (selected_ + n - 1) % n;
    return true;
}

void Suggester::replaced(std::string_view line, size_t caret) {
    text_ = std::string(line);
    caret_ = (std::min)(caret, text_.size());
    hidden_ = true;
    stale_ = true;
    selected_ = 0;
}

bool Suggester::escape() {
    if (!showing()) return false;
    hidden_ = true;
    return true;
}

std::optional<LineEdit> Suggester::accept(const CompletionSources& src, size_t max_size) {
    if (!showing()) return std::nullopt;  // hidden: a row the player cannot see is never taken
    LineEdit e = take_row(text_, now_, (std::min)(selected_, now_.rows.size() - 1));
    if (e.text.size() > max_size) return std::nullopt;  // it would not fit the input line: nothing changes
    text_ = e.text;  // the input gets this line: not an edit of the player's, the list stays
    caret_ = e.caret;
    hidden_ = false;
    compute(src);  // at once: the rest of this frame shows the new line's list and ghost, not the old ones
    return e;
}

std::optional<LineEdit> Suggester::accept_right(const CompletionSources& src, size_t max_size) {
    if (caret_ != text_.size()) return std::nullopt;
    return accept(src, max_size);
}

void Suggester::clear() {
    text_.clear();
    caret_ = 0;
    hidden_ = false;
    stale_ = true;
    selected_ = 0;
    now_ = Suggestions();
}

}  // namespace emf::core
