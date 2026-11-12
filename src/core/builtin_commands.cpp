#include "builtin_commands.h"

#include "mod_registry.h"
#include "settings.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "common/offsets.h"
#include "common/pattern_find.h"
#include "common/seh.h"
#include "api.h"
#include "daytime.h"
#include "inventory.h"
#include "item_names.h"
#include "path_service.h"
#include "emf/emf.hpp"
#include "emf/generated/components.hpp"

namespace emf::core {
namespace {

constexpr double kFixed = 4294967296.0;

BuiltinContext& ctx_of(void* user) { return *static_cast<BuiltinContext*>(user); }

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

bool parse_uint(const char* s, uint32_t max, uint32_t* out) {
    if (!s || !*s) return false;
    uint64_t v = 0;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10 + uint64_t(*p - '0');
        if (v > max) return false;
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

bool parse_hours(const char* s, double* out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (!end || *end || !(v >= 0.0 && v <= 24.0)) return false;
    *out = v;
    return true;
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// ---- item names as people type them (item_names.h): give, put, take, stack, items ----
struct ItemList {
    std::vector<NamedItem> named;
    std::vector<uint16_t> stack;  // max stack, in the same order
};

// A command typed in the game's chat: names in the player's language (the game's chat draws every script); on EMF's
// own console the names its font can draw.
bool from_game_chat() {
    const CommandSender* s = current_sender();
    return s && s->from_chat;
}

ItemList all_items(BuiltinContext& c, EmfWorld* w) {
    ItemList l;
    c.api->for_each_item(w, [](const EmfItemInfo* i, void* p) {
        auto* list = static_cast<ItemList*>(p);
        list->named.push_back({i->item_id, i->name, {}});
        list->stack.push_back(i->max_stack);
    }, &l);
    if (c.api->item_name) {  // v29: the names the game shows (none on the dedicated server)
        char buf[256];
        const uint32_t flags = from_game_chat() ? EMF_ITEM_NAME_GAME_TEXT : 0u;
        for (NamedItem& n : l.named)
            if (c.api->item_name(w, n.id, flags, buf, sizeof buf)) n.shown = buf;
    }
    return l;
}

// "Wood Logs (Material_T1_Wood_Logs)" when the game has a name for the item here, else the debug name.
std::string item_label(const EmfApi* api, EmfWorld* w, uint32_t id, const char* debug_name, bool game_chat) {
    char buf[256];
    const std::string debug = debug_name ? debug_name : "";
    if (api->item_name && api->item_name(w, id, game_chat ? EMF_ITEM_NAME_GAME_TEXT : 0u, buf, sizeof buf))
        return debug.empty() ? std::string(buf) : std::string(buf) + " (" + debug + ")";
    return debug;
}

// What read_item found.
enum class ItemRead { Ok, Usage, Replied };

// The item and numbers a command's words mean (read_item_args: "give simple bait 4", "take tool torch 01 5"). Usage when
// there is no name, or a quoted name is followed by something else than up to max_numbers numbers; Replied after saying
// that no item has that name, which items it could be, or, when the words can be read as different items, how to type
// each reading.
ItemRead read_item(BuiltinContext& c, EmfWorld* w, const char* cmd, int argc, const char* const* argv,
                   size_t max_numbers, uint32_t* id, std::vector<std::string>* numbers) {
    if (argc < 2) return ItemRead::Usage;
    const std::vector<std::string> words(argv + 1, argv + argc);
    const ItemList l = all_items(c, w);
    const ItemArgs a = read_item_args(l.named, words, max_numbers);
    if (a.bad) return ItemRead::Usage;
    if (a.id) {
        *id = a.id;
        *numbers = a.numbers;
        return ItemRead::Ok;
    }
    const auto name_of = [&](uint32_t item) {
        for (const auto& n : l.named)
            if (n.id == item) return n.name;
        return std::string("?");
    };
    if (!a.readings.empty()) {
        std::string typed, choices;
        for (const auto& word : words) typed += (typed.empty() ? "" : " ") + word;
        for (const auto& [item, nums] : a.readings) {
            choices += (choices.empty() ? "" : " | ") + std::string(cmd) + " " + name_of(item);
            for (const auto& n : nums) choices += " " + n;
        }
        command_reply_line("'" + typed + "' can be read more than one way; type one of: " + choices);
        return ItemRead::Replied;
    }
    const ItemMatch& m = a.match;
    if (m.matches.empty()) {
        command_reply_line("unknown item '" + a.name + "' (items <part of a name> lists names)");
        return ItemRead::Replied;
    }
    constexpr size_t kNamed = 8;
    std::string line = "'" + a.name + "' could be " + std::to_string(m.matches.size()) + " items: ";
    for (size_t k = 0; k < m.matches.size() && k < kNamed; ++k) line += (k ? ", " : "") + l.named[m.matches[k]].name;
    if (m.matches.size() > kNamed) line += " and " + std::to_string(m.matches.size() - kNamed) + " more";
    command_reply_line(line + ". Type more of the name.");
    return ItemRead::Replied;
}

bool player_pos(BuiltinContext& c, EmfWorld* w, uint32_t player, double out[3]) {
    uint64_t size = 0;
    const auto* p = static_cast<const int64_t*>(c.api->get_component(w, off::kHashCurrentTransform, player, &size));
    if (!p || size < 24) return false;
    for (int i = 0; i < 3; ++i) out[i] = p[i] / kFixed;
    return true;
}

// A component of `id` when its size is the generated struct's (else null).
template <class T>
const T* comp_of(BuiltinContext& c, EmfWorld* w, uint32_t id) {
    uint64_t size = 0;
    const void* p = c.api->get_component(w, T::kHash, id, &size);
    return p && size == sizeof(T) ? static_cast<const T*>(p) : nullptr;
}

// near [radius]: the entities around the player, nearest first, tagged by what they have.
void cmd_near(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    uint32_t radius = 10;
    if (argc > 2 || (argc == 2 && (!parse_uint(argv[1], 200, &radius) || radius == 0))) {
        command_reply_line("usage: near [radius 1-200]");
        return;
    }
    double p[3];
    if (!player_pos(c, w, c.player ? c.player() : 0, p)) {
        command_reply_line("near: player position unknown");
        return;
    }
    struct Hit {
        uint32_t id;
        double d, pos[3];
    };
    struct Scan {
        BuiltinContext* c;
        EmfWorld* w;
        double p[3], r;
        std::vector<Hit> hits;
    } s{&c, w, {p[0], p[1], p[2]}, static_cast<double>(radius), {}};
    c.api->for_each_entity(w, [](uint32_t id, void* user) {
        auto& s = *static_cast<Scan*>(user);
        double q[3];
        if (!player_pos(*s.c, s.w, id, q)) return;
        const double dx = q[0] - s.p[0], dy = q[1] - s.p[1], dz = q[2] - s.p[2];
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d <= s.r) s.hits.push_back({id, d, {q[0], q[1], q[2]}});
    }, &s);
    std::sort(s.hits.begin(), s.hits.end(), [](const Hit& a, const Hit& b) { return a.d < b.d; });
    const size_t shown = (std::min)(s.hits.size(), size_t{30});
    for (size_t i = 0; i < shown; ++i) {
        const Hit& h = s.hits[i];
        std::string tags;
        if (comp_of<emf::gen::ecs::PlayerState>(c, w, h.id)) tags += " player";
        if (c.api->get_template_component && c.api->get_template_component(w, off::kHashActiveNpcState, h.id, nullptr))
            tags += " villager";
        if (comp_of<emf::gen::ecs::InventorySetup>(c, w, h.id)) tags += " inventory";
        if (comp_of<emf::gen::ecs::AnimalSaved>(c, w, h.id)) tags += " animal";
        if (comp_of<emf::gen::ecs::DynamicGrowth>(c, w, h.id)) {
            const auto* lv = comp_of<emf::gen::ecs::Level>(c, w, h.id);
            tags += lv ? fmt(" plant (level %u)", lv->dataStorage[0]) : std::string(" plant");
        }
        if (comp_of<emf::gen::ecs::Health>(c, w, h.id)) tags += " health";
        command_reply_line(fmt("  %u: %.1f m (%.1f, %.1f, %.1f)%s", h.id, h.d, h.pos[0], h.pos[1], h.pos[2], tags.c_str()));
    }
    command_reply_line(fmt("%zu entities within %u m%s", s.hits.size(), radius,
                           s.hits.size() > shown ? " (the nearest 30 shown)" : ""));
}

// inv <entity>: every stack in the entity's own and linked inventories.
void cmd_inv(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    uint32_t id = 0;
    if (argc != 2 || !parse_uint(argv[1], 0xFFFFFFFFu, &id) || !id) {
        command_reply_line("usage: inv <entity>");
        return;
    }
    const auto* setup = comp_of<emf::gen::ecs::InventorySetup>(c, w, id);
    std::vector<uint32_t> invs{id};
    if (setup)
        for (const auto& link : setup->linksEntities)
            if (link.id && link.id != id) invs.push_back(link.id);
    size_t n = 0;
    for (uint32_t inv_id : invs) {
        const auto* inv = comp_of<emf::gen::ecs::Inventory>(c, w, inv_id);
        if (!inv) continue;
        for (uint32_t slot = 0; slot < 8; ++slot) {
            const auto& st = inv->slots[slot];
            if (!st.id.value) continue;
            ++n;
            EmfItemInfo info{};
            info.size = sizeof(info);
            const std::string name = c.api->item_info(w, st.id.value, &info) && info.name[0]
                                         ? std::string(info.name)
                                         : fmt("item %08x", st.id.value);
            std::string extra;
            if (st.data.pide.id) {
                extra = fmt(", data entity %u", st.data.pide.id);
                if (const auto* is = comp_of<emf::gen::ecs::ItemState>(c, w, st.data.pide.id))
                    extra += fmt(", level %u, rarity %u", static_cast<unsigned>(is->itemLevelUi),
                                 static_cast<unsigned>(is->itemRarityUi));
            }
            const unsigned long long slot_id = (static_cast<unsigned long long>(slot) << 32) | inv_id;
            command_reply_line(fmt("  inventory %u slot %u (slot id 0x%llx): %s x%u%s", inv_id, slot, slot_id,
                                   name.c_str(), st.data.count, extra.c_str()));
        }
    }
    if (!setup && !n)
        command_reply_line(fmt("entity %u has no inventory", id));
    else
        command_reply_line(fmt("entity %u: %zu stack(s)", id, n));
}

// hud <id> [place 0-2] [text]: a line kept on screen (API v12), to try a layout; no text removes it.
void cmd_hud(EmfWorld*, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    if (argc < 2) {
        command_reply_line("usage: hud <id> [place 0-2] [text]: no text removes the line");
        return;
    }
    uint32_t place = 0;
    int first = 2;
    if (argc > 2 && parse_uint(argv[2], 2, &place)) first = 3;
    std::string text;
    for (int i = first; i < argc; ++i) text += (text.empty() ? "" : " ") + std::string(argv[i]);
    if (!c.api->hud_set || !c.api->hud_set(argv[1], text.c_str(), place, 0)) {
        command_reply_line("hud: refused (see the log)");
        return;
    }
    command_reply_line(text.empty() ? fmt("hud: %s removed", argv[1]) : fmt("hud: %s shown (place %u)", argv[1], place));
}

// Every command; for a player in chat who is no admin only the ones they may run (marked for players, v17). In the
// chat (10 lines per command reach it) in pages of 8: "help 2" and so on.
void cmd_help(EmfWorld*, int argc, const char* const* argv, void* u) {
    const CommandSender* s = current_sender();
    const bool restricted = s && s->restricted;
    const bool chat = s && s->from_chat;
    std::vector<std::string> lines;
    for (const auto& c : ctx_of(u).center->entries()) {
        if (restricted && !c.players) continue;
        std::string line = c.help.empty() ? c.name : c.name + " " + c.help;
        if (c.players && !restricted && c.name != "help") line += " (any player)";
        lines.push_back(line);
    }
    if (!chat) {
        for (const auto& l : lines) command_reply_line(l);
        return;
    }
    constexpr size_t kPage = 8;
    const size_t pages = lines.empty() ? 1 : (lines.size() + kPage - 1) / kPage;
    size_t page = argc > 1 ? static_cast<size_t>(std::strtoul(argv[1], nullptr, 10)) : 1;
    page = page < 1 ? 1 : page > pages ? pages : page;
    std::string head = std::string(restricted ? "commands you may run here" : "commands") + ", page " +
                       std::to_string(page) + " of " + std::to_string(pages);
    if (page < pages) head += " (/help " + std::to_string(page + 1) + " for more)";
    command_reply_line(head);
    for (size_t i = (page - 1) * kPage; i < lines.size() && i < page * kPage; ++i) command_reply_line(lines[i]);
}

void cmd_mods(EmfWorld*, int, const char* const*, void* u) {
    auto& c = ctx_of(u);
    const auto mods = c.registry ? c.registry->list() : std::vector<ModRecord>{};
    size_t loaded = 0, off = 0, not_loaded = 0;
    for (const auto& m : mods) {
        std::string line = m.id;
        if (!m.version.empty()) line += " " + m.version;
        line += " (" + (m.has_manifest ? m.name : m.source) + "): " + mod_status_name(m.status);
        if (!m.reason.empty()) line += " (" + m.reason + ")";
        command_reply_line(line);
        if (m.status == ModStatus::Loaded || m.status == ModStatus::Script) ++loaded;
        else if (m.status == ModStatus::Disabled) ++off;
        else ++not_loaded;
    }
    command_reply_line(fmt("%zu mod(s): %zu loaded, %zu switched off, %zu not loaded", mods.size(), loaded, off,
                           not_loaded));
}

// "exe+0x6c0fc0" for an address in the game exe, else the module and offset, else the address
std::string where(uintptr_t a) {
    const CoreState* st = core_state();
    if (st && a >= st->exe.base && a < st->exe.base + st->exe.size)
        return fmt("exe+0x%llx", static_cast<unsigned long long>(a - st->exe.base));
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(a), &m)) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(m, path, MAX_PATH);
        std::string s(path);
        s = s.substr(s.find_last_of("\\/") + 1);
        return fmt("%s+0x%llx", s.c_str(), static_cast<unsigned long long>(a - reinterpret_cast<uintptr_t>(m)));
    }
    return fmt("0x%llx", static_cast<unsigned long long>(a));
}

