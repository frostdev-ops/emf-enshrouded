#pragma once
// One portraits call through emf_content.dll (src/content/emf_content.h, emf_content_portraits), the way
// content_build.h makes a build: load it, check its ABI, call it once, check no thread of its own still runs, unload
// it. Every outcome becomes a code and a message, never an exception. Called on one thread from start to end. A DLL
// without the export (one from before portraits) is an outcome too (kBuildNoExport): the start goes on without them.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "content_abi.h"
#include "common/sha256.h"
#include "content_build.h"
#include "content_fingerprint.h"
#include "core/json.h"
#include "portraits_attempts.h"
#include "portraits_plan.h"

namespace emf::loader {

struct PortraitsOutcome {
    BuildOutcome call;                // code, message, ms, unloaded, truncated (mods and errors stay empty)
    std::vector<std::string> drawn;   // keys
    std::vector<std::string> current;
    std::vector<std::string> failed;  // "<key>: <error>"
};

namespace detail {
inline void read_portraits_reply(const std::string& text, PortraitsOutcome& out, bool* ok) {
    using T = emf::core::json::Value::Type;
    const auto parsed = emf::core::json::parse(text);
    *ok = parsed.value && parsed.value->is(T::Object);
    if (!*ok) return;
    const auto& v = *parsed.value;
    if (const auto* m = v.get("message"); m && m->is(T::String)) out.call.message = m->s;
    if (const auto* ms = v.get("ms"); ms && ms->is(T::Number) && ms->n >= 0) out.call.ms = static_cast<uint64_t>(ms->n);
    if (const auto* t = v.get("truncated"); t && t->is(T::Bool)) out.call.truncated = t->b;
    if (const auto* items = v.get("items"); items && items->is(T::Array))
        for (const auto& i : items->arr) {
            const auto* key = i.get("key");
            const auto* status = i.get("status");
            const std::string k = key && key->is(T::String) ? key->s : "?";
            const std::string st = status && status->is(T::String) ? status->s : "";
            if (st == "drawn") out.drawn.push_back(k);
            else if (st == "current") out.current.push_back(k);
            else {
                const auto* e = i.get("error");
                out.failed.push_back(k + ": " + (e && e->is(T::String) ? e->s : std::string("failed")));
            }
        }
}
}  // namespace detail

inline PortraitsOutcome run_portraits(const std::wstring& dll, const std::string& request, const BuildLog& log,
                                      size_t reply_capacity = kReplyCapacity,
                                      const std::atomic<bool>* abandoned = nullptr) {
    namespace abi = content_abi;
    PortraitsOutcome out;
    HMODULE module = nullptr;
    try {
        if (abandoned && abandoned->load()) {
            out.call.code = kBuildAbandoned;
            out.call.message = "the held start gave up waiting before emf_content.dll was loaded";
            return out;
        }
        module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            out.call.code = kBuildNoDll;
            out.call.message =
                "could not load " + cio::to_utf8(dll) + " (Windows error " + std::to_string(GetLastError()) + ")";
            return out;
        }
        const auto version = reinterpret_cast<abi::emf_content_abi_fn>(GetProcAddress(module, "emf_content_abi"));
        const auto draw = reinterpret_cast<abi::emf_content_portraits_fn>(GetProcAddress(module, "emf_content_portraits"));
        if (!version || !draw) {
            out.call.code = kBuildNoExport;
            out.call.message = "this emf_content.dll draws no portraits (it is older than this EMF)";
        } else if (const uint32_t v = version(); v != abi::kAbiVersion) {
            out.call.code = kBuildWrongAbi;
            out.call.message = "emf_content.dll has ABI " + std::to_string(v) + "; this EMF needs ABI " +
                               std::to_string(abi::kAbiVersion);
        } else {
            std::vector<char> reply(reply_capacity ? reply_capacity : 1, '\0');
            size_t length = 0;
            detail::LogContext ctx{&log};
            const ULONGLONG t0 = GetTickCount64();
            const int32_t rc = draw(request.c_str(), &detail::build_log_line, &ctx, reply.data(), reply.size(), &length);
            out.call.code = rc;
            if (rc == abi::EMF_CONTENT_REPLY_TOO_SMALL) {
                out.call.truncated = true;
                out.call.message = "the portraits' reply did not fit, so their result is unknown";
            } else {
                const size_t n = length > 0 && length <= reply.size() ? length - 1 : strnlen(reply.data(), reply.size());
                bool ok = false;
                detail::read_portraits_reply(std::string(reply.data(), n), out, &ok);
                if (!ok) {
                    out.call.code = kBuildBadReply;
                    out.call.message = "emf_content.dll's portraits reply (code " + std::to_string(rc) +
                                       ") is not the JSON it should be";
                }
            }
            if (!out.call.ms) out.call.ms = GetTickCount64() - t0;
        }
    } catch (...) {
        out.call.code = kBuildError;
        out.call.message = "the portraits stopped on an error in the loader (out of memory?)";
    }
    if (module) detail::unload(module, dll, out.call);
    return out;
}

