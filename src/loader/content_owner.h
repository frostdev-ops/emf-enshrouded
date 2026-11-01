#pragma once
// Who applies the EML mods' content at this start (spec d.1, d.2, E7): EMF builds it only when nothing else does and
// everything it needs is there. The checks, in this order (the first that says no decides, and its reason is the
// loader's one line):
//   1. [content] mode=off in emf\emf_core.ini
//   2. "Play without any mods" (EMF.exe's one-shot flag); "Play without EMF mods" keeps EML content (E7)
//   3. another tool applies it: a dbghelp.dll in the game folder (EML's or ShroudForge's proxy), or EML's or
//      ShroudForge's dinput8.dll listed in dinput8-chain.ini; mode=on never overrides another tool
//   4. emf_content.dll is not installed (<game>\emf\emf_content.dll)
//   5. the dedicated server (its content comes later)
//   6. no EML mod in <game>\mods that EMF can build (eml_mods.h)
//   7. no base to build from (content_base.h)
#include <cwchar>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "content_base.h"
#include "eml_mods.h"
#include "play_once.h"

namespace emf::loader {

// [content] mode: auto (the default) builds when the checks allow it; on does the same and also shows why content is
// off in the in-game console; off never builds.
enum class ContentMode { Auto, On, Off };

// `get_string(section, key)` reads the ini like GetPrivateProfileStringW (L"" when missing).
template <class GetString>
ContentMode content_mode(GetString get_string) {
    std::wstring v = get_string(L"content", L"mode");
    if (const size_t semi = v.find(L';'); semi != std::wstring::npos) v.resize(semi);  // a comment after the value
    const auto space = [](wchar_t c) { return c == L' ' || c == L'\t'; };
    while (!v.empty() && space(v.back())) v.pop_back();
    size_t a = 0;
    while (a < v.size() && space(v[a])) ++a;
    v = v.substr(a);
    if (_wcsicmp(v.c_str(), L"off") == 0) return ContentMode::Off;
    if (_wcsicmp(v.c_str(), L"on") == 0) return ContentMode::On;
    return ContentMode::Auto;
}

struct ContentFacts {
    ContentMode mode = ContentMode::Auto;
    PlayOnce play = PlayOnce::Normal;
    bool dll_present = false;
    std::optional<std::string> other_tool;  // the tool that applies the content, as players know it
    ModScan mods;
    BaseChoice base;
    bool server = false;
};

struct ContentDecision {
    bool emf_owns = false;
    std::string why;  // the loader's line is "EML content: " + why
};

inline ContentDecision decide_content(const ContentFacts& f) {
    ContentDecision d;
    if (f.mode == ContentMode::Off) {
        d.why = "off ([content] mode=off in emf\\emf_core.ini)";
    } else if (f.play == PlayOnce::NoMods) {
        d.why = "off for this start (Play without any mods in EMF.exe)";
    } else if (f.other_tool) {
        d.why = "applied by " + *f.other_tool + ", so EMF leaves it alone";
    } else if (!f.dll_present) {
        d.why = "off (emf_content.dll is not installed)";
    } else if (f.server) {
        d.why = "off (the dedicated server does not take EML content from EMF yet)";
    } else if (f.mods.usable.empty()) {
        d.why = "off (no EML mod in the game's mods folder that EMF can build)";
    } else if (!f.base.ok) {
        d.why = "off (" + f.base.why + ")";
    } else {
        d.emf_owns = true;
        const size_t n = f.mods.usable.size();
        d.why = "EMF builds it from " + std::to_string(n) + (n == 1 ? " mod" : " mods") + "; " + f.base.why;
    }
    return d;
}

// How the loader looks at the game folder for another tool (paths relative to it).
struct ToolLook {
    std::function<bool(const std::wstring&)> exists;
    std::function<std::string(const std::wstring&)> tool_name;  // the tool a DLL's text names, as players know it; ""
    std::vector<std::wstring> chain;                            // the DLLs dinput8-chain.ini lists
};

// The tool that applies EML content at this start, or nullopt. A dbghelp.dll in the game folder is EML's or
// ShroudForge's proxy (Windows' own lives in System32). A chained DLL is known by the name EMF's installer gives it
// (<word>.dll: eml.dll, shroudforge.dll); under another name only by reading it, which `read_chain` allows (the loader
// asks only when it would build otherwise: reading DLLs costs time at every start).
inline std::optional<std::string> other_content_tool(const ToolLook& look, bool read_chain) {
    if (look.exists && look.exists(L"dbghelp.dll")) {
        const std::string name = look.tool_name ? look.tool_name(L"dbghelp.dll") : std::string();
        return name.empty() ? std::string("dbghelp.dll (EML or ShroudForge)") : name;
    }
    for (const auto& dll : look.chain) {
        const size_t slash = dll.find_last_of(L"\\/");
        const std::wstring file = slash == std::wstring::npos ? dll : dll.substr(slash + 1);
        const char* known = _wcsicmp(file.c_str(), L"eml.dll") == 0           ? "EML"
                            : _wcsicmp(file.c_str(), L"shroudforge.dll") == 0 ? "ShroudForge"
                                                                               : nullptr;
        if (known) return std::string(known) + " (from dinput8-chain.ini: " + cio::to_utf8(dll) + ")";
    }
    if (!read_chain || !look.tool_name) return std::nullopt;
    for (const auto& dll : look.chain) {
        const std::string name = look.tool_name(dll);
        if (name.rfind("EML", 0) == 0 || name.rfind("ShroudForge", 0) == 0)
            return name + " (from dinput8-chain.ini: " + cio::to_utf8(dll) + ")";
    }
    return std::nullopt;
}

}  // namespace emf::loader