void cmd_hooks(EmfWorld*, int, const char* const*, void*) {
    const CoreState* st = core_state();
    if (!st || !st->hooks) {
        command_reply_line("hooks are not available");
        return;
    }
    // The list is in install order. On one function the newest hook runs first and its original calls the one
    // installed before it; the oldest calls the game.
    const auto list = st->hooks->list();
    for (size_t i = 0; i < list.size(); ++i) {
        size_t before = 0, after = 0;
        for (size_t j = 0; j < list.size(); ++j)
            if (j != i && list[j].target == list[i].target) ++(j < i ? before : after);
        std::string line = list[i].owner + ": " + list[i].name + " at " + where(list[i].target);
        if (before + after) {
            const size_t pos = after + 1, n = before + after + 1;  // 1 = runs first
            const char* th = (pos % 100 >= 11 && pos % 100 <= 13) ? "th"
                             : pos % 10 == 1                      ? "st"
                             : pos % 10 == 2                      ? "nd"
                             : pos % 10 == 3                      ? "rd"
                                                                  : "th";
            line += fmt(" (chained: runs %zu%s of %zu)", pos, th, n);
        }
        command_reply_line(line);
    }
    command_reply_line(fmt("%zu hook(s)", list.size()));
}

void cmd_pattern(EmfWorld*, int argc, const char* const* argv, void*) {
    const CoreState* st = core_state();
    if (argc < 2 || !st) {
        command_reply_line("usage: pattern <bytes, e.g. 48 8B 05 ?? ?? ?? ??> [.section]");
        return;
    }
    std::string section = ".text", bytes;
    int last = argc - 1;
    if (argv[last][0] == '.') {
        section = argv[last];
        --last;
    }
    for (int i = 1; i <= last; ++i) bytes += (i > 1 ? " " : "") + std::string(argv[i]);
    const emf::Section* sec = nullptr;
    for (const auto& s : st->exe.sections)
        if (s.name == section) sec = &s;
    if (!sec) {
        command_reply_line("the game exe has no section " + section);
        return;
    }
    emf::PatternQuery q;
    q.bytes = bytes;
    const auto r = emf::find_unique({st->exe.scan_bytes(*sec), sec->size}, sec->start, st->exe.base, st->exe.size, q);
    if (r.address)
        command_reply_line("found once in " + section + " at " + where(r.address));
    else
        command_reply_line(r.error + (r.matches == 0 && r.error == "not found" ? " in " + section : ""));
}

