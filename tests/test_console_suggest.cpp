// The console's suggestions as you type (console_suggest.h): matching, hint lines, values, and the list's keys.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/overlay/console_suggest.h"

using namespace emf::core;

namespace {

// Commands as CommandCenter::list() gives them: sorted by name, help "<args>: what it does".
CompletionSources sources() {
    CompletionSources s;
    const std::vector<std::pair<std::string, std::string>> cmds = {
        {"give", "<item name> [count]: drop items at your feet (host)"},
        {"goto", "[#k] <x> <y> <z> | <entity> [ground] (host)"},
        {"handover.to", "<name|#k> <item> [count|all]: hand items to that player (within reach)"},
        {"help", "[page]: list the commands (in the chat 8 per page)"},
        {"loot.status", ": is your Loot Magnet on, and its rules"},
        {"loot.toggle", "[on|off]: your Loot Magnet (corpses of creatures killed near you come to you)"},
        {"lua.reload", ": reload the Lua mods whose files changed (this also happens every second)"},
        {"lua.status", ": every Lua mod and whether it runs"},
        {"pos", ": your position"},
        {"put", "<item name> [count 1-65535] [entity] [level] [rarity 0-4]: put items straight into an inventory"},
        {"set", "<mod> <key> <value>: change a mod's setting (saved in emf/config/<mod>.ini)"},
        {"settings", "<mod>: list a mod's settings"},
        {"tpl", "<entity> <component> [bytes 1-256] [offset]: hex dump of the entity's template data"},
        {"tplset", "<entity> <component> <offset> <f32|i32|u32|u16|u8> <value>: change one template value (host)"},
    };
    for (const auto& [name, help] : cmds) {
        s.commands.push_back(name);
        s.command_help.push_back(help);
    }
    s.items = {"Material_T1_Wood_Logs", "Material_T1_Wood_Logs_Dead", "Material_T4_Wood_Logs_Palm", "Tool_Torch01",
               "Material_T0_simple_FishingBait"};
    s.components = {"ActiveNpcState", "Enemy01BehaviorState", "Locomotion"};
    ModSource clock;
    clock.id = "clock";
    clock.name = "Clock";
    SettingSource place("place");
    place.label = "Where it shows";
    place.values = {{"top_right", "Top right"}, {"bottom_right", "Bottom right"}, {"bottom_left", "Bottom left"}};
    SettingSource h24("h24");
    h24.label = "24-hour clock";
    h24.values = {{"on", ""}, {"off", ""}};
    clock.settings = {place, h24};
    ModSource companion;
    companion.id = "companion";
    companion.name = "Companion";
    SettingSource faction("faction");
    faction.values = {{"Friendly", ""}, {"Neutral", ""}, {"Hostile", ""}};
    companion.settings = {faction};
    s.mod_settings = {clock, companion};
    s.version = 1;
    return s;
}

Suggestions at_end(const std::string& line, const CompletionSources& src, size_t rows = 8) {
    return suggest(line, line.size(), src, rows);
}

std::vector<std::string> labels(const Suggestions& s) {
    std::vector<std::string> out;
    for (const auto& r : s.rows) out.push_back(r.label);
    return out;
}

// The hint line as text, the argument being typed in *stars*.
std::string shown(const Suggestions& s) {
    std::string out;
    for (const auto& p : s.hint) out += p.kind == HintPiece::Kind::Current ? "*" + p.text + "*" : p.text;
    return out;
}

}  // namespace

TEST_CASE("split_help: \"<args>: what it does\" in two parts") {
    auto h = split_help("<item name> [count]: drop items at your feet (host)");
    CHECK(h.args == "<item name> [count]");
    CHECK(h.text == "drop items at your feet (host)");
    h = split_help(": your position");
    CHECK(h.args.empty());
    CHECK(h.text == "your position");
    h = split_help("[#k] <x> <y> <z> | <entity> [ground] (host)");  // no ':' and starts with an argument: all arguments
    CHECK(h.args == "[#k] <x> <y> <z> | <entity> [ground] (host)");
    CHECK(h.text.empty());
    h = split_help("toggles the thing");  // no ':' and no argument: all text
    CHECK(h.args.empty());
    CHECK(h.text == "toggles the thing");
    h = split_help("shows the clock: on or off");  // no arguments before the ':': all text
    CHECK(h.args.empty());
    CHECK(h.text == "shows the clock: on or off");
    h = split_help("<a>: one: two");  // the first ':' splits
    CHECK(h.args == "<a>");
    CHECK(h.text == "one: two");
    h = split_help("<time 12:30> [x]: sets it");  // a ':' inside brackets is the argument's
    CHECK(h.args == "<time 12:30> [x]");
    CHECK(h.text == "sets it");
    h = split_help("");
    CHECK(h.args.empty());
    CHECK(h.text.empty());
}

