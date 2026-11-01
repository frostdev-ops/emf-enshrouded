#pragma once
// The EML mods in <game>\mods\ (one folder each, with an EML mod.json) that EMF can build: the folder name does not
// start with a dot (EML skips those), mod.json parses and has "id", "name" and "version" (EML needs all three), the id
// is not taken by an earlier folder, and "capabilities" asks only for "patch" and/or "export". A mod that needs EML's
// runtime loader ("runtime", "runtime-register-dll") is left out: EMF does not run that part of EML. Every folder left
// out is named with the reason; plain files (another tool's DLLs, a readme) are passed over, a .zip mod is named.
#include <windows.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "content_io.h"
#include "core/json.h"

namespace emf::loader {

struct EmlMod {
    std::wstring folder;  // the folder's name in <game>\mods\ (also its name in the shadow's mods\)
    std::string id, version;
    std::vector<std::string> capabilities;
};

struct ModScan {
    std::vector<EmlMod> usable;
    std::vector<std::pair<std::wstring, std::string>> skipped;  // folder (or .zip), why
};

constexpr uint64_t kMaxManifestBytes = 1u << 20;

namespace detail {
struct DirEntry {
    std::wstring name;
    DWORD attrs = 0;
};

// The entries of `dir` (not "." and ".."), sorted by name as Windows compares them (case ignored).
inline std::vector<DirEntry> list_dir(const std::wstring& dir) {
    std::vector<DirEntry> out;
    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileExW(cio::join(dir, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring n = fd.cFileName;
        if (n != L"." && n != L"..") out.push_back({n, fd.dwFileAttributes});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return out;
}

inline bool ends_with_ci(const std::wstring& s, const wchar_t* tail) {
    const size_t n = wcslen(tail);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, tail) == 0;
}

// "" when the manifest is one EMF builds (filling `m`), else why not.
inline std::string read_manifest(const std::wstring& folder_path, EmlMod& m) {
    const std::wstring path = cio::join(folder_path, L"mod.json");
    const cio::Stat s = cio::stat(path);
    if (!s.exists || s.dir) return "no mod.json in the folder";
    const auto text = cio::read_file(path, kMaxManifestBytes);
    if (!text) return "mod.json could not be read (or is bigger than 1 MB)";
    const auto parsed = emf::core::json::parse(*text);
    if (!parsed.value)
        return "mod.json is not valid JSON: " + parsed.error + " (line " + std::to_string(parsed.line) + ", column " +
               std::to_string(parsed.column) + ")";
    using T = emf::core::json::Value::Type;
    const auto& v = *parsed.value;
    if (!v.is(T::Object)) return "mod.json is not a JSON object";
    for (const char* key : {"id", "name", "version"}) {
        const auto* f = v.get(key);
        if (!f || !f->is(T::String) || f->s.empty()) return std::string("mod.json has no \"") + key + "\"";
    }
    m.id = v.get("id")->s;
    m.version = v.get("version")->s;
    m.capabilities.clear();
    if (const auto* caps = v.get("capabilities")) {
        if (!caps->is(T::Array)) return "mod.json's \"capabilities\" is not a list";
        for (const auto& c : caps->arr) {
            if (!c.is(T::String)) return "mod.json's \"capabilities\" holds something that is not a name";
            if (c.s == "runtime" || c.s == "runtime-register-dll") return "needs EML's runtime loader, which EMF does not run";
            if (c.s != "patch" && c.s != "export") return "mod.json asks for the capability \"" + c.s + "\", which EMF does not know";
            m.capabilities.push_back(c.s);
        }
    }
    return {};
}
}  // namespace detail

// A mod's files, for the fingerprint and the shadow's copy: every file under its folder, by its path relative to the
// folder ("sub\a.lua"), sorted. Junctions and links inside are passed over (never followed), as the copy does.
struct ModFile {
    std::wstring rel;
    uint64_t size = 0;
    uint64_t written = 0;
};

constexpr size_t kMaxModFiles = 100000;  // a mod folder past this is not walked further

inline void add_mod_files(const std::wstring& root, const std::wstring& rel, std::vector<ModFile>& out, int depth) {
    if (depth > 32) return;
    for (const auto& e : detail::list_dir(rel.empty() ? root : cio::join(root, rel))) {
        if (out.size() >= kMaxModFiles) return;
        if (e.attrs & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        const std::wstring r = rel.empty() ? e.name : rel + L"\\" + e.name;
        if (e.attrs & FILE_ATTRIBUTE_DIRECTORY) {
            add_mod_files(root, r, out, depth + 1);
        } else {
            const cio::Stat s = cio::stat(cio::join(root, r));
            out.push_back({r, s.size, s.written});
        }
    }
}

inline std::vector<ModFile> mod_files(const std::wstring& mod_dir) {
    std::vector<ModFile> out;
    add_mod_files(mod_dir, L"", out, 0);
    std::sort(out.begin(), out.end(), [](const ModFile& a, const ModFile& b) {
        return CompareStringOrdinal(a.rel.c_str(), -1, b.rel.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return out;
}

inline ModScan scan_eml_mods(const std::wstring& mods_dir) {
    ModScan scan;
    for (const auto& e : detail::list_dir(mods_dir)) {
        if (!(e.attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            if (detail::ends_with_ci(e.name, L".zip"))
                scan.skipped.emplace_back(e.name, "a .zip mod: EMF builds only mods in a folder (unpack it into one)");
            continue;
        }
        if (e.name[0] == L'.') {
            scan.skipped.emplace_back(e.name, "the folder's name starts with a dot (EML leaves such mods out)");
            continue;
        }
        if (e.attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
            scan.skipped.emplace_back(e.name, "a junction or link, not a real folder (EML leaves those out)");
            continue;
        }
        EmlMod m;
        m.folder = e.name;
        std::string why = detail::read_manifest(cio::join(mods_dir, e.name), m);
        if (why.empty())
            for (const auto& u : scan.usable)
                if (u.id == m.id) why = "the id \"" + m.id + "\" is taken by the folder " + cio::to_utf8(u.folder);
        if (why.empty())
            scan.usable.push_back(std::move(m));
        else
            scan.skipped.emplace_back(e.name, why);
    }
    return scan;
}

}  // namespace emf::loader