std::string range_note(const SettingDef& d) {
    char b[160] = "";
    if (d.has_min && d.has_max) std::snprintf(b, sizeof b, ", %g to %g", d.min, d.max);
    else if (d.has_min) std::snprintf(b, sizeof b, ", at least %g", d.min);
    else if (d.has_max) std::snprintf(b, sizeof b, ", at most %g", d.max);
    std::string out = b;
    if (d.step > 0) {
        std::snprintf(b, sizeof b, ", steps of %g", d.step);
        out += b;
    }
    if (d.slider) out += ", slider";
    if (d.max_length) out += ", at most " + std::to_string(d.max_length) + " characters";
    return out;
}

// "1 (bottom right)" for a choice, "tr (Top right)" for a select, the value otherwise
std::string shown_value(const SettingDef& d, const std::string& v) {
    if (d.type != SettingType::Choice && d.type != SettingType::Select) return v;
    const std::string label = setting_shown_value(d, v);
    return label != v ? v + " (" + label + ")" : v;
}

void cmd_settings(EmfWorld*, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    if (argc < 2 || !c.settings) {
        command_reply_line("usage: settings <mod>");
        return;
    }
    const std::string id = argv[1];
    const auto rec = c.registry ? c.registry->find(id) : std::nullopt;
    const auto defs = c.settings->defs(id);
    if (!rec && defs.empty()) {
        command_reply_line(fmt("no mod \"%s\" (mods lists them)", id.c_str()));
        return;
    }
    if (rec)
        command_reply_line(fmt("enabled = %d (the mod's switch; applies at the next game start)",
                               c.settings->enabled(id, rec->enabled_default) ? 1 : 0));
    if (defs.empty()) {
        command_reply_line(id + " has no settings");
        return;
    }
    for (const auto& d : defs) {
        std::string type = setting_type_name(d.type);
        if (d.type == SettingType::Choice) {
            type += ":";
            for (size_t i = 0; i < d.choices.size(); ++i) type += (i ? ", " : " ") + std::to_string(i) + " " + d.choices[i];
        }
        if (d.type == SettingType::Select) {
            type += ":";
            for (size_t i = 0; i < d.options.size(); ++i)
                type += (i ? ", " : " ") + d.options[i].value +
                        (d.options[i].label != d.options[i].value ? " (" + d.options[i].label + ")" : "");
        }
        std::string line = d.key + " = " + shown_value(d, c.settings->get(id, d.key)) + "  [" + type + range_note(d) +
                           "; default " + d.default_value + "; " + (d.live ? "applies at once" : "next game start") + "]";
        if (d.label != d.key) line += "  " + d.label;
        if (!d.group.empty()) line += "  (" + d.group + ")";
        command_reply_line(line);
        if (!d.help.empty()) command_reply_line("    " + d.help);
        if (!d.when_key.empty()) {
            const bool on = setting_active(d, [&](const std::string& k) { return c.settings->get(id, k); });
            command_reply_line("    " + setting_condition_text(d, defs) + (on ? "" : " (greyed out in the Mods tab now)"));
        }
    }
}

void cmd_set(EmfWorld*, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    if (argc < 4 || !c.settings) {
        command_reply_line("usage: set <mod> <key> <value>   (settings <mod> lists the keys)");
        return;
    }
    const std::string id = argv[1], key = argv[2];
    std::string value = argv[3];
    for (int i = 4; i < argc; ++i) value += std::string(" ") + argv[i];
    if (key == "enabled") {
        const std::string l = lower(value);
        const bool on = l == "1" || l == "true" || l == "on" || l == "yes";
        const bool off = l == "0" || l == "false" || l == "off" || l == "no";
        if ((!on && !off) || !(c.registry && c.registry->find(id))) {
            command_reply_line(!(c.registry && c.registry->find(id)) ? fmt("no mod \"%s\"", id.c_str())
                                                                     : "use 1 or 0 for enabled");
            return;
        }
        command_reply_line(c.settings->set_enabled(id, on)
                               ? fmt("%s is switched %s from the next game start", id.c_str(), on ? "on" : "off")
                               : fmt("could not write emf\\config\\%s.ini", id.c_str()));
        return;
    }
    std::string why;
    const auto r = c.settings->set(id, key, value, &why);
    if (r == SettingsStore::SetResult::Ok || r == SettingsStore::SetResult::Unchanged) {
        for (const auto& d : c.settings->defs(id))
            if (d.key == key) {
                command_reply_line(id + "." + key + " = " + shown_value(d, c.settings->get(id, key)) +
                                   (d.live ? "" : " (applies at the next game start)"));
                break;
            }
        return;
    }
    command_reply_line("refused: " + why);
}

// items [words]: the items whose names hold every word (in any order, a part of a word is enough), best match first
void cmd_items(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    std::string typed;
    for (int i = 1; i < argc; ++i) typed += (i > 1 ? " " : "") + std::string(argv[i]);
    const ItemList l = all_items(c, w);
    std::vector<size_t> found;
    if (typed.empty()) {
        for (size_t i = 0; i < l.named.size(); ++i) found.push_back(i);
    } else {
        found = search_items(l.named, typed);
    }
    for (size_t k = 0; k < found.size() && k < 50; ++k) {
        const NamedItem& it = l.named[found[k]];
        // the template a placeable item puts down (spawn takes it)
        std::string places;
        uint64_t size = 0;
        const auto* info = c.api->item_data ? static_cast<const uint8_t*>(c.api->item_data(w, it.id, &size)) : nullptr;
        uint8_t tpl[16] = {};
        if (info && size >= off::kItemInfoPlacedEntity + 16) std::memcpy(tpl, info + off::kItemInfoPlacedEntity, 16);
        if (std::any_of(std::begin(tpl), std::end(tpl), [](uint8_t b) { return b != 0; }))
            places = ", places " + emf::guid_to_text(tpl);
        if (!it.shown.empty())
            command_reply_line(fmt("%s: %s (0x%08x, stack %u%s)", it.shown.c_str(), it.name.c_str(), it.id,
                                   unsigned(l.stack[found[k]]), places.c_str()));
        else
            command_reply_line(fmt("%s (0x%08x, stack %u%s)", it.name.c_str(), it.id, unsigned(l.stack[found[k]]),
                                   places.c_str()));
    }
    command_reply_line(
        fmt("%zu of %zu items%s", found.size(), l.named.size(), found.size() > 50 ? " (first 50 shown)" : ""));
}

