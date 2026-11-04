#pragma once
// One portraits call through emf_content.dll (src/content/emf_content.h, emf_content_portraits), the way
// content_build.h makes a build: load it, check its ABI, call it once, check no thread of its own still runs, unload
// it. Every outcome becomes a code and a message, never an exception. Called on one thread from start to end. A DLL
// without the export (one from before portraits) is an outcome too (kBuildNoExport): the start goes on without them.
#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

#include "content_abi.h"
#include "content_build.h"
#include "core/json.h"

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

}  // namespace emf::loader