TEST_CASE("parse_hint: arguments, optional ones, the \"a|b\" choices and other words") {
    auto t = parse_hint("<item name> [count]");
    REQUIRE(t.size() == 2);
    CHECK(t[0].kind == HintToken::Kind::Arg);
    CHECK(t[0].text == "<item name>");
    CHECK(t[0].name == "item name");
    CHECK_FALSE(t[0].optional);
    CHECK(t[1].name == "count");
    CHECK(t[1].optional);
    CHECK(t[1].choices.empty());

    CHECK(parse_hint("[on|off]")[0].choices == std::vector<std::string>{"on", "off"});
    CHECK(parse_hint("<f32|i32|u32|u16|u8>")[0].choices == std::vector<std::string>{"f32", "i32", "u32", "u16", "u8"});
    CHECK(parse_hint("[skeleton|lich|golem|all]")[0].choices.size() == 4);
    CHECK(parse_hint("[count|all]")[0].choices == std::vector<std::string>{"all"});  // count stands for a number
    CHECK(parse_hint("<name|#k>")[0].choices.empty());
    CHECK(parse_hint("<entity|off>")[0].choices == std::vector<std::string>{"off"});
    CHECK(parse_hint("[setting|all]")[0].choices == std::vector<std::string>{"all"});
    // the first word of each way: profile's "[save <name> [--settings] [--replace] | load <name>]"
    const auto p = parse_hint("[save <name> [--settings] [--replace] | load <name>]");
    REQUIRE(p.size() == 1);
    CHECK(p[0].choices == std::vector<std::string>{"save", "load"});

    t = parse_hint("[#k] <x> <y> <z> | <entity> [ground] (host)");
    REQUIRE(t.size() == 8);
    CHECK(t[0].name == "#k");
    CHECK(t[4].kind == HintToken::Kind::Or);
    CHECK(t[5].name == "entity");
    CHECK(t[7].kind == HintToken::Kind::Text);
    CHECK(t[7].text == "(host)");

    t = parse_hint("<#k|name> [<#m|name>]");  // brackets inside brackets: one argument
    REQUIRE(t.size() == 2);
    CHECK(t[1].text == "[<#m|name>]");
    CHECK(t[1].optional);
    CHECK(parse_hint("<item name").size() == 1);  // unclosed: no crash
    CHECK(parse_hint("").empty());
}

TEST_CASE("suggest: commands that start with what is typed come first, then those that hold it") {
    const auto src = sources();
    auto s = at_end("lo", src);
    CHECK(labels(s) == std::vector<std::string>{"loot.status", "loot.toggle", "lua.reload"});
    REQUIRE(s.rows.size() == 3);
    CHECK(s.rows[1].hint == "[on|off]");
    CHECK(s.rows[1].help == "your Loot Magnet (corpses of creatures killed near you come to you)");
    CHECK(s.rows[0].hint.empty());
    CHECK(s.rows[0].help == "is your Loot Magnet on, and its rules");
    CHECK(s.rows[0].insert == "loot.status");
    CHECK(s.typed == "lo");
    CHECK(s.hint.empty());  // no hint line while the command is typed

    CHECK(labels(at_end("reload", src)) == std::vector<std::string>{"lua.reload"});  // held, not at the start
    CHECK(labels(at_end("STATUS", src)) == std::vector<std::string>{"loot.status", "lua.status"});  // any case
    CHECK(labels(at_end("p", src)).front() == "pos");
    CHECK(at_end("", src).rows.empty());
    CHECK(at_end("   ", src).rows.empty());
    CHECK(at_end("zzz", src).rows.empty());
    CHECK(at_end("pos", src).rows.empty());  // only itself: nothing to suggest

    CompletionSources many;
    for (int i = 0; i < 20; ++i) many.commands.push_back("cmd" + std::to_string(i));
    CHECK(at_end("cmd", many, 8).rows.size() == 8);
    CHECK(at_end("cmd", many, 12).rows.size() == 12);
    CHECK(at_end("cmd", many, 3).rows.size() == 3);
}