void cmd_give(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    uint32_t count = 1, id = 0;
    std::vector<std::string> numbers;
    const ItemRead read = read_item(c, w, "give", argc, argv, 1, &id, &numbers);
    if (read == ItemRead::Replied) return;
    if (read == ItemRead::Usage ||
        (numbers.size() == 1 && (!parse_uint(numbers[0].c_str(), 65535, &count) || count == 0))) {
        command_reply_line("usage: give <item name> [count]");
        return;
    }
    EmfItemInfo info{};
    info.size = sizeof(info);
    c.api->item_info(w, id, &info);
    const uint32_t player = c.player ? c.player() : 0;
    const uint32_t pickup = c.api->give_item(w, player, id, count);
    command_reply_line(pickup ? fmt("gave %u x %s to player %u", count, item_label(c.api, w, id, info.name, from_game_chat()).c_str(), player)
                              : std::string("give failed (see the log)"));
}

// put's and take's result arrives after the next server step, when the command has returned: it goes to the reply the
// command had (the console, or the chat player who typed it), kept in this per-request record.
struct LateItems {
    BuiltinContext* c;
    ReplyFn reply;
    bool game_chat = false;  // typed in the game's chat: the answer names the item in the player's language
};

void items_done(EmfWorld* w, uint32_t entity, uint32_t item, uint32_t requested, uint32_t n, void* user, bool take) {
    const std::unique_ptr<LateItems> late(static_cast<LateItems*>(user));  // each accepted request ends exactly once
    if (!late->reply) return;
    EmfItemInfo info{};
    info.size = sizeof(info);
    const bool named = late->c->api->item_info(w, item, &info) && info.name[0];
    const std::string why = InventoryService::delivering_reason();
    late->reply(fmt(take ? "take %u x %s from entity %u: %u taken%s" : "put %u x %s into entity %u: %u added%s",
                    requested, named ? item_label(late->c->api, w, item, info.name, late->game_chat).c_str() : fmt("0x%08x", item).c_str(), entity, n,
                    why.empty() ? "" : (" (" + why + ")").c_str()));
}
void put_done(EmfWorld* w, uint32_t entity, uint32_t item, uint32_t requested, uint32_t added, void* user) {
    items_done(w, entity, item, requested, added, user, false);
}
void take_done(EmfWorld* w, uint32_t entity, uint32_t item, uint32_t requested, uint32_t taken, void* user) {
    items_done(w, entity, item, requested, taken, user, true);
}

// put <item> [count] [entity] [level] [rarity] / take <item> [count] [entity]
void cmd_items(EmfWorld* w, int argc, const char* const* argv, void* u, bool take) {
    auto& c = ctx_of(u);
    uint32_t count = 1, entity = 0, level = 0, rarity = 0xFF, id = 0;
    std::vector<std::string> numbers;
    const ItemRead read = read_item(c, w, take ? "take" : "put", argc, argv, take ? 2 : 4, &id, &numbers);
    if (read == ItemRead::Replied) return;
    bool ok = read == ItemRead::Ok;
    if (ok && numbers.size() >= 1) ok = parse_uint(numbers[0].c_str(), 65535, &count) && count > 0;
    if (ok && numbers.size() >= 2) ok = parse_uint(numbers[1].c_str(), 0xffffffffu, &entity) && entity > 0;
    if (ok && numbers.size() >= 3) ok = parse_uint(numbers[2].c_str(), 1000, &level);
    if (ok && numbers.size() >= 4) ok = parse_uint(numbers[3].c_str(), 4, &rarity);
    if (!ok) {
        command_reply_line(take ? "usage: take <item name> [count 1-65535] [entity]"
                                : "usage: put <item name> [count 1-65535] [entity] [level] [rarity 0-4]");
        return;
    }
    if (!entity) entity = c.player ? c.player() : 0;
    auto* late = new LateItems{&c, current_reply(), from_game_chat()};  // freed by the result callback, or below when refused
    bool queued = false;
    if (!take && numbers.size() < 3) {
        queued = c.api->put_items(w, entity, id, count, put_done, late);
    } else {
        EmfItemsRequest r{};
        r.size = sizeof(r);
        r.entity = entity;
        r.item = id;
        r.count = count;
        r.level = level;
        if (rarity != 0xFF) {
            r.flags = EMF_ITEMS_RARITY;
            r.rarity = static_cast<uint8_t>(rarity);
        }
        r.cb = take ? take_done : put_done;
        r.user = late;
        queued = take ? c.api->take_items(w, &r) : c.api->put_items_ex(w, &r);
    }
    if (!queued) {
        delete late;  // a refused request never calls back
        command_reply_line(take ? "take refused (see the log)" : "put refused (see the log)");
    }
}
void cmd_put(EmfWorld* w, int argc, const char* const* argv, void* u) { cmd_items(w, argc, argv, u, false); }
void cmd_take(EmfWorld* w, int argc, const char* const* argv, void* u) { cmd_items(w, argc, argv, u, true); }

bool parse_coord(const char* s, double* out);  // goto's, below

