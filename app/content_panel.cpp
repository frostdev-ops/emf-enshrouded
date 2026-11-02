// EMF.exe's card "EML content" (content_panel.h): the loader's own rules, run on the game folder as they will run at
// the next start. The facts are gathered in the order of content_start.h's collect_facts (the first check that says no
// ends it), the decision is content_owner.h's decide_content, and the state is judged as content_start.h's
// plan_content judges it.
#include "content_panel.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <map>

#include "core/json.h"
#include "core/known_tools.h"
#include "game_folder.h"
#include "json_out.h"
#include "loader/chain_list.h"
#include "loader/content_base.h"
#include "loader/content_fingerprint.h"
#include "loader/content_owner.h"
#include "loader/content_shadow.h"
#include "loader/content_state.h"
#include "loader/eml_mods.h"
#include "loader/file_hooks.h"  // shadow_facts: the redirect's own check of shadow.txt
#include "saves/backup.h"       // size_text

#ifndef EMF_VERSION
#define EMF_VERSION "dev"
#endif

namespace emf::app {

namespace {

namespace ld = emf::loader;
namespace cio = emf::loader::cio;
using VT = core::json::Value::Type;

constexpr const char* kFileName = "enshrouded";  // the game client's data: enshrouded.kfc, enshrouded_NNN.dat

int64_t unix_seconds(uint64_t filetime) {
    constexpr uint64_t k1970 = 116444736000000000ull;
    return filetime > k1970 ? static_cast<int64_t>((filetime - k1970) / 10000000ull) : 0;
}

// "YYYY-MM-DDTHH:MM:SSZ" (the state's built_at) as a FILETIME number; 0 when it does not read.
uint64_t filetime_of(const std::string& utc) {
    SYSTEMTIME st{};
    if (sscanf_s(utc.c_str(), "%4hu-%2hu-%2huT%2hu:%2hu:%2huZ", &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute,
                    &st.wSecond) != 6)
        return 0;
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// "8.5 s", "28 s": a build's time as the loader's log line gives it (content_start.h seconds_text).
std::string seconds_text(uint64_t ms) {
    if (ms < 10000) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f s", static_cast<double>(ms) / 1000.0);
        return buf;
    }
    return std::to_string((ms + 500) / 1000) + " s";
}

bool is_file(const std::wstring& path) {
    const cio::Stat s = cio::stat(path);
    return s.exists && !s.dir;
}

// The mod's "name" in its mod.json ("" when it has none); the loader has checked the file already.
std::string mod_name(const std::wstring& folder) {
    const auto text = cio::read_file(cio::join(folder, L"mod.json"), ld::kMaxManifestBytes);
    if (!text) return {};
    const auto parsed = core::json::parse(*text);
    if (!parsed.value || !parsed.value->is(VT::Object)) return {};
    const auto* n = parsed.value->get("name");
    return n && n->is(VT::String) ? n->s : std::string();
}

// "off (why)" -> "why": the loader's line without its frame, for the card.
std::string inner(const std::string& why) {
    if (why.rfind("off (", 0) == 0 && !why.empty() && why.back() == ')') return why.substr(5, why.size() - 6);
    return why;
}

// The bytes of the shadow's own files (one name each), never its links to the game's files; nothing is followed.
uint64_t own_bytes(const std::wstring& dir, int depth) {
    uint64_t sum = 0;
    if (depth > 32) return sum;
    for (const auto& e : ld::detail::list_dir(dir)) {
        if (e.attrs & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        const std::wstring path = cio::join(dir, e.name);
        if (e.attrs & FILE_ATTRIBUTE_DIRECTORY) {
            sum += own_bytes(path, depth + 1);
            continue;
        }
        const auto id = ld::shadow_detail::file_id(path);
        if (id.ok && id.links == 1) sum += cio::stat(path).size;
    }
    return sum;
}

// "" when the redirect will take the shadow copy at the next start (dllmain.cpp's record_ok): its record, shadow.txt,
// matches the game's files, and its copies and links are whole.
std::string copy_problem(const std::wstring& game, const std::wstring& shadow, uint32_t dat_count) {
    const auto normal = ld::normal_game_dir(game);
    if (!normal) return "the game folder's path is not one the redirect can read";
    if (const std::string p = ld::shadow_problem(ld::file_hooks::shadow_facts(*normal)); !p.empty()) return p;
    return ld::shadow_intact(game, shadow, kFileName, dat_count);
}

// What changed since the last build, as far as the state and the files' times tell (the state keeps only the
// fingerprint, not what went into it).
std::vector<std::string> changes(const ld::ContentState& state, const ld::FingerprintInput& in,
                                 const std::vector<ContentPanel::Mod>& mods) {
    std::vector<std::string> out;
    std::map<std::string, std::string> built;  // id -> version
    for (const auto& m : state.mods) {
        const size_t space = m.find(' ');
        built[m.substr(0, space)] = space == std::string::npos ? std::string() : m.substr(space + 1);
    }
    std::map<std::string, std::string> names;
    for (const auto& m : mods) {
        names[m.id] = m.name;
        const auto b = built.find(m.id);
        if (b == built.end())
            out.push_back(m.name + " is new");
        else if (b->second != m.version)
            out.push_back(m.name + " changed (" + b->second + " to " + m.version + ")");
    }
    for (const auto& [id, version] : built)
        if (!names.count(id)) out.push_back(id + " is no longer there (taken out, switched off or left out)");
    if (!out.empty()) return out;
    const uint64_t at = filetime_of(state.built_at);
    if (!at) return out;
    for (const auto& m : in.mods)
        for (const auto& f : m.files)
            if (f.written > at) {
                out.push_back((names.count(m.id) ? names[m.id] : m.id) + " changed");
                break;
            }
    bool game = false;
    for (const auto* f : {&in.base, &in.game_kfc, &in.resources, &in.exe}) game = game || (f->exists && f->written > at);
    if (game) out.push_back("the game changed (a Steam update or a file check)");
    if (in.dll.exists && in.dll.written > at) out.push_back("emf_content.dll changed");
    return out;
}

}  // namespace

std::vector<std::string> content_set_aside_names(const std::wstring& game) {
    std::vector<std::string> names;
    const auto text = cio::read_file(game + L"\\emf\\moved.txt", 1u << 20);
    if (!text) return names;
    size_t from = 0;
    while (from < text->size()) {
        size_t to = text->find('\n', from);
        if (to == std::string::npos) to = text->size();
        std::string line = text->substr(from, to - from);
        from = to + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string head = "set-aside\t";
        if (line.rfind(head, 0) != 0) continue;
        const size_t tab = line.find('\t', head.size());
        if (tab == std::string::npos || _stricmp(line.c_str() + tab + 1, "dbghelp.dll") != 0) continue;
        names.push_back(line.substr(head.size(), tab - head.size()));
    }
    return names;
}

const char* content_status_name(ContentStatus s) {
    switch (s) {
        case ContentStatus::Ready: return "ready";
        case ContentStatus::BuildsNext: return "builds_next";
        case ContentStatus::Failed: return "failed";
        case ContentStatus::Off: return "off";
        case ContentStatus::OtherTool: return "other_tool";
        case ContentStatus::NotInstalled: return "not_installed";
        case ContentStatus::ServerLater: return "server_later";
    }
    return "unknown";
}

const char* take_over_name(TakeOver t) {
    switch (t) {
        case TakeOver::Offer: return "offer";
        case TakeOver::Given: return "given";
        case TakeOver::None: break;
    }
    return "none";
}

ContentPanel content_panel(const std::wstring& game, bool server_folder) {
    ContentPanel p;
    if (server_folder) {
        p.status = ContentStatus::ServerLater;
        p.headline = "The dedicated server gets EML content in a later version of EMF.";
        return p;
    }
    const std::wstring dll = game + L"\\emf\\emf_content.dll";
    const std::wstring shadow = game + L"\\" + ld::kShadowSubdir;
    const std::wstring ini = game + L"\\emf\\emf_core.ini";

    // ---- the facts, in the loader's order ----
    ld::ContentFacts f;
    f.mode = ld::content_mode([&](const wchar_t* section, const wchar_t* key) {
        wchar_t buf[128] = {};
        GetPrivateProfileStringW(section, key, L"", buf, 128, ini.c_str());
        return std::wstring(buf);
    });
    p.mode = f.mode == ld::ContentMode::Off ? "off" : f.mode == ld::ContentMode::On ? "on" : "auto";
    const auto tools = core::parse_known_tools(known_tools_table(game));
    ld::ToolLook look;
    look.exists = [&](const std::wstring& rel) { return is_file(game + L"\\" + rel); };
    look.tool_name = [&](const std::wstring& rel) {
        const std::string word = dll_tool_word(game, rel);
        if (word.empty() || word == "emf") return std::string();
        for (const auto& t : tools)
            if (t.word == word) return t.name;
        return word;
    };
    look.chain = ld::parse_chain_list(cio::read_file(game + L"\\dinput8-chain.ini", 1u << 20).value_or(std::string()));
    const cio::Stat dll_stat = cio::stat(dll);
    p.dll.present = dll_stat.exists && !dll_stat.dir;
    if (p.dll.present) {
        p.dll.size = dll_stat.size;
        p.dll.time = unix_seconds(dll_stat.written);
    }
    if (f.mode != ld::ContentMode::Off) {
        f.dll_present = p.dll.present;
        f.other_tool = ld::other_content_tool(look, f.dll_present, false);
        if (!f.other_tool && f.dll_present) {
            f.mods = ld::scan_eml_mods(game + L"\\mods");
            if (!f.mods.usable.empty()) {
                f.other_tool = ld::chained_tool_by_text(look);
                if (!f.other_tool)
                    f.base = ld::choose_base(ld::read_base_file(game + L"\\enshrouded.kfc"),
                                             ld::read_base_file(game + L"\\enshrouded.kfc.bak"));
            }
        }
    }
    const ld::ContentDecision d = ld::decide_content(f);
    p.why = d.why;

    // ---- the mods, the last build, the copy ----
    const auto state = ld::read_state(shadow + L"\\" + ld::kStateFile);
    const bool state_file = cio::stat(shadow + L"\\" + ld::kStateFile).exists;
    if (state) {
        ContentPanel::LastBuild b;
        b.code = state->code;
        b.message = state->message;
        b.errors = state->errors;
        b.mods = state->mods;
        b.ms = state->ms;
        b.built_at = state->built_at;
        p.last_build = b;
    }
    for (const auto& m : f.mods.usable) {
        ContentPanel::Mod x;
        x.folder = cio::to_utf8(m.folder);
        x.id = m.id;
        x.version = m.version;
        x.name = mod_name(game + L"\\mods\\" + m.folder);
        if (x.name.empty()) x.name = m.id;
        if (state)
            for (const auto& s : state->mods) x.built = x.built || s == m.id + " " + m.version;
        p.mods.push_back(std::move(x));
    }
    for (const auto& [folder, why] : f.mods.skipped) p.left_out.push_back({cio::to_utf8(folder), why});
    const cio::Stat shadow_stat = cio::stat(shadow);
    p.copy_there = shadow_stat.exists && shadow_stat.dir;
    if (p.copy_there && ld::shadow_path_safe(game, shadow)) p.copy_bytes = own_bytes(shadow, 0);

    // ---- EML's dbghelp.dll and EMF (E5), and a patch in place ----
    for (const auto& name : content_set_aside_names(game))
        if (p.set_aside.empty() && is_file(game + L"\\" + cio::from_utf8(name))) p.set_aside = name;
    if (!p.set_aside.empty())
        p.take_over = TakeOver::Given;
    else if (p.dll.present && is_file(game + L"\\dbghelp.dll") && dll_tool_word(game, L"dbghelp.dll") == "eml")
        p.take_over = TakeOver::Offer;
    if (!f.other_tool) {
        const ld::BaseChoice base = f.base.ok || !f.base.why.empty()
                                        ? f.base
                                        : ld::choose_base(ld::read_base_file(game + L"\\enshrouded.kfc"),
                                                          ld::read_base_file(game + L"\\enshrouded.kfc.bak"));
        p.patched_in_place = base.use_bak;
    }

    // ---- the status ----
    if (!d.emf_owns) {
        if (f.other_tool) {
            p.status = ContentStatus::OtherTool;
            p.other_tool = *f.other_tool;
            p.headline = "Applied by " + p.other_tool;
            p.detail = "Another tool applies the content of the EML mods, so EMF leaves it alone and builds nothing.";
            if (p.take_over == TakeOver::Offer)
                p.detail += " EMF can take over from EML: it sets EML's dbghelp.dll aside (uninstalling EMF puts it back).";
        } else if (f.mode != ld::ContentMode::Off && !f.dll_present) {
            p.status = ContentStatus::NotInstalled;
            p.headline = "Not installed";
            p.detail = "EMF builds the content of EML mods only with emf\\emf_content.dll, which is not in this folder.";
        } else {
            p.status = ContentStatus::Off;
            p.headline = "Off";
            p.detail = inner(d.why) + ".";
        }
        return p;
    }
    const std::string emf_version = [&] {
        const std::string v = emf_core_version(game + L"\\emf\\emf_core.dll");
        return v.empty() ? std::string(EMF_VERSION) : v;
    }();
    const ld::FingerprintInput in = ld::collect_fingerprint_input(game, kFileName, f.base, f.mods.usable, dll, emf_version);
    const std::string fingerprint = ld::content_fingerprint(in);
    const bool same = state && !state->fingerprint.empty() && state->fingerprint == fingerprint;
    const std::string again = "EMF tries again when a mod, the game or emf_content.dll changes.";
    const auto builds = [&](const std::string& why) {
        p.status = ContentStatus::BuildsNext;
        p.headline = "Builds at the next start";
        p.detail = why + ". The game waits at its start while EMF builds" +
                   (state && state->code == 0 && state->ms ? " (the last build took " + seconds_text(state->ms) + ")"
                                                           : std::string(" (a big pack takes a minute or two)")) +
                   ".";
    };
    const auto failed = [&](const std::string& message) {
        p.status = ContentStatus::Failed;
        p.headline = "Failed";
        p.detail = "The last build with these files failed: " + message + ". " + again;
    };
    if (ld::state_current(state, fingerprint)) {
        if (const std::string broken = copy_problem(game, shadow, f.base.dat_count); !broken.empty()) {
            builds("The copy in emf\\content\\client is missing or does not match the game's files");
        } else {
            p.status = ContentStatus::Ready;
            p.headline = "Ready";
            const size_t n = state->mods.size();
            p.detail = "The copy in emf\\content\\client is current: the next start uses it without building (" +
                       std::to_string(n) + (n == 1 ? " mod" : " mods") + ", built " + state->built_at + ").";
        }
    } else if (same && state->code == kContentBuildRunning) {
        if (state->attempts < kContentMaxBuildAttempts)
            builds("The last build did not finish: the game stopped during it");
        else
            failed("the build stopped the game " + std::to_string(state->attempts) + " times in a row (see emf\\content.log)");
    } else if (same) {
        failed(state->message.empty() ? "code " + std::to_string(state->code) : state->message);
    } else if (!state) {
        builds(state_file ? "EMF's record of the last build (emf_content_state.json) could not be read"
                          : "EMF has not built it yet");
    } else {
        std::vector<std::string> what = changes(*state, in, p.mods);
        std::string why;
        for (const auto& w : what) why += (why.empty() ? "" : "; ") + w;
        if (why.empty()) why = "the game, a mod or emf_content.dll changed since the last build";
        why[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(why[0])));
        builds(why);
    }
    return p;
}

std::string content_panel_json(const ContentPanel& p) {
    std::vector<std::string> mods, left;
    for (const auto& m : p.mods)
        mods.push_back(Obj().str("folder", m.folder).str("id", m.id).str("name", m.name).str("version", m.version)
                           .boolean("built", m.built).text());
    for (const auto& l : p.left_out) left.push_back(Obj().str("folder", l.folder).str("why", l.why).text());
    std::string last = "null";
    if (p.last_build) {
        const auto& b = *p.last_build;
        last = Obj().num("code", b.code).str("message", b.message).strs("errors", b.errors).strs("mods", b.mods)
                   .num("ms", static_cast<double>(b.ms)).str("built_at", b.built_at).text();
    }
    return Obj()
        .str("status", content_status_name(p.status)).str("headline", p.headline).str("detail", p.detail).str("why", p.why)
        .str("mode", p.mode)
        .raw("dll", Obj().boolean("present", p.dll.present).num("size", static_cast<double>(p.dll.size))
                        .str("size_text", saves::size_text(p.dll.size)).num("time", static_cast<double>(p.dll.time)).text())
        .raw("mods", json_list(mods)).raw("left_out", json_list(left)).raw("last_build", last)
        .boolean("copy_there", p.copy_there).num("copy_bytes", static_cast<double>(p.copy_bytes))
        .str("copy_text", saves::size_text(p.copy_bytes)).boolean("patched_in_place", p.patched_in_place)
        .str("take_over", take_over_name(p.take_over)).str("other_tool", p.other_tool).str("set_aside", p.set_aside)
        .text();
}

}  // namespace emf::app