// The stamps of this start (portraits_attempts.h): emf_content.dll's, the game's exe, .kfc, .kfc_resources and .dat
// count, and the EML content mods' (id, version and every file's path, size and time, in the order given: the list the
// content fingerprint takes), as facts of the kind the content build's fingerprint takes (nothing hashed per file).
inline PortraitStamps portraits_stamps(const std::wstring& game, const std::string& file_name, const std::wstring& dll,
                                       const std::vector<EmlMod>& mods) {
    const std::wstring name = cio::from_utf8(file_name);
    std::string game_text = "emf portraits game stamp 1\n";
    game_text += detail::fact_line("exe", fact_of(cio::join(game, name + L".exe"), file_name + ".exe"));
    game_text += detail::fact_line("kfc", fact_of(cio::join(game, name + L".kfc"), file_name + ".kfc"));
    game_text += detail::fact_line("resources", fact_of(cio::join(game, name + L".kfc_resources"), file_name + ".kfc_resources"));
    game_text += "dats\t" + std::to_string(count_game_dats(game, file_name)) + "\n";
    const std::string dll_text = "emf portraits dll stamp 1\n" + detail::fact_line("dll", fact_of(dll, "emf_content.dll"));
    // sorted as the content fingerprint sorts them (mods by id, files by name), so the order they come in is no change
    std::vector<ModFiles> sorted;
    for (const auto& m : mods) {
        ModFiles mf;
        mf.id = m.id;
        mf.version = m.version;
        for (const auto& f : mod_files(cio::join(cio::join(game, L"mods"), m.folder))) {
            FileFact x;
            x.name = cio::to_utf8(f.rel);
            x.size = f.size;
            x.written = f.written;
            x.exists = true;
            mf.files.push_back(std::move(x));
        }
        std::sort(mf.files.begin(), mf.files.end(),
                  [](const FileFact& a, const FileFact& b) { return detail::name_less(a.name, b.name); });
        sorted.push_back(std::move(mf));
    }
    std::sort(sorted.begin(), sorted.end(), [](const ModFiles& a, const ModFiles& b) { return a.id < b.id; });
    std::string mods_text = "emf portraits mods stamp 1\n";
    for (const auto& m : sorted) {
        mods_text += "mod\t" + m.id + "\t" + m.version + "\t" + std::to_string(m.files.size()) + "\n";
        for (const auto& f : m.files) mods_text += detail::fact_line("file", f);
    }
    const auto hex = [](const std::string& t) {
        emf::Sha256 h;
        h.update(t.data(), t.size());
        return "sha256:" + h.hex();
    };
    return {hex(dll_text), hex(game_text), hex(mods_text)};
}

// The memory after a call that asked for `asked`: each key's result from the reply; a key the reply says nothing of
// (the call failed whole, or the reply was cut) failed with the call's message. Other keys' records stay.
inline std::vector<PortraitAttempt> record_attempts(std::vector<PortraitAttempt> memory,
                                                    const std::vector<PortraitItem>& asked, const PortraitsOutcome& out,
                                                    const PortraitStamps& now) {
    const auto has = [](const std::vector<std::string>& keys, const std::string& k) {
        return std::find(keys.begin(), keys.end(), k) != keys.end();
    };
    for (const auto& item : asked) {
        PortraitAttempt a;
        a.key = item.key;
        a.entry = portrait_entry(item);
        a.dll = now.dll;
        a.game = now.game;
        a.mods = now.mods;
        a.ok = has(out.drawn, item.key) || has(out.current, item.key);
        if (!a.ok) {
            const std::string prefix = item.key + ": ";
            for (const auto& f : out.failed)
                if (f.rfind(prefix, 0) == 0) a.message = f.substr(prefix.size());
            if (a.message.empty())
                a.message = out.call.message.empty() ? "the call gave no result for it (code " + std::to_string(out.call.code) + ")"
                                                     : out.call.message;
        }
        memory.erase(std::remove_if(memory.begin(), memory.end(), [&](const PortraitAttempt& m) { return m.key == a.key; }),
                     memory.end());
        memory.push_back(std::move(a));
    }
    return memory;
}

// The portraits the mods in <mods_dir>\<id>\mod.json ask for (in the order of the folder names) to ask the DLL for: the
// ones the cache lacks (<cache_dir>\<key>\index.json), and, with a `memory` (portraits_attempts.h), the ones last asked
// about under another DLL, game or entry, while a failed key the memory has for this DLL, game and entry is left out
// (a line in `problems` says so). At most kMaxPortraitsPerStart. A folder without a mod.json is no mod; a bad list is a
// line in `problems`. No mods folder: none.
inline std::vector<PortraitItem> wanted_portraits(const std::wstring& mods_dir, const std::wstring& cache_dir,
                                                  std::vector<std::string>* problems,
                                                  const PortraitMemory* memory = nullptr) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileExW((mods_dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories,
                                      nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return {};
    do {
        const std::wstring n = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != L"." && n != L"..") names.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    std::vector<std::pair<std::string, std::string>> manifests;
    for (const auto& n : names) {
        const std::wstring path = mods_dir + L"\\" + n + L"\\mod.json";
        if (auto text = cio::read_file(path, 1u << 20)) {
            manifests.emplace_back(cio::to_utf8(n), std::move(*text));
        } else if (problems && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            problems->push_back(cio::to_utf8(n) + ": mod.json is over 1 MiB or cannot be read; its portraits are left out");
        }
    }
    const auto cached = [&cache_dir](const std::string& key) {
        const DWORD a = GetFileAttributesW((cache_dir + L"\\" + cio::from_utf8(key) + L"\\index.json").c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    };
    const auto items = read_portrait_lists(manifests, problems);
    if (!memory) return missing_portraits(items, cached);
    std::vector<std::string> remembered;
    auto out = portraits_to_ask(items, memory->attempts, memory->stamps, cached, &remembered);
    if (problems) problems->insert(problems->end(), remembered.begin(), remembered.end());
    return out;
}

}  // namespace emf::loader