// spawn <item name | template guid> [count] [at <x> <y> <z>]: what a placeable item puts down (or a template), 2.5 m in
// front of you, or at a place in metres written any way (a dedicated server's console has no player of its own).
void cmd_spawn(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    const char* usage = "usage: spawn <item name | template guid> [count 1-20] [at <x> <y> <z>]";
    std::vector<std::string> words(argv + 1, argv + argc);
    const auto at_word = std::find_if(words.begin(), words.end(), [](const std::string& s) { return lower(s) == "at"; });
    const bool at = at_word != words.end();
    const std::vector<std::string> what(words.begin(), at_word);
    const std::vector<std::string> where = at ? emf::coordinate_words({at_word + 1, words.end()}) : std::vector<std::string>{};
    uint8_t guid[16] = {};
    uint32_t count = 1;
    bool ok = !what.empty();
    if (ok && emf::parse_guid(what[0], guid)) {
        ok = what.size() == 1 || (what.size() == 2 && parse_uint(what[1].c_str(), 20, &count) && count != 0);
    } else if (ok) {  // an item's name: the template it places
        std::vector<const char*> av{"spawn"};
        for (const auto& word : what) av.push_back(word.c_str());
        uint32_t id = 0;
        std::vector<std::string> numbers;
        const ItemRead read = read_item(c, w, "spawn", static_cast<int>(av.size()), av.data(), 1, &id, &numbers);
        if (read == ItemRead::Replied) return;
        ok = read == ItemRead::Ok && (numbers.empty() || (parse_uint(numbers[0].c_str(), 20, &count) && count != 0));
        if (ok) {
            uint64_t size = 0;
            const auto* info = c.api->item_data ? static_cast<const uint8_t*>(c.api->item_data(w, id, &size)) : nullptr;
            if (info && size >= off::kItemInfoPlacedEntity + 16) std::memcpy(guid, info + off::kItemInfoPlacedEntity, 16);
            if (std::all_of(std::begin(guid), std::end(guid), [](uint8_t v) { return v == 0; })) {
                EmfItemInfo ii{};
                ii.size = sizeof(ii);
                c.api->item_info(w, id, &ii);
                command_reply_line(fmt("%s places nothing: spawn takes an item you can place (items <name> shows "
                                       "\"places\"), or a template guid",
                                       item_label(c.api, w, id, ii.name, from_game_chat()).c_str()));
                return;
            }
        }
    }
    double p[3] = {};
    if (ok && at)
        ok = where.size() == 3 && parse_coord(where[0].c_str(), &p[0]) && parse_coord(where[1].c_str(), &p[1]) &&
             parse_coord(where[2].c_str(), &p[2]);
    if (!ok) {
        command_reply_line(usage);
        return;
    }
    if (!at) {
        const uint32_t player = c.player ? c.player() : 0;
        if (!player_pos(c, w, player, p)) {
            command_reply_line("spawn: player position unknown (give a place: spawn <name> at <x> <y> <z>)");
            return;
        }
        double yaw = -1;  // in front of you: the camera's direction on this game, else beside you
        EmfPlayerView v{};
        v.size = sizeof v;
        if (c.api->size >= offsetof(EmfApi, player_view) + sizeof(c.api->player_view) && c.api->player_view &&
            c.api->local_player && player == c.api->local_player() && c.api->player_view(&v))
            yaw = (v.flags & EMF_VIEW_CAMERA) ? v.camera_yaw_deg : v.heading_deg;  // your own game's view only
        if (yaw >= -360 && yaw <= 360 && yaw != -1) {
            p[0] += 2.5 * std::sin(yaw * 3.14159265358979 / 180.0);
            p[2] += 2.5 * std::cos(yaw * 3.14159265358979 / 180.0);
        } else {
            p[0] += 2.0;  // beside the player, not inside
        }
    }
    std::string ids;
    uint32_t made = 0;
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t id = c.api->spawn(w, guid, p[0] + 1.5 * k, p[1], p[2]);
        if (!id) continue;
        ++made;
        ids += (ids.empty() ? "" : " ") + std::to_string(id);
    }
    command_reply_line(fmt("spawned %u: %s", made, ids.empty() ? "(none, see the log)" : ids.c_str()));
}

void cmd_time(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    double hours = 0;
    if (argc != 2 || !parse_hours(argv[1], &hours)) {
        command_reply_line("usage: time <hours 0-24>");
        return;
    }
    auto* daytime = static_cast<uint8_t*>(c.api->singleton(w, "g38_daytime::Daytime"));
    auto* weather = static_cast<uint8_t*>(c.api->singleton(w, "WeatherSystem"));
    auto fn = reinterpret_cast<SetTimeOfDayFn>(c.api->resolve("SetTimeOfDay"));
    if (!daytime || !fn) {
        command_reply_line("time: the daytime system or setTimeOfDay is not available here");
        return;
    }
    const CoreState* st = core_state();
    if (st && !st->tick_offsets_ok) {  // setTimeOfDay needs the world's time
        command_reply_line("time: the world's tick fields are not confirmed on this game build");
        return;
    }
    const bool known = st && st->known_build;
    const auto r = set_time_of_day(daytime, weather, fn, c.api->ecs_time_ns(w), hours, known);
    if (!r.ok) {
        command_reply_line("time: setTimeOfDay faulted");
        return;
    }
    command_reply_line(fmt("time of day is now %.2f h (was %.2f h)%s", r.new_tod / 3600e9, r.old_tod / 3600e9,
                           known ? "" : "; weather not reset (a game build EMF does not know)"));
}

// perf: how long each world step took since the last perf (or the start): the game's own steps, EMF's wait for the
// server world's jobs and each mod (its tick callbacks and posted work), per step on average, the worst step and the
// share of the time. Then the numbers start again.
void cmd_perf(EmfWorld*, int, const char* const*, void*) {
    CoreState* state = core_state();
    if (!state) return;
    const uint64_t now = GetTickCount64();
    const double seconds = static_cast<double>(now - state->perf_since_ms.exchange(now)) / 1000.0;
    const auto rows = state->perf.take();
    char head[160];
    std::snprintf(head, sizeof head, "perf: the last %.1f s, per world step (counted again from now):", seconds);
    command_reply_line(head);
    if (rows.empty()) {
        command_reply_line("  no world step ran meanwhile");
        return;
    }
    for (const auto& line : emf::perf_lines(rows, seconds)) command_reply_line("  " + line);
}

void cmd_pos(EmfWorld* w, int, const char* const*, void* u) {
    auto& c = ctx_of(u);
    const uint32_t player = c.player ? c.player() : 0;
    double p[3];
    if (!player_pos(c, w, player, p)) {
        command_reply_line("pos: player position unknown");
        return;
    }
    command_reply_line(fmt("player %u at (%.1f, %.1f, %.1f) m", player, p[0], p[1], p[2]));
}

// goto [#k] <x> <y> <z> [ground] | goto [#k] <entity> [ground]: teleports you (the chat sender, else the local player;
// #k = the k-th player, lowest entity id first) to a position, or 1.5 m beside an entity (API v20 teleport). The result
// comes one or more server steps later, to the same reply.
struct LateGoto {
    ReplyFn reply;
};
void goto_done(EmfWorld*, const EmfTeleportResult* r, void* user) {
    auto* late = static_cast<LateGoto*>(user);
    if (late->reply) {
        if (r->status == EMF_TELEPORT_FAILED)
            late->reply(fmt("goto: entity %u not moved: %s", r->entity, r->reason ? r->reason : "?"));
        else
            late->reply(fmt("goto: entity %u now at (%.1f, %.1f, %.1f), %.1f m from the target%s", r->entity, r->x, r->y,
                            r->z, r->distance,
                            (r->flags & EMF_TELEPORT_RAW_TARGET)   ? " (no ground found near it: exactly there)"
                            : (r->flags & EMF_TELEPORT_OFF_TARGET) ? " (the ground search moved it)"
                                                                   : ""));
    }
    if (r->flags & EMF_TELEPORT_LAST) delete late;
}