TEST_CASE("suggest: ghost text completes the top match after the caret, when it starts with what was typed") {
    const auto src = sources();
    auto s = at_end("lo", src);
    CHECK(ghost_of("lo", 2, s, 0) == "ot.status");
    CHECK(ghost_of("lo", 2, s, 1) == "ot.toggle");
    CHECK(ghost_of("lo", 2, s, 2).empty());  // lua.reload holds "lo" but does not start with it
    s = at_end("LoOt.T", src);
    CHECK(ghost_of("LoOt.T", 6, s, 0) == "oggle");  // any case
    s = suggest("lo 5", 2, src, 8);  // the caret is not at the end of the line
    CHECK_FALSE(s.rows.empty());
    CHECK(ghost_of("lo 5", 2, s, 0).empty());
}

TEST_CASE("take_row: the row replaces the word being typed, a space follows, the rest of the line stays") {
    const auto src = sources();
    auto s = at_end("lo", src);
    auto e = take_row("lo", s, 1);
    CHECK(e.text == "loot.toggle ");
    CHECK(e.caret == 12);
    // the caret inside a word: the whole word is replaced, the words after it stay
    s = suggest("loxx 5", 2, src, 8);
    e = take_row("loxx 5", s, 0);
    CHECK(e.text == "loot.status 5");
    CHECK(e.caret == 12);
    // an item name of several words is replaced as one
    s = at_end("give wood log", src);
    REQUIRE_FALSE(s.rows.empty());
    e = take_row("give wood log", s, 0);
    CHECK(e.text == "give Material_T1_Wood_Logs ");
    CHECK(e.caret == e.text.size());
}

TEST_CASE("suggest: the hint line shows the command's arguments with the one being typed, and its help") {
    const auto src = sources();
    CHECK(shown(at_end("give ", src)) == "give *<item name>* [count]: drop items at your feet (host)");
    CHECK(shown(at_end("give wood", src)) == "give *<item name>* [count]: drop items at your feet (host)");
    CHECK(shown(at_end("give wood 5", src)) == "give <item name> *[count]*: drop items at your feet (host)");
    CHECK(shown(at_end("GIVE wood 5", src)) == "give <item name> *[count]*: drop items at your feet (host)");
    // an item name is several words, up to a plain number
    CHECK(shown(at_end("give wood log", src)) == "give *<item name>* [count]: drop items at your feet (host)");
    // after a space: still the name while it means no one item yet, the count once it does
    CHECK(shown(at_end("give wood ", src)) == "give *<item name>* [count]: drop items at your feet (host)");
    CHECK(shown(at_end("give wood log ", src)) == "give <item name> *[count]*: drop items at your feet (host)");
    CHECK(shown(at_end("give Material_T1_Wood_Logs ", src)) ==
          "give <item name> *[count]*: drop items at your feet (host)");
    // the last argument takes the rest of the line
    CHECK(shown(at_end("give wood 5 6 7", src)) == "give <item name> *[count]*: drop items at your feet (host)");
    CHECK(shown(at_end("set clock place top right", src)) ==
          "set <mod> <key> *<value>*: change a mod's setting (saved in emf/config/<mod>.ini)");
    // no arguments
    CHECK(shown(at_end("pos ", src)) == "pos: your position");
    // two ways to call it: each shows its argument; [#k] is passed by a word without '#'
    CHECK(shown(at_end("goto ", src)) == "goto *[#k]* <x> <y> <z> | *<entity>* [ground] (host)");
    CHECK(shown(at_end("goto 12", src)) == "goto [#k] *<x>* <y> <z> | *<entity>* [ground] (host)");
    CHECK(shown(at_end("goto 12 5", src)) == "goto [#k] <x> *<y>* <z> | <entity> *[ground]* (host)");
    CHECK(shown(at_end("goto #2 5", src)) == "goto [#k] *<x>* <y> <z> | <entity> *[ground]* (host)");
    // the caret decides which argument is being typed
    CHECK(shown(suggest("give wood 5", 7, src, 8)) == "give *<item name>* [count]: drop items at your feet (host)");
    // not yet a whole command, or not a command
    CHECK(at_end("give", src).hint.empty());
    CHECK(at_end("nosuch ", src).hint.empty());
    CHECK(at_end("nosuch x", src).rows.empty());
}

