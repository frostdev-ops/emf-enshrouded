#pragma once
// The fingerprint of everything an EML content build depends on: when it equals the one the last good build recorded
// (content_state.h), the shadow copy is still right and emf_content.dll is not loaded at all. It covers, by size and
// last write time (no file is hashed: a start must stay quick):
//   the base (which file: <name>.kfc or <name>.kfc.bak), the game's own .kfc, .kfc_resources and exe, the base's
//   container count and the game folder's .dat count (the redirect's check of shadow.txt looks at those),
//   emf_content.dll, each usable mod sorted by id (id, version, every file's relative path, size and time), and EMF's
//   version. Only names relative to the game folder go in, never its own path: the loader (GetModuleFileNameW) and
//   EMF.exe (--game, the picker, Steam) may spell one folder differently (letter case, a trailing backslash, a
//   junction or a subst drive), and a start must not build again for that.
// The fingerprint is "sha256:" + EMF's own SHA-256 of that list (no Windows crypto while the game starts up).
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "common/sha256.h"
#include "content_base.h"
#include "content_io.h"
#include "eml_mods.h"

namespace emf::loader {

struct FileFact {
    std::string name;  // a path or a file name, UTF-8
    uint64_t size = 0;
    uint64_t written = 0;
    bool exists = false;
};

struct ModFiles {
    std::string id, version;
    std::vector<FileFact> files;  // name: the path relative to the mod's folder
};

struct FingerprintInput {
    FileFact base;      // name: the base's file name (<name>.kfc or <name>.kfc.bak), never the folder's path
    FileFact game_kfc;  // the game's own <name>.kfc (the base may be the .bak)
    FileFact resources;
    FileFact exe;
    FileFact dll;
    uint32_t dat_count = 0;       // the base's containers
    unsigned long game_dats = 0;  // <name>_NNN.dat files in the game folder
    std::vector<ModFiles> mods;
    std::string emf_version;
};

namespace detail {
inline std::string fact_line(const char* what, const FileFact& f) {
    return std::string(what) + "\t" + f.name + "\t" + (f.exists ? std::to_string(f.size) + "\t" + std::to_string(f.written)
                                                                : std::string("missing")) + "\n";
}

inline bool name_less(const std::string& a, const std::string& b) {
    const std::wstring wa = cio::from_utf8(a), wb = cio::from_utf8(b);
    return CompareStringOrdinal(wa.c_str(), -1, wb.c_str(), -1, TRUE) == CSTR_LESS_THAN;
}
}  // namespace detail

inline std::string content_fingerprint(const FingerprintInput& in) {
    std::string text = "emf content fingerprint 2\nemf\t" + in.emf_version + "\n";
    text += detail::fact_line("base", in.base);
    text += detail::fact_line("kfc", in.game_kfc);
    text += detail::fact_line("resources", in.resources);
    text += detail::fact_line("exe", in.exe);
    text += detail::fact_line("dll", in.dll);
    text += "dats\t" + std::to_string(in.dat_count) + "\t" + std::to_string(in.game_dats) + "\n";
    std::vector<ModFiles> mods = in.mods;
    std::sort(mods.begin(), mods.end(), [](const ModFiles& a, const ModFiles& b) { return a.id < b.id; });
    for (auto& m : mods) {
        text += "mod\t" + m.id + "\t" + m.version + "\t" + std::to_string(m.files.size()) + "\n";
        std::sort(m.files.begin(), m.files.end(),
                  [](const FileFact& a, const FileFact& b) { return detail::name_less(a.name, b.name); });
        for (const auto& f : m.files) text += detail::fact_line("file", f);
    }
    emf::Sha256 h;
    h.update(text.data(), text.size());
    return "sha256:" + h.hex();
}

inline FileFact fact_of(const std::wstring& path, const std::string& name) {
    FileFact f;
    f.name = name;
    const cio::Stat s = cio::stat(path);
    if (!s.exists || s.dir) return f;
    f.exists = true;
    f.size = s.size;
    f.written = s.written;
    return f;
}

// The <file_name>_<digits>.dat files directly in `game`.
inline unsigned long count_game_dats(const std::wstring& game, const std::string& file_name) {
    const std::wstring head = cio::from_utf8(file_name) + L"_";
    unsigned long n = 0;
    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileExW(cio::join(game, head + L"*.dat").c_str(), FindExInfoBasic, &fd,
                                      FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        const std::wstring name = fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (name.size() <= head.size() + 4 || _wcsnicmp(name.c_str(), head.c_str(), head.size()) != 0 ||
            _wcsicmp(name.c_str() + name.size() - 4, L".dat") != 0)
            continue;
        const std::wstring digits = name.substr(head.size(), name.size() - head.size() - 4);
        if (digits.find_first_not_of(L"0123456789") == std::wstring::npos) ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

inline FingerprintInput collect_fingerprint_input(const std::wstring& game, const std::string& file_name,
                                                  const BaseChoice& base, const std::vector<EmlMod>& mods,
                                                  const std::wstring& dll, const std::string& emf_version) {
    FingerprintInput in;
    const std::wstring name = cio::from_utf8(file_name);
    const std::wstring base_path = cio::join(game, name + (base.use_bak ? L".kfc.bak" : L".kfc"));
    in.base = fact_of(base_path, file_name + (base.use_bak ? ".kfc.bak" : ".kfc"));
    in.game_kfc = fact_of(cio::join(game, name + L".kfc"), file_name + ".kfc");
    in.resources = fact_of(cio::join(game, name + L".kfc_resources"), file_name + ".kfc_resources");
    in.exe = fact_of(cio::join(game, name + L".exe"), file_name + ".exe");
    in.dll = fact_of(dll, "emf_content.dll");
    in.dat_count = base.dat_count;
    in.game_dats = count_game_dats(game, file_name);
    in.emf_version = emf_version;
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
        in.mods.push_back(std::move(mf));
    }
    return in;
}

}  // namespace emf::loader