bool parse_coord(const char* s, double* out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (!end || *end || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

void cmd_goto(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    std::vector<std::string> words = emf::coordinate_words({argv + 1, argv + argc});
    uint32_t k = 0;
    if (!words.empty() && words[0].size() > 1 && words[0][0] == '#') {
        if (!parse_uint(words[0].c_str() + 1, 1023, &k) || k == 0) words.clear();  // a bad #k: usage
        else words.erase(words.begin());
    }
    bool ground = false;
    if (!words.empty() && lower(words.back()) == "ground") {
        ground = true;
        words.pop_back();
    }
    double v[3] = {};
    bool ok = words.size() == 1 || words.size() == 3;
    for (size_t i = 0; ok && i < words.size(); ++i) ok = parse_coord(words[i].c_str(), &v[i]);
    if (!ok) {
        command_reply_line("usage: goto [#k] <x> <y> <z> [ground] | goto [#k] <entity> [ground]");
        return;
    }
    if (c.api->size < offsetof(EmfApi, teleport) + sizeof(c.api->teleport) || !c.api->teleport) {
        command_reply_line("goto: this core has no teleport");
        return;
    }
    uint32_t player = 0;
    if (k) {
        uint32_t seen = 0;
        for (uint32_t id = 1; id <= 1023 && !player; ++id)
            if (comp_of<emf::gen::ecs::PlayerState>(c, w, id) && ++seen == k) player = id;
        if (!player) {
            command_reply_line(fmt("goto: there is no player #%u", k));
            return;
        }
    } else {
        player = c.player ? c.player() : 0;
        if (!player) {
            command_reply_line("goto: no player to move (on a dedicated server's console name one: goto #1 ...)");
            return;
        }
    }
    double to[3] = {v[0], v[1], v[2]};
    if (words.size() == 1) {
        const double e = v[0];
        if (e < 1 || e > 4294967295.0 || e != std::floor(e)) {
            command_reply_line("goto: an entity is a whole number");
            return;
        }
        if (!player_pos(c, w, static_cast<uint32_t>(e), to)) {
            command_reply_line(fmt("goto: entity %u has no position", static_cast<uint32_t>(e)));
            return;
        }
        to[0] += 1.5;  // beside it, not inside it
    }
    EmfTeleport t{};
    t.size = sizeof t;
    t.entity = player;
    t.x = to[0];
    t.y = to[1];
    t.z = to[2];
    t.flags = ground ? EMF_TELEPORT_FIND_GROUND : 0u;
    t.cb = goto_done;
    t.user = new LateGoto{current_reply()};
    if (!c.api->teleport(w, &t)) {
        delete static_cast<LateGoto*>(t.user);  // a refused request never calls back
        command_reply_line("goto: refused (see the log)");
        return;
    }
    command_reply_line(fmt("goto: moving player %u to (%.1f, %.1f, %.1f)%s", player, to[0], to[1], to[2],
                           ground ? " with the ground search" : ""));
}

// "keen::ecs::ActiveNpcState" -> "ActiveNpcState"
const char* short_name(const char* qualified) {
    const char* p = std::strrchr(qualified, ':');
    return p ? p + 1 : qualified;
}

void cmd_inspect(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    uint32_t id = 0;
    if (argc != 2 || !parse_uint(argv[1], 0xffffffffu, &id)) {
        command_reply_line("usage: inspect <entity>");
        return;
    }
    size_t total = 0, templates = 0;
    auto dynamic = [&](const emf::gen::ComponentInfo& ci) {
        uint64_t size = 0;
        if (!c.api->get_component(w, ci.hash, id, &size)) return;
        ++total;
        command_reply_line(fmt("%s (%llu B)", short_name(ci.name), static_cast<unsigned long long>(size)));
    };
    for (const auto& ci : emf::gen::kComponents) {
        dynamic(ci);
        uint64_t tsize = 0;
        if (c.api->get_template_component && c.api->get_template_component(w, ci.hash, id, &tsize)) {
            ++total;
            ++templates;
            command_reply_line(tsize ? fmt("%s (template, %llu B)", short_name(ci.name), static_cast<unsigned long long>(tsize))
                                     : fmt("%s (template, size unknown)", short_name(ci.name)));
        }
    }
    for (const auto& ci : emf::gen::kDynamicComponents) dynamic(ci);
    command_reply_line(total ? fmt("entity %u: %zu components (%zu template)", id, total, templates)
                             : fmt("entity %u: no components (no such entity?)", id));
}

const emf::gen::ComponentInfo* find_component(const char* name) {
    for (const auto& ci : emf::gen::kComponents)
        if (lower(short_name(ci.name)) == lower(name)) return &ci;
    return nullptr;
}

bool parse_offset(const char* s, uint32_t* out) {
    if (!s || !*s || *s == '-') return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s, &end, 0);  // 0x.. or decimal
    if (!end || *end || v > 0xffffu) return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

// The entity's template data for a component, with the reply for each way it can be missing. Null if missing.
const emf::gen::ComponentInfo* template_target(BuiltinContext& c, EmfWorld* w, uint32_t id, const char* name,
                                               uint8_t** data) {
    const emf::gen::ComponentInfo* info = find_component(name);
    if (!info) {
        command_reply_line(fmt("unknown component '%s'", name));
        return nullptr;
    }
    uint64_t game_size = 0;
    *data = c.api->get_template_component
                ? static_cast<uint8_t*>(c.api->get_template_component(w, info->hash, id, &game_size))
                : nullptr;
    if (!*data) {
        command_reply_line(fmt("entity %u has no template data for %s", id, short_name(info->name)));
        return nullptr;
    }
    if (game_size && game_size != info->size) {
        command_reply_line(fmt("%s is %llu B in this game but %u B in EMF's headers (a game update changed it): refused",
                               short_name(info->name), static_cast<unsigned long long>(game_size), info->size));
        return nullptr;
    }
    return info;
}

void cmd_tpl(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    uint32_t id = 0, bytes = 32, offset = 0;
    if (argc < 3 || argc > 5 || !parse_uint(argv[1], 0xffffffffu, &id) ||
        (argc >= 4 && (!parse_uint(argv[3], 256, &bytes) || bytes == 0)) || (argc == 5 && !parse_offset(argv[4], &offset))) {
        command_reply_line("usage: tpl <entity> <component> [bytes 1-256] [offset]");
        return;
    }
    uint8_t* p = nullptr;
    const auto* info = template_target(c, w, id, argv[2], &p);
    if (!info) return;
    if (offset >= info->size) {
        command_reply_line(fmt("offset out of range (%s is %u B)", short_name(info->name), info->size));
        return;
    }
    if (offset + bytes > info->size) bytes = info->size - offset;
    command_reply_line(fmt("%s of entity %u at %p (%u of %u B from +0x%03x):", short_name(info->name), id,
                           static_cast<const void*>(p), bytes, info->size, offset));
    for (uint32_t row = offset; row < offset + bytes; row += 16) {
        std::string line = fmt("+0x%03x:", row);
        for (uint32_t i = row; i < row + 16 && i < offset + bytes; ++i) {
            uint8_t b = 0;
            line += emf::seh_read(p + i, &b, 1) ? fmt(" %02x", b) : std::string(" ??");
        }
        command_reply_line(line);
    }
}

// One value of a tplset type, as raw bytes and as text.
struct Scalar {
    uint8_t bytes[4] = {};
    uint32_t size = 0;
};
bool parse_scalar(const std::string& type, const char* text, Scalar* out) {
    char* end = nullptr;
    if (type == "f32") {
        const float f = std::strtof(text, &end);
        if (!end || *end || !std::isfinite(f)) return false;
        std::memcpy(out->bytes, &f, 4);
        out->size = 4;
        return true;
    }
    if (type == "i32") {
        const long long v = std::strtoll(text, &end, 0);
        if (!end || *end || v < INT32_MIN || v > INT32_MAX) return false;
        const int32_t x = static_cast<int32_t>(v);
        std::memcpy(out->bytes, &x, 4);
        out->size = 4;
        return true;
    }
    const uint64_t limit = type == "u32" ? 0xffffffffull : type == "u16" ? 0xffffull : type == "u8" ? 0xffull : 0;
    if (!limit || *text == '-') return false;
    const unsigned long long v = std::strtoull(text, &end, 0);
    if (!end || *end || v > limit) return false;
    const uint32_t x = static_cast<uint32_t>(v);
    out->size = type == "u32" ? 4 : type == "u16" ? 2 : 1;
    std::memcpy(out->bytes, &x, out->size);
    return true;
}
std::string scalar_text(const std::string& type, const uint8_t* b) {
    if (type == "f32") {
        float f;
        std::memcpy(&f, b, 4);
        return fmt("%g", f);
    }
    if (type == "i32") {
        int32_t v;
        std::memcpy(&v, b, 4);
        return fmt("%d", v);
    }
    uint32_t v = 0;
    std::memcpy(&v, b, type == "u32" ? 4 : type == "u16" ? 2 : 1);
    return fmt("%u", v);
}

void cmd_tplset(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    uint32_t id = 0, offset = 0;
    const std::string type = argc == 6 ? lower(argv[4]) : std::string();
    const bool type_ok = type == "f32" || type == "i32" || type == "u32" || type == "u16" || type == "u8";
    if (argc != 6 || !parse_uint(argv[1], 0xffffffffu, &id) || !parse_offset(argv[3], &offset) || !type_ok) {
        command_reply_line("usage: tplset <entity> <component> <offset> <f32|i32|u32|u16|u8> <value>");
        return;
    }
    Scalar value;
    if (!parse_scalar(type, argv[5], &value)) {
        command_reply_line(fmt("bad value '%s' for %s", argv[5], type.c_str()));
        return;
    }
    uint8_t* p = nullptr;
    const auto* info = template_target(c, w, id, argv[2], &p);
    if (!info) return;
    if (offset + value.size > info->size) {
        command_reply_line(fmt("offset out of range (%s is %u B)", short_name(info->name), info->size));
        return;
    }
    uint8_t before[4] = {}, after[4] = {};
    emf::seh_read(p + offset, before, value.size);
    if (!c.api->write_memory || !c.api->write_memory(p + offset, value.bytes, value.size)) {
        command_reply_line("could not write the template data (see the log)");
        return;
    }
    emf::seh_read(p + offset, after, value.size);
    command_reply_line(fmt("%s+0x%03x of entity %u: %s %s -> %s (every entity of this template, until the game restarts)",
                           short_name(info->name), offset, id, type.c_str(), scalar_text(type, before).c_str(),
                           scalar_text(type, after).c_str()));
}

void cmd_stack(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    uint32_t max = 0, id = 0;
    std::vector<std::string> numbers;
    const ItemRead read = read_item(c, w, "stack", argc, argv, 1, &id, &numbers);
    if (read == ItemRead::Replied) return;
    if (read == ItemRead::Usage || numbers.size() != 1 || !parse_uint(numbers[0].c_str(), 65535, &max) || max == 0) {
        command_reply_line("usage: stack <item name> <max 1-65535>");
        return;
    }
    EmfItemInfo info{};
    info.size = sizeof(info);
    c.api->item_info(w, id, &info);
    const unsigned before = info.max_stack;
    if (!c.api->set_item_max_stack || !c.api->set_item_max_stack(id, static_cast<uint16_t>(max))) {
        command_reply_line(fmt("%s: could not change the stack size (see the log)", item_label(c.api, w, id, info.name, from_game_chat()).c_str()));
        return;
    }
    command_reply_line(fmt("%s: stack %u -> %u (until the game restarts)", item_label(c.api, w, id, info.name, from_game_chat()).c_str(), before, max));
}

void cmd_entities(EmfWorld* w, int, const char* const*, void* u) {
    size_t n = 0;
    ctx_of(u).api->for_each_entity(w, [](uint32_t, void* p) { ++*static_cast<size_t*>(p); }, &n);
    command_reply_line(fmt("%zu entities in this world", n));
}

// Removes one entity (host). Refuses players (the local one, or anything with PlayerState) and ids that are not in the
// world, so a typo cannot delete a player or poke the game with a stale id.
void cmd_destroy(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    uint32_t id = 0;
    if (argc != 2 || !parse_uint(argv[1], 0xffffffffu, &id) || id == 0) {
        command_reply_line("usage: destroy <entity>");
        return;
    }
    struct Find {
        uint32_t id;
        bool found;
    } find{id, false};
    c.api->for_each_entity(w, [](uint32_t e, void* p) {
        auto* f = static_cast<Find*>(p);
        f->found |= e == f->id;
    }, &find);
    if (!find.found) {
        command_reply_line(fmt("no entity %u in this world", id));
        return;
    }
    uint64_t size = 0;
    const uint32_t player = c.player ? c.player() : 0;
    if (id == player || c.api->get_component(w, emf::gen::ecs::PlayerState::kHash, id, &size)) {
        command_reply_line(fmt("entity %u is a player: not destroyed", id));
        return;
    }
    command_reply_line(c.api->destroy(w, id) ? fmt("destroyed entity %u", id)
                                             : fmt("destroy %u failed (see the log)", id));
}

// path <x> <y> <z> [jumps] [closest] (v37): a path from you; the answer comes after a step or more.
struct LatePath {
    ReplyFn reply;
};
void path_done(EmfWorld*, uint32_t id, const EmfPathResult* r, const EmfPathPoint* points, void* user) {
    const std::unique_ptr<LatePath> late(static_cast<LatePath*>(user));
    if (late->reply && r) late->reply(path_answer_text(id, *r, points));
}
void cmd_path(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    EmfPathQuery q{};
    q.size = sizeof q;
    const std::vector<std::string> words = emf::coordinate_words({argv + 1, argv + argc});
    bool ok = words.size() >= 3 && words.size() <= 6;
    for (size_t i = 0; ok && i < 3; ++i) {
        char* end = nullptr;
        q.to[i] = std::strtof(words[i].c_str(), &end);
        ok = end && *end == 0 && end != words[i].c_str();
    }
    for (size_t i = 3; ok && i < words.size(); ++i) {
        const std::string& a = words[i];
        uint32_t nodes = 0;
        if (a == "jumps") q.moves |= EMF_PATH_JUMP_UP | EMF_PATH_JUMP_DOWN | EMF_PATH_JUMP_ACROSS;
        else if (a == "closest") q.closest = 1;
        else if (!q.max_nodes && parse_uint(a.c_str(), kPathMaxNodes, &nodes) && nodes) q.max_nodes = nodes;
        else ok = false;
    }
    if (!ok) {
        command_reply_line("usage: path <x> <y> <z> [jumps] [closest] [nodes 1-20000]");
        return;
    }
    const uint32_t player = c.player ? c.player() : 0;
    double at[3] = {};
    if (!player || !player_pos(c, w, player, at)) {
        command_reply_line("path: you have no position in this world");
        return;
    }
    for (int i = 0; i < 3; ++i) q.from[i] = static_cast<float>(at[i]);
    q.owner = player;
    auto* late = new LatePath{current_reply()};
    const uint32_t id = c.api->path_find ? c.api->path_find(w, &q, path_done, late) : 0;
    if (!id) {
        delete late;
        command_reply_line("path not asked (see the log: path_find refused ...)");
        return;
    }
    command_reply_line(fmt("path %u: asked from (%.1f, %.1f, %.1f); the answer follows", id, at[0], at[1], at[2]));
}

// door <entity> [open|close|lock|unlock] [front|back] (v38): a door's state, or a change at the next step.
std::string door_text(uint32_t st) {
    if (!(st & (EMF_DOOR_OPEN | EMF_DOOR_CLOSED))) return "not a door";
    std::string s = (st & EMF_DOOR_OPEN) ? "open" : "closed";
    if (st & EMF_DOOR_LOCKED) s += ", locked";
    if (st & EMF_DOOR_LOGIC) s += ", driven by a switch";
    return s;
}
void cmd_door(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    uint32_t id = 0;
    std::string act = argc >= 3 ? argv[2] : "";
    const std::string side_word = argc >= 4 ? argv[3] : "";
    const bool acts = act == "open" || act == "close" || act == "lock" || act == "unlock";
    const bool side_ok = side_word.empty() || (act == "open" && (side_word == "front" || side_word == "back"));
    if (argc < 2 || argc > 4 || !parse_uint(argv[1], 0xffffffffu, &id) || !id || (argc >= 3 && !acts) || !side_ok) {
        command_reply_line("usage: door <entity> [open|close|lock|unlock] [front|back]");
        return;
    }
    if (act.empty()) {
        command_reply_line(fmt("entity %u: %s", id, door_text(c.api->door_state ? c.api->door_state(w, id) : 0).c_str()));
        return;
    }
    bool ok = false;
    if (act == "open" || act == "close") {
        const uint32_t side = side_word == "front" ? 1u : side_word == "back" ? 2u : 0u;
        ok = c.api->door_set && c.api->door_set(w, id, act == "open" ? 1u : 0u, side);
        if (!ok) {
            command_reply_line(fmt("door %u: not changed (see the log: door_set refused ...)", id));
            return;
        }
    } else {
        ok = c.api->door_lock && c.api->door_lock(w, id, act == "lock" ? 1u : 0u);
        if (!ok) {
            command_reply_line(fmt("door %u: not changed (see the log: door_lock refused ...)", id));
            return;
        }
    }
    command_reply_line(fmt("door %u: %s at the next step", id, act == "open" ? "opens" : act == "close" ? "closes"
                                                               : act == "lock" ? "locked" : "unlocked"));
}

// remove <entity> [drop|<player>] [force] (v36): a placed object taken away like a pick-up, its item given back.
void cmd_remove(EmfWorld* w, int argc, const char* const* argv, void* u) {
    auto& c = ctx_of(u);
    EmfRemoveBuilding r{};
    r.size = sizeof(r);
    bool ok = argc >= 2 && argc <= 4 && parse_uint(argv[1], 0xffffffffu, &r.entity) && r.entity != 0;
    for (int i = 2; ok && i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "force" && !(r.flags & EMF_REMOVE_FORCE)) {
            r.flags |= EMF_REMOVE_FORCE;
        } else if (a == "drop" && r.refund == EMF_REFUND_NONE) {
            r.refund = EMF_REFUND_DROP;
        } else if ((lower(a) == "me" || lower(a) == "player") && r.refund == EMF_REFUND_NONE && c.player && c.player()) {
            r.refund = EMF_REFUND_PLAYER;  // whoever typed it
            r.player = c.player();
        } else if (r.refund == EMF_REFUND_NONE && parse_uint(a.c_str(), 0xffffffffu, &r.player) && r.player) {
            r.refund = EMF_REFUND_PLAYER;
        } else {
            ok = false;
        }
    }
    if (!ok) {
        command_reply_line("usage: remove <entity> [me|drop|<player>] [force]");
        return;
    }
    if (!c.api->remove_building(w, &r)) {
        command_reply_line(fmt("entity %u not removed: %s", r.entity, r.why));
        return;
    }
    command_reply_line(!r.refunded                      ? fmt("removed entity %u (no item back)", r.entity)
                       : r.refund == EMF_REFUND_DROP     ? fmt("removed entity %u; the item came back as a drop", r.entity)
                                                         : fmt("removed entity %u; the item came back to player %u (a drop "
                                                               "at the object if it did not fit)", r.entity, r.player));
    if (r.flags & EMF_REMOVE_FORCE) command_reply_line("anything it held lies on the ground where it stood");
}

}  // namespace

