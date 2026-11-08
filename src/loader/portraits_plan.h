#pragma once
// Portraits (pictures of game creatures for mod windows, API v26 image_load("portrait:<key>")): which ones mods want
// (the "portraits" list of their mod.json), which of them the cache lacks, and the request for emf_content_portraits.
// Pure: the loader reads the files and calls the DLL (content_portraits.h). A mod.json entry:
//   "portraits": [{"key": "wolf", "template": "<template GUID>" | "model": "<RenderModel GUID>", "frames": 24,
//                  "size": 256}]
#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "content_io.h"
#include "core/json.h"

namespace emf::loader {

constexpr size_t kMaxPortraitsPerStart = 32;  // the rest are drawn at the next start

struct PortraitItem {
    std::string mod, key, template_guid, model_guid;
    int frames = 24;  // 1..64
    int size = 256;   // 8..512 pixels
};

// A key as the portrait cache and image_load take it: 1-64 of A-Z a-z 0-9 _ . -, no "..", no trailing dot, no Windows
// device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, with or without an extension).
inline bool portrait_key_ok(const std::string& k) {
    if (k.empty() || k.size() > 64 || k.find("..") != std::string::npos || k.back() == '.') return false;
    for (const char c : k)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == '-'))
            return false;
    std::string base = k.substr(0, k.find('.'));
    for (char& c : base) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return false;
    const bool port = base.rfind("COM", 0) == 0 || base.rfind("LPT", 0) == 0;
    return !(base.size() == 4 && port && base[3] >= '0' && base[3] <= '9');
}

inline bool portrait_guid_ok(const std::string& g) {
    if (g.size() != 36) return false;
    for (size_t i = 0; i < g.size(); ++i) {
        const char c = g[i];
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

// Windows folders (the cache's) do not tell "Wolf" from "wolf": the same key.
inline std::string lower_key(std::string k) {
    for (char& c : k) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return k;
}

// The lists of (mod id, mod.json text) in order. A bad entry, or a mod.json that is not JSON, is skipped with a line
// in `problems` ("<mod>: <why>"); a key a mod before named already keeps that mod's entry.
inline std::vector<PortraitItem> read_portrait_lists(const std::vector<std::pair<std::string, std::string>>& manifests,
                                                     std::vector<std::string>* problems) {
    using T = emf::core::json::Value::Type;
    std::vector<PortraitItem> out;
    std::set<std::string> keys;
    const auto say = [&](const std::string& mod, const std::string& why) {
        if (problems) problems->push_back(mod + ": " + why);
    };
    for (const auto& [mod, text] : manifests) {
        const auto parsed = emf::core::json::parse(text);
        if (!parsed.value || !parsed.value->is(T::Object)) {
            say(mod, "mod.json is not a JSON object");
            continue;
        }
        const auto* list = parsed.value->get("portraits");
        if (!list) continue;
        if (!list->is(T::Array)) {
            say(mod, "\"portraits\" is not a list");
            continue;
        }
        for (const auto& e : list->arr) {
            const auto text_of = [&](const char* name) -> std::string {
                const auto* f = e.get(name);
                return f && f->is(T::String) ? f->s : std::string();
            };
            const auto int_of = [&](const char* name, int def, bool* ok) -> int {
                const auto* f = e.get(name);
                if (!f) return def;
                // range first: a double outside int's range cast to int is undefined
                if (!f->is(T::Number) || !(f->n >= -1e9 && f->n <= 1e9) || f->n != static_cast<double>(static_cast<int>(f->n))) {
                    *ok = false;
                    return def;
                }
                return static_cast<int>(f->n);
            };
            PortraitItem p;
            p.mod = mod;
            p.key = text_of("key");
            p.template_guid = text_of("template");
            p.model_guid = text_of("model");
            bool whole = true;
            p.frames = int_of("frames", 24, &whole);
            p.size = int_of("size", 256, &whole);
            const std::string at = "portrait \"" + p.key + "\": ";
            if (!portrait_key_ok(p.key)) {
                say(mod, at + "the key is 1-64 of A-Z a-z 0-9 _ . - (no trailing dot, no device name)");
            } else if (p.template_guid.empty() == p.model_guid.empty()) {
                say(mod, at + "give a template or a model, one of them");
            } else if (!portrait_guid_ok(p.template_guid.empty() ? p.model_guid : p.template_guid)) {
                say(mod, at + "not a GUID");
            } else if (!whole || p.frames < 1 || p.frames > 64 || p.size < 8 || p.size > 512) {
                say(mod, at + "frames are 1-64 and size 8-512 (whole numbers)");
            } else if (keys.count(lower_key(p.key))) {
                say(mod, at + "another mod named that key first; its picture is used");
            } else {
                keys.insert(lower_key(p.key));
                out.push_back(std::move(p));
            }
        }
    }
    return out;
}

// The items whose picture the cache does not have yet (`cached(key)`: <cache>\<key>\index.json is there), in order,
// at most `cap`.
inline std::vector<PortraitItem> missing_portraits(const std::vector<PortraitItem>& items,
                                                   const std::function<bool(const std::string& key)>& cached,
                                                   size_t cap = kMaxPortraitsPerStart) {
    std::vector<PortraitItem> out;
    for (const auto& p : items) {
        if (out.size() >= cap) break;
        if (!cached(p.key)) out.push_back(p);
    }
    return out;
}

// emf_content_portraits' request (src/content/emf_content.h): frames and size as JSON integers.
inline std::string portraits_request(const std::wstring& game_dir, const std::wstring& cache_dir,
                                     const std::vector<PortraitItem>& items) {
    std::string r = "{\"game_dir\": " + cio::json_string(cio::to_utf8(game_dir)) +
                    ", \"file_name\": \"enshrouded\", \"cache_dir\": " + cio::json_string(cio::to_utf8(cache_dir)) +
                    ", \"items\": [";
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& p = items[i];
        r += (i ? ", " : "") + std::string("{\"key\": ") + cio::json_string(p.key);
        if (!p.template_guid.empty()) r += ", \"template\": " + cio::json_string(p.template_guid);
        else r += ", \"model\": " + cio::json_string(p.model_guid);
        r += ", \"frames\": " + std::to_string(p.frames) + ", \"size\": " + std::to_string(p.size) + "}";
    }
    return r + "]}";
}

}  // namespace emf::loader
