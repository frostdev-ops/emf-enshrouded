#pragma once
// What the core tells the player about EML content at this start, from what the loader told it in the environment
// variable EMF_CONTENT_STATUS (src/loader/content_status_env.h; docs/engine_parts/content_loader.md):
//   built / current   one line in the console ("EML content: 3 mods (built in 28 s)")
//   failed            a line in the console and on screen once a world loads, and a save backup before that world
//                     (mod items vanish when a world loads without their mod): the backup ignores min_minutes
//   off               only with [content] mode=on (the loader sends it only then): why, in the console
// Header-only and pure but for reading the variable.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "loader/content_status_env.h"

namespace emf::core {

struct ContentLines {
    std::string console;  // the in-game console and emf_core.log; "" for none
    std::string notice;   // on screen once, when the first world loads; "" for none
    bool backup = false;  // back up the saves now, whatever min_minutes says
};

constexpr const char* kContentBackupLabel = "before a start without EML content";

inline std::string content_seconds(uint64_t ms) {
    if (ms < 10000) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f s", static_cast<double>(ms) / 1000.0);
        return buf;
    }
    return std::to_string((ms + 500) / 1000) + " s";
}

// `backups_on`: [backup] on_start (a backup is promised only when one will be made).
inline ContentLines content_lines(const std::optional<emf::loader::ContentStatus>& s, bool backups_on) {
    ContentLines l;
    if (!s) return l;
    const std::string mods = std::to_string(s->mods) + (s->mods == 1 ? " mod" : " mods");
    if (s->kind == "built") {
        l.console = "EML content: " + mods + " (built in " + content_seconds(s->ms) + ")";
    } else if (s->kind == "current") {
        l.console = "EML content: " + mods + " (unchanged since the last build)";
    } else if (s->kind == "failed" || s->kind == "failed_again") {
        std::string why = s->message.empty() ? std::string("an unknown error") : s->message;
        while (!why.empty() && (why.back() == '.' || why.back() == ' ')) why.pop_back();
        l.console = "EML content could not be built: " + why + ". The game runs without it.";
        if (s->kind == "failed_again") {
            l.console += " EMF tries again when a mod, the game or emf_content.dll changes.";
        } else {
            if (backups_on) l.console += " Your saves were backed up.";
            l.backup = true;
        }
        l.notice = l.console;
    } else if (s->kind == "off") {
        std::string why = s->message;
        if (why.rfind("off ", 0) == 0) why = why.substr(4);  // "off (...)": the decision's own words
        l.console = "EML content is off " + why;
    }
    return l;
}

// The status in the variable's text (UTF-16, as the environment holds it).
inline std::optional<emf::loader::ContentStatus> content_status_from(const std::wstring& text) {
    if (text.empty()) return std::nullopt;
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), s.data(), n, nullptr, nullptr);
    return emf::loader::parse_content_status(s);
}

// This process's EMF_CONTENT_STATUS, set by the loader before the core started; nullopt when it set none. Read whole
// (a long "failed" message carries the save backup), then cleared, so nothing the game starts later inherits it.
inline std::optional<emf::loader::ContentStatus> read_content_status() {
    const wchar_t* name = emf::loader::kContentStatusVariable;
    const DWORD need = GetEnvironmentVariableW(name, nullptr, 0);
    if (need == 0) return std::nullopt;
    std::wstring text(need, L'\0');
    const DWORD n = GetEnvironmentVariableW(name, text.data(), need);
    SetEnvironmentVariableW(name, nullptr);
    if (n == 0 || n >= need) return std::nullopt;
    text.resize(n);
    return content_status_from(text);
}

}  // namespace emf::core