TEST_CASE("suggest: the \"a|b\" choices of the argument being typed") {
    const auto src = sources();
    CHECK(labels(at_end("loot.toggle ", src)) == std::vector<std::string>{"on", "off"});  // shown at once
    CHECK(labels(at_end("loot.toggle o", src)) == std::vector<std::string>{"on", "off"});
    CHECK(labels(at_end("loot.toggle OF", src)) == std::vector<std::string>{"off"});
    CHECK(at_end("loot.toggle on", src).rows.empty());  // exactly what was typed
    CHECK(at_end("loot.toggle on ", src).rows.empty());
    CHECK(labels(at_end("tplset 6820 Locomotion 0 u", src)) == std::vector<std::string>{"u32", "u16", "u8"});
    CHECK(labels(at_end("handover.to bob torch ", src)) == std::vector<std::string>{"all"});  // after the item
    CHECK(at_end("help ", src).rows.empty());  // [page]: nothing to choose from
    auto s = at_end("loot.toggle o", src);
    CHECK(take_row("loot.toggle o", s, 1).text == "loot.toggle off ");
    CHECK(ghost_of("loot.toggle o", 13, s, 0) == "n");
}

TEST_CASE("suggest: item names for give, put and any <item> argument, by the friendly-name rules, best first") {
    const auto src = sources();
    auto s = at_end("give wood", src);
    CHECK(labels(s) ==
          std::vector<std::string>{"Material_T1_Wood_Logs", "Material_T1_Wood_Logs_Dead", "Material_T4_Wood_Logs_Palm"});
    REQUIRE_FALSE(s.rows.empty());
    CHECK(s.rows[0].insert == "Material_T1_Wood_Logs");
    CHECK(s.typed == "wood");
    CHECK(labels(at_end("give logs palm", src)) == std::vector<std::string>{"Material_T4_Wood_Logs_Palm"});
    CHECK(labels(at_end("give simple bait", src)) == std::vector<std::string>{"Material_T0_simple_FishingBait"});
    CHECK(labels(at_end("put to", src)) == std::vector<std::string>{"Tool_Torch01"});  // a part of a word
    CHECK(labels(at_end("handover.to bob torch", src)) == std::vector<std::string>{"Tool_Torch01"});  // <item>
    CHECK(labels(at_end("give wood ", src)).size() == 3);  // still the name: its matches stay
    CHECK(at_end("give ", src).rows.empty());               // nothing typed yet: 4000 names are no help
    CHECK(at_end("give wood 5", src).rows.empty());         // the count
    CHECK(at_end("give wood log ", src).rows.empty());      // one item: the count is next
    CHECK(at_end("give nothinglikethis", src).rows.empty());
    CHECK(at_end("give wood", src, 2).rows.size() == 2);
}

TEST_CASE("suggest: components for tpl and tplset once something is typed") {
    const auto src = sources();
    CHECK(labels(at_end("tpl 1025 activenpc", src)) == std::vector<std::string>{"ActiveNpcState"});
    CHECK(labels(at_end("tplset 6820 behavior", src)) == std::vector<std::string>{"Enemy01BehaviorState"});
    CHECK(at_end("tpl 1025 ", src).rows.empty());
    CHECK(at_end("tpl ", src).rows.empty());  // <entity>
}

TEST_CASE("suggest: mod ids, a mod's setting keys and what a setting takes for settings and set") {
    const auto src = sources();
    auto s = at_end("set ", src);
    CHECK(labels(s) == std::vector<std::string>{"clock", "companion"});
    REQUIRE_FALSE(s.rows.empty());
    CHECK(s.rows[0].help == "Clock");
    CHECK(labels(at_end("settings com", src)) == std::vector<std::string>{"companion"});
    s = at_end("set clock ", src);
    CHECK(labels(s) == std::vector<std::string>{"place", "h24", "enabled"});
    REQUIRE_FALSE(s.rows.empty());
    CHECK(s.rows[0].help == "Where it shows");
    CHECK(labels(at_end("set CLOCK p", src)) == std::vector<std::string>{"place"});
    CHECK(at_end("set nobody ", src).rows.empty());
    s = at_end("set clock place ", src);
    CHECK(labels(s) == std::vector<std::string>{"top_right", "bottom_right", "bottom_left"});
    REQUIRE_FALSE(s.rows.empty());
    CHECK(s.rows[0].help == "Top right");
    CHECK(labels(at_end("set clock place bottom", src)) == std::vector<std::string>{"bottom_right", "bottom_left"});
    CHECK(labels(at_end("set clock place left", src)) == std::vector<std::string>{"bottom_left"});
    CHECK(labels(at_end("set clock h24 ", src)) == std::vector<std::string>{"on", "off"});
    CHECK(labels(at_end("set clock enabled ", src)) == std::vector<std::string>{"on", "off"});
    CHECK(labels(at_end("set companion faction neu", src)) == std::vector<std::string>{"Neutral"});
    CHECK(at_end("set clock nokey ", src).rows.empty());
    CHECK(take_row("set clock place b", at_end("set clock place b", src), 1).text == "set clock place bottom_left ");
}

