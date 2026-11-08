#pragma once
// What the loader remembers of its creature pictures calls: <game>\emf\cache\portraits_attempts.json, beside the
// portraits cache folder (not inside it: a key is a folder name there, and "attempts.json" is a valid key). One record
// per key, written after each call that asked for it:
//   {"attempts": [{"key": "wolf", "entry": "template:<guid>:24:256", "dll": "sha256:...", "game": "sha256:...",
//                  "result": "ok" | "failed", "message": "..."}]}
// "dll" is the fingerprint of emf_content.dll (size and time), "game" of the game's exe, .kfc, .kfc_resources and
// .dat count: the same kind of fingerprint the content build uses (content_fingerprint.h), no file is hashed.
//   - a failed key (the DLL said no, or the call did not finish or left the DLL loaded) is not tried again while the
//     record matches the DLL, the game and the mod's entry: a start held for a picture that cannot be drawn would
//     otherwise happen at every start. It is tried again when emf_content.dll, the game or the entry changed;
//   - a key that worked is left alone while the record matches and the cache has its index.json; when the game or the
//     DLL changed since, the DLL is asked again, which answers "current" without drawing when what the picture is
//     drawn from (the model, its textures) is unchanged, and draws it again when it is not (a game update).
// A key with no record (a cache from before this file) is asked once when the cache has it.
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "content_io.h"
#include "core/json.h"
#include "portraits_plan.h"

namespace emf::loader {

struct PortraitStamps {
    std::string dll;   // emf_content.dll
    std::string game;  // the game's exe, .kfc, .kfc_resources, .dat count
};

struct PortraitAttempt {
    std::string key, entry, dll, game;
    bool ok = false;
    std::string message;  // why it failed
};

// What the loader knows when it looks for pictures to ask for: the file's records and the stamps of this start.
struct PortraitMemory {
    std::vector<PortraitAttempt> attempts;
    PortraitStamps stamps;
};

// What a mod's entry says about a picture: another model, frames or size is another entry.
inline std::string portrait_entry(const PortraitItem& p) {
    return (p.template_guid.empty() ? "model:" + p.model_guid : "template:" + p.template_guid) + ":" +
           std::to_string(p.frames) + ":" + std::to_string(p.size);
}

inline std::string attempts_text(const std::vector<PortraitAttempt>& attempts) {
    std::string t = "{\"attempts\": [";
    for (size_t i = 0; i < attempts.size(); ++i) {
        const auto& a = attempts[i];
        t += (i ? ",\n  " : "\n  ") + std::string("{\"key\": ") + cio::json_string(a.key) +
             ", \"entry\": " + cio::json_string(a.entry) + ", \"dll\": " + cio::json_string(a.dll) +
             ", \"game\": " + cio::json_string(a.game) + ", \"result\": " + (a.ok ? "\"ok\"" : "\"failed\"") +
             ", \"message\": " + cio::json_string(a.message) + "}";
    }
    return t + "\n]}\n";
}

// A file that is not what attempts_text writes is no memory (every key is asked again); a bad record is left out.
inline std::vector<PortraitAttempt> read_attempts(const std::string& text) {
    using T = emf::core::json::Value::Type;
    std::vector<PortraitAttempt> out;
    const auto parsed = emf::core::json::parse(text);
    if (!parsed.value || !parsed.value->is(T::Object)) return out;
    const auto* list = parsed.value->get("attempts");
    if (!list || !list->is(T::Array)) return out;
    for (const auto& e : list->arr) {
        const auto str = [&e](const char* name) {
            const auto* f = e.get(name);
            return f && f->is(T::String) ? f->s : std::string();
        };
        PortraitAttempt a;
        a.key = str("key");
        a.entry = str("entry");
        a.dll = str("dll");
        a.game = str("game");
        a.message = str("message");
        const std::string result = str("result");
        a.ok = result == "ok";
        if (a.key.empty() || (result != "ok" && result != "failed")) continue;
        out.push_back(std::move(a));
    }
    return out;
}

// The items to ask the DLL for at this start, in order, at most `cap`: the ones whose picture the cache lacks, and the
// ones that were last asked about under another DLL, game or entry (or never: a cache from before the memory).
// `remembered` gets "<key>: <message>" for each failed key left alone.
inline std::vector<PortraitItem> portraits_to_ask(const std::vector<PortraitItem>& items,
                                                  const std::vector<PortraitAttempt>& attempts,
                                                  const PortraitStamps& now,
                                                  const std::function<bool(const std::string& key)>& cached,
                                                  std::vector<std::string>* remembered = nullptr,
                                                  size_t cap = kMaxPortraitsPerStart) {
    std::vector<PortraitItem> out;
    for (const auto& p : items) {
        if (out.size() >= cap) break;
        const PortraitAttempt* last = nullptr;
        for (const auto& a : attempts)
            if (a.key == p.key) last = &a;
        const bool same = last && last->entry == portrait_entry(p) && last->dll == now.dll && last->game == now.game;
        if (same && !last->ok) {
            if (remembered)
                remembered->push_back(p.key + ": failed at an earlier start (" + last->message +
                                      "); not tried again until emf_content.dll, the game or the entry changes");
            continue;
        }
        if (same && cached(p.key)) continue;
        out.push_back(p);  // no picture, or the DLL, the game or the entry changed, or a cache from before the memory
    }
    return out;
}

}  // namespace emf::loader
