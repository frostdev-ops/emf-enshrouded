#pragma once
// Suggestions while typing in the in-game console, free of ImGui: the commands that match what is typed (those that
// start with it first, then those that hold it: "reload" finds lua.reload), each with its argument hint and help; the
// values the argument being typed takes (item names, components, mod ids, setting keys and values, "a|b" choices);
// grey ghost text that completes the top match; the argument hint line with the argument being typed; and the keys
// that move in, take from or hide the list (Up/Down, Tab/Right, Esc).
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "console_model.h"
#include "core/manifest.h"

namespace emf::core {

// A command's help, "<args>: what it does", in two parts (split at the first ':' outside brackets that ends the help
// or is followed by a space). Without one, a help that starts with '<' or '[' is all arguments, any other all text.
struct HelpParts {
    std::string args;  // "<item name> [count]"
    std::string text;  // "drop items at your feet (host)"
};
HelpParts split_help(std::string_view help);

// One word of an argument hint: an argument ("<item name>", "[count]", "[on|off]"), a lone "|" between two ways to
// call the command (goto's "<x> <y> <z> | <entity>"), or other text ("(host)").
struct HintToken {
    enum class Kind : uint8_t { Arg, Or, Text };
    Kind kind = Kind::Text;
    std::string text;                  // as written: "[on|off]"
    std::string name;                  // an argument's inside: "on|off"
    bool optional = false;             // [...]
    std::vector<std::string> choices;  // the "a|b" alternatives that are words to type as they are ("on", "off"; the
                                       // first word of "save <name>"), not stand-ins such as count, name or #k
};
std::vector<HintToken> parse_hint(std::string_view args);

struct SuggestRow {
    std::string label;   // a command's name or a value
    std::string hint;    // a command's arguments ("<item name> [count]"); "" for a value
    std::string help;    // grey: what it does or means
    std::string insert;  // what replaces the typed part when the row is taken
};

// A piece of the argument hint line: "give", " ", "<item name>" (the argument being typed: Current), " ", "[count]",
// ": drop items at your feet (host)".
struct HintPiece {
    enum class Kind : uint8_t { Command, Arg, Current, Text, Help };
    Kind kind = Kind::Text;
    std::string text;
};

struct Suggestions {
    std::vector<SuggestRow> rows;
    size_t from = 0, to = 0;      // the bytes of the line a row replaces: the word (or the words of an item name or of
                                  // the last argument) being typed, to the end of the word at the caret
    std::string typed;            // what was typed of it, up to the caret
    std::vector<HintPiece> hint;  // the argument hint line; empty until a known command and a space are typed
};

// What the input line becomes when a row is taken: the row's text with a space after it, the caret after the space.
struct LineEdit {
    std::string text;
    size_t caret = 0;
};

// The suggestions for `line` with the caret at byte `caret`.
// - The first word: the commands that start with it (any case), then those that hold it, each with its help split
//   into hint and text.
// - After a known command and a space: the hint line, and values for the argument being typed. An argument named
//   like "<item name>" takes item names (several words, up to a plain number: "give wood log 5"), found by the
//   rules of `items` (item_names.h), best match first, once something is typed; "<component>" takes components
//   (once something is typed); "<mod>" mod ids; set's "<key>" the mod's keys and enabled, its "<value>" what the
//   setting takes; an argument with "a|b" alternatives those choices. The last argument takes the rest of the line;
//   an optional "[#k]" is passed by a word without a '#'. One row that is exactly what was typed is no suggestion.
// At most max_rows rows.
Suggestions suggest(std::string_view line, size_t caret, const CompletionSources& src, size_t max_rows = 8);
LineEdit take_row(std::string_view line, const Suggestions& s, size_t row);
// The rest of row `row` after what was typed (grey after the caret): only with the caret at the end of the line and
// a row that starts with what was typed (any case); else "".
std::string ghost_of(std::string_view line, size_t caret, const Suggestions& s, size_t row);

// What `set` offers for a setting: its label, and on/off for a switch, the choices of a list, the options of a
// select (with their labels).
SettingSource setting_source(const SettingDef& def);

// The list's state while the player types (render thread). The list shows while there are rows, until Esc or a line
// from the history hides it; typing shows it again. Up/Down move in it only while it shows (else they are the
// history's), Tab takes the selected row only while it shows (else the console's Tab completes as before), Right only
// while it shows and the caret is at the end.
class Suggester {
public:
    static constexpr size_t kMinRows = 3, kMaxRows = 12;
    void set_rows(size_t rows);  // kept within 3-12
    size_t rows() const { return rows_; }
    // The input line and caret as they are now. Recomputes only when they or the sources' version changed; changed
    // text shows a hidden list again.
    void update(std::string_view line, size_t caret, const CompletionSources& src);
    const Suggestions& current() const { return now_; }
    bool showing() const { return !hidden_ && !now_.rows.empty(); }
    size_t selected() const { return selected_; }
    std::string ghost() const;  // "" unless the list shows
    // Up (-1) / Down (+1): true when it moved in the list (wrapping around); false when no list shows (the history's).
    bool move(int delta);
    // The line was set from the history: no list until the next edit.
    void replaced(std::string_view line, size_t caret);
    // True when it hid the list; false when none showed (Esc closes the console then).
    bool escape();
    // Tab and Right: the line with the selected row taken; nothing while no list shows. The suggestions for that line
    // are worked out at once (from `src`), so the list, the ghost and showing() fit the new line in the same frame.
    std::optional<LineEdit> accept(const CompletionSources& src);        // Tab
    std::optional<LineEdit> accept_right(const CompletionSources& src);  // Right
    void clear();                                                        // the line was sent
    uint64_t computed() const { return computed_; }  // how often the suggestions were worked out (tests)

private:
    void compute(const CompletionSources& src);

    size_t rows_ = 8;
    std::string text_;
    size_t caret_ = 0;
    uint64_t version_ = 0;
    bool stale_ = true;    // recompute at the next update
    bool hidden_ = false;  // Esc or a history line hid the list
    size_t selected_ = 0;
    Suggestions now_;
    uint64_t computed_ = 0;
};

}  // namespace emf::core