TEST_CASE("setting_source: on/off for a switch, a list's choices, a select's options with their labels") {
    SettingDef b;
    b.key = "h24";
    b.type = SettingType::Bool;
    auto s = setting_source(b);
    CHECK(s.key == "h24");
    CHECK(s.label == "h24");  // no label: the key
    CHECK(s.values == std::vector<std::pair<std::string, std::string>>{{"on", ""}, {"off", ""}});
    SettingDef c;
    c.key = "faction";
    c.label = "Faction";
    c.type = SettingType::Choice;
    c.choices = {"Friendly", "Hostile"};
    s = setting_source(c);
    CHECK(s.label == "Faction");
    CHECK(s.values == std::vector<std::pair<std::string, std::string>>{{"Friendly", ""}, {"Hostile", ""}});
    SettingDef sel;
    sel.key = "place";
    sel.type = SettingType::Select;
    sel.options = {{"top_right", "Top right"}, {"left", "left"}};
    s = setting_source(sel);
    CHECK(s.values == std::vector<std::pair<std::string, std::string>>{{"top_right", "Top right"}, {"left", ""}});
    SettingDef n;
    n.key = "radius";
    n.type = SettingType::Int;
    CHECK(setting_source(n).values.empty());
}

TEST_CASE("Suggester: the list shows while there are rows; Up/Down move in it and wrap; the ghost follows") {
    const auto src = sources();
    Suggester g;
    g.update("lo", 2, src);
    CHECK(g.showing());
    CHECK(g.selected() == 0);
    CHECK(g.ghost() == "ot.status");
    CHECK(g.move(+1));
    CHECK(g.selected() == 1);
    CHECK(g.ghost() == "ot.toggle");
    CHECK(g.move(+1));
    CHECK(g.selected() == 2);
    CHECK(g.ghost().empty());  // lua.reload does not start with "lo"
    CHECK(g.move(+1));
    CHECK(g.selected() == 0);  // wraps
    CHECK(g.move(-1));
    CHECK(g.selected() == 2);
    g.update("lo", 2, src);  // nothing changed: the selection stays
    CHECK(g.selected() == 2);
    g.update("loo", 3, src);  // typing starts at the top again
    CHECK(g.selected() == 0);
    g.update("", 0, src);
    CHECK_FALSE(g.showing());
    CHECK_FALSE(g.move(-1));  // no list: Up is the history's
    CHECK_FALSE(g.move(+1));
}

TEST_CASE("Suggester: Esc hides the list first, typing shows it again; a caret move alone does not") {
    const auto src = sources();
    Suggester g;
    g.update("lo", 2, src);
    REQUIRE(g.showing());
    CHECK(g.escape());  // hid it
    CHECK_FALSE(g.showing());
    CHECK(g.ghost().empty());
    CHECK_FALSE(g.move(-1));  // hidden: Up/Down are the history's
    CHECK_FALSE(g.escape());  // nothing to hide: the console closes
    g.update("lo", 1, src);    // the caret moved
    CHECK_FALSE(g.showing());
    g.update("loo", 3, src);  // typed
    CHECK(g.showing());
    g.update("", 0, src);
    CHECK_FALSE(g.escape());  // no rows: Esc closes the console
}