bool add_builtin_commands(BuiltinContext& ctx) {
    if (!ctx.api || !ctx.center) return false;
    struct Def {
        const char* name;
        const char* help;
        CommandFn fn;
    };
    const Def defs[] = {
        {"help", "[page]: list the commands (in the chat 8 per page)", cmd_help},
        {"mods", ": list the mods and what became of them", cmd_mods},
        {"settings", "<mod>: list a mod's settings", cmd_settings},
        {"hooks", ": every inline hook, its owner and target (EMF's and mods')", cmd_hooks},
        {"pattern", "<bytes> [.section]: test a byte pattern against the game exe (found once?)", cmd_pattern},
        {"set", "<mod> <key> <value>: change a mod's setting (saved in emf/config/<mod>.ini)", cmd_set},
        {"items", "[words]: find the game's items by name (words in any order, part of a word is enough)", cmd_items},
        {"give", "<item name> [count]: drop items at your feet (host)", cmd_give},
        {"put", "<item name> [count 1-65535] [entity] [level] [rarity 0-4]: put items straight into an inventory, yours by "
                "default (host)",
         cmd_put},
        {"take", "<item name> [count 1-65535] [entity]: take items out of an inventory, yours by default (host)", cmd_take},
        {"spawn", "<item name | template guid> [count 1-20] [at <x> <y> <z>]: what a placeable item puts down (or a template), in front of you or at a place (host)",
         cmd_spawn},
        {"time", "<hours 0-24>: set the time of day (host)", cmd_time},
        {"pos", ": your position", cmd_pos},
        {"perf", ": how long the game's world steps, EMF's wait and each mod took per step since the last perf",
         cmd_perf},
        {"goto", "[#k] <x> <y> <z> | <entity> [ground] (host)",
         cmd_goto},
        {"near", "[radius 1-200]: the entities around you, nearest first, and what they are (host)", cmd_near},
        {"hud", "<id> [place 0-2] [text]: keep a line on screen (0 top right, 1 bottom right, 2 bottom left); no text removes it", cmd_hud},
        {"inv", "<entity>: every stack in the entity's inventories, with slot ids and gear levels (host)", cmd_inv},
        {"entities", ": count the entities in the server world", cmd_entities},
        {"inspect", "<entity>: list the entity's components (template ones marked)", cmd_inspect},
        {"tpl", "<entity> <component> [bytes 1-256] [offset]: hex dump of the entity's template data", cmd_tpl},
        {"tplset", "<entity> <component> <offset> <f32|i32|u32|u16|u8> <value>: change one template value (host)",
         cmd_tplset},
        {"stack", "<item name> <max 1-65535>: change an item's stack size until the game restarts (host)", cmd_stack},
        {"door", "<entity> [open|close|lock|unlock] [front|back]: a door's state, or open, close or lock it (host)",
         cmd_door},
        {"path", "<x> <y> <z> [jumps] [closest] [nodes]: a path from you on the game's path grid (it moves nothing; host)",
         cmd_path},
        {"remove", "<entity> [me|drop|<player>] [force]: take a placed object away like a pick-up, its item back to you, as a "
                   "drop or to a player; refused for a chest with items unless force (they drop on the ground), and for a Flame "
                   "Altar (host)", cmd_remove},
        {"destroy", "<entity>: remove an entity from the world, never a player (host)", cmd_destroy},
    };
    bool ok = true;
    for (const auto& d : defs) {
        const bool anywhere = std::strcmp(d.name, "help") == 0 || std::strcmp(d.name, "mods") == 0 ||
                              std::strcmp(d.name, "settings") == 0 || std::strcmp(d.name, "set") == 0 ||
                              std::strcmp(d.name, "hooks") == 0 || std::strcmp(d.name, "pattern") == 0;  // need no world
        // help is for every player (it lists only what they may run)
        const uint32_t flags = std::strcmp(d.name, "help") == 0 ? kCommandForPlayers : 0u;
        ok &= ctx.center->add(d.name, d.help, d.fn, &ctx, nullptr, anywhere, flags);
    }
    return ok;
}

}  // namespace emf::core