TEST_CASE("Suggester: a line from the history hides the list until the next edit, so Up/Down stay the history's") {
    const auto src = sources();
    Suggester g;
    g.update("", 0, src);
    g.replaced("loot.status", 11);  // Up brought back "loot.status"
    g.update("loot.status", 11, src);
    CHECK_FALSE(g.showing());
    CHECK_FALSE(g.move(-1));
    g.replaced("lo", 2);  // an older line that has matches
    g.update("lo", 2, src);
    CHECK_FALSE(g.showing());
    CHECK_FALSE(g.move(+1));
    g.update("lo", 1, src);  // only the caret moved
    CHECK_FALSE(g.showing());
    g.update("lu", 2, src);  // an edit
    CHECK(g.showing());
}

TEST_CASE("Suggester: Tab takes the selected row (also while hidden), Right only while it shows at the line's end") {
    const auto src = sources();
    Suggester g;
    g.update("loot.t", 6, src);
    auto e = g.accept(src);
    REQUIRE(e);
    CHECK(e->text == "loot.toggle ");
    CHECK(e->caret == 12);
    // the line the input now has is not an edit of the player's; the next argument's values show
    g.update("loot.toggle ", 12, src);
    CHECK(g.showing());
    CHECK(g.selected() == 0);
    REQUIRE(g.current().rows.size() == 2);
    CHECK(g.current().rows[1].label == "off");
    CHECK(g.move(+1));
    e = g.accept_right(src);
    REQUIRE(e);
    CHECK(e->text == "loot.toggle off ");
    g.update("loot.toggle off ", 16, src);
    CHECK_FALSE(g.accept(src));  // no rows
    CHECK_FALSE(g.accept_right(src));

    g.update("lo", 2, src);
    g.escape();
    CHECK_FALSE(g.accept_right(src));  // hidden: Right just moves the caret
    e = g.accept(src);                 // Tab still completes
    REQUIRE(e);
    CHECK(e->text == "loot.status ");

    g.update("lo 5", 2, src);  // the caret is not at the end of the line
    REQUIRE(g.showing());
    CHECK_FALSE(g.accept_right(src));
    e = g.accept(src);
    REQUIRE(e);
    CHECK(e->text == "loot.status 5");
}

TEST_CASE("Suggester: taking a row works out the new line's suggestions at once, with no old list or ghost left over") {
    const auto src = sources();
    Suggester g;
    g.update("lo", 2, src);
    REQUIRE(g.ghost() == "ot.status");
    auto e = g.accept(src);
    REQUIRE(e);
    CHECK(e->text == "loot.status ");
    CHECK_FALSE(g.showing());  // loot.status takes nothing
    CHECK(g.ghost().empty());  // not "ot.status" again after "loot.status "
    CHECK(shown(g.current()) == "loot.status: is your Loot Magnet on, and its rules");
    g.update("loot.t", 6, src);
    e = g.accept_right(src);
    REQUIRE(e);
    CHECK(labels(g.current()) == std::vector<std::string>{"on", "off"});
    CHECK(g.ghost() == "on");
    const uint64_t n = g.computed();
    g.update(e->text, e->caret, src);  // the input now has that line: nothing to work out again
    CHECK(g.computed() == n);
    CHECK(g.ghost() == "on");
}

TEST_CASE("Suggester: works out the suggestions only when the line, the caret or the sources change") {
    auto src = sources();
    Suggester g;
    g.update("give wood", 9, src);
    const uint64_t n = g.computed();
    CHECK(n >= 1);
    for (int i = 0; i < 100; ++i) g.update("give wood", 9, src);  // every frame while nothing changes
    CHECK(g.computed() == n);
    g.update("give wood", 8, src);
    CHECK(g.computed() == n + 1);
    src.items.push_back("Material_T2_Wood_Hardwood");
    ++src.version;
    g.update("give wood", 8, src);
    CHECK(g.computed() == n + 2);
    CHECK(g.current().rows.size() == 4);
    g.clear();
    CHECK(g.current().rows.empty());
    CHECK_FALSE(g.showing());
}

TEST_CASE("Suggester: the number of rows stays within 3-12") {
    Suggester g;
    CHECK(g.rows() == 8);
    g.set_rows(1);
    CHECK(g.rows() == 3);
    g.set_rows(40);
    CHECK(g.rows() == 12);
    g.set_rows(5);
    CompletionSources many;
    for (int i = 0; i < 20; ++i) many.commands.push_back("cmd" + std::to_string(i));
    g.update("cmd", 3, many);
    CHECK(g.current().rows.size() == 5);
}
