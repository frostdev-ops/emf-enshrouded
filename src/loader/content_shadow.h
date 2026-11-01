#pragma once
// The shadow copy the EML content is built into: <game>\emf\content\client\ (spec c.4). emf_content.dll works in the
// folder it is given, so it gets this one, never the game's own:
//   <name>.exe, <name>_000.dat .. <name>_(N-1).dat    hard links to the game's files (N: the base's containers).
//                                                     EML only reads them; it writes new containers from N up, after
//                                                     deleting the name first. A link whose file is no longer the
//                                                     game's (Steam replaced it) is made again.
//   <name>.kfc, <name>.kfc_resources                  private copies (the base: the .kfc, or the .kfc.bak when the
//                                                     game's files were patched in place; content_base.h). EML
//                                                     patches them in place and keeps <name>.kfc.bak itself: the DLL
//                                                     refuses to build when one of the three has a second name.
//   mods\<folder>\                                    real folders with copies of the usable mods (EML skips junctions)
//   shadow.txt                                        the record of the game's files the redirect checks at each start
//                                                     (file_redirect.h): taken before anything is copied, written last
// Before each build it removes what the last one left: the index files, every .tmp, .cache\, mods\, and every
// container at N or past it. Links are only ever deleted, never opened for writing; the game's files are never
// written. Every file it writes is written as .tmp and renamed. No hard links (exFAT, another drive): content is off,
// it never copies the game's 55 GB.
#include <windows.h>

#include <cstdint>
#include <cwchar>
#include <functional>
#include <string>
#include <vector>

#include "content_base.h"
#include "content_fingerprint.h"
#include "content_io.h"
#include "eml_mods.h"

namespace emf::loader {

struct ShadowResult {
    bool ok = false;
    std::string why;  // when !ok: what went wrong, for the log and the state
};

// The file system calls a test replaces.
struct ShadowIo {
    // CreateHardLinkW(link, target); false with *error set when it fails
    std::function<bool(const std::wstring& link, const std::wstring& target, DWORD* error)> link;
};

namespace shadow_detail {
inline std::string err(DWORD e) { return " (Windows error " + std::to_string(e) + ")"; }

struct Id {
    DWORD volume = 0, high = 0, low = 0, links = 0;
    bool ok = false;
};

// The file's identity, opened for its attributes only (never its data).
inline Id file_id(const std::wstring& path) {
    Id id;
    const HANDLE f = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (f == INVALID_HANDLE_VALUE) return id;
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(f, &info))
        id = {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow, info.nNumberOfLinks, true};
    CloseHandle(f);
    return id;
}

inline bool same_file(const std::wstring& a, const std::wstring& b) {
    const Id x = file_id(a), y = file_id(b);
    return x.ok && y.ok && x.volume == y.volume && x.high == y.high && x.low == y.low;
}

// Deletes a name (a link stays a link: the file behind it lives on under its other names). A read-only file is made
// writable first only when it has no other name: the mark is the file's, so clearing it on a link would clear it on
// the game's file too.
inline bool delete_name(const std::wstring& path, std::string* why) {
    if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)
        return true;
    DWORD e = GetLastError();
    const DWORD a = GetFileAttributesW(path.c_str());
    if (e == ERROR_ACCESS_DENIED && a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY) && file_id(path).links == 1 &&
        SetFileAttributesW(path.c_str(), a & ~FILE_ATTRIBUTE_READONLY)) {
        if (DeleteFileW(path.c_str())) return true;
        e = GetLastError();
    }
    *why = "could not remove " + cio::to_utf8(path) + " from the shadow copy" + err(e);
    return false;
}

// Removes a folder of the shadow's own (.cache, mods) with what is in it; a junction or link inside is removed as a
// link, never followed.
inline bool remove_tree(const std::wstring& path, std::string* why, int depth = 0) {
    const cio::Stat s = cio::stat(path);
    if (!s.exists) return true;
    if (!s.dir) return delete_name(path, why);
    if (!s.reparse && depth < 64) {
        for (const auto& e : detail::list_dir(path))
            if (!remove_tree(cio::join(path, e.name), why, depth + 1)) return false;
    }
    if (RemoveDirectoryW(path.c_str())) return true;
    *why = "could not remove the folder " + cio::to_utf8(path) + " from the shadow copy" + err(GetLastError());
    return false;
}

// "<name>_<digits>.dat": its number, or -1.
inline long dat_index(const std::wstring& file, const std::wstring& name) {
    const std::wstring head = name + L"_";
    if (file.size() <= head.size() + 4 || _wcsnicmp(file.c_str(), head.c_str(), head.size()) != 0 ||
        _wcsicmp(file.c_str() + file.size() - 4, L".dat") != 0)
        return -1;
    const std::wstring digits = file.substr(head.size(), file.size() - head.size() - 4);
    if (digits.empty() || digits.size() > 9 || digits.find_first_not_of(L"0123456789") != std::wstring::npos) return -1;
    return std::wcstol(digits.c_str(), nullptr, 10);
}

inline std::wstring dat_name(const std::wstring& name, uint32_t i) {
    wchar_t n[16];
    swprintf_s(n, L"_%03u.dat", i);
    return name + n;
}

// A private copy of `from` as `to` (through to.tmp), writable even when the game's file is read-only.
inline bool copy_private(const std::wstring& from, const std::wstring& to, std::string* why) {
    const std::wstring tmp = to + L".tmp";
    if (!delete_name(tmp, why)) return false;
    if (!CopyFileW(from.c_str(), tmp.c_str(), TRUE)) {
        *why = "could not copy " + cio::to_utf8(from) + " into the shadow copy" + err(GetLastError());
        return false;
    }
    const DWORD a = GetFileAttributesW(tmp.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(tmp.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (!MoveFileExW(tmp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        *why = "could not put " + cio::to_utf8(to) + " in place" + err(GetLastError());
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

inline bool make_dir(const std::wstring& path, std::string* why) {
    if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        const cio::Stat s = cio::stat(path);
        if (s.dir && !s.reparse) return true;
        *why = cio::to_utf8(path) + (s.reparse ? " is a junction or link: EMF does not build there (it would write "
                                                 "somewhere else)"
                                               : " is not a folder");
        return false;
    }
    *why = "could not make the folder " + cio::to_utf8(path) + err(GetLastError());
    return false;
}

// "source <file> <size> <time>" as file_redirect.h's record reads it.
inline std::string source_line(const std::wstring& game, const std::wstring& file, std::string* why) {
    const cio::Stat s = cio::stat(cio::join(game, file));
    if (!s.exists || s.dir) {
        *why = cio::to_utf8(file) + " is not in the game folder";
        return {};
    }
    return "source " + cio::to_utf8(file) + " " + std::to_string(s.size) + " " + std::to_string(s.written) + "\r\n";
}
}  // namespace shadow_detail

inline ShadowResult build_shadow(const std::wstring& game, const std::wstring& shadow, const std::string& file_name,
                                 const BaseChoice& base, const std::vector<EmlMod>& mods, const ShadowIo& io = {}) {
    namespace d = shadow_detail;
    ShadowResult r;
    std::string& why = r.why;
    if (!base.ok) {
        why = "no base to build from: " + base.why;
        return r;
    }
    const std::wstring name = cio::from_utf8(file_name);

    // 1. the record of the game's files, taken before anything is copied (a file that changes meanwhile won't match)
    std::string record = "# EMF's content loader made this shadow copy for EML content (src/loader/content_shadow.h)\r\n"
                         "# the game's files when it was made: name, size in bytes, last write time (UTC, Windows file "
                         "time); the number of containers\r\n";
    for (const wchar_t* ext : {L".exe", L".kfc", L".kfc_resources"}) {
        const std::string line = d::source_line(game, name + ext, &why);
        if (line.empty()) return r;
        record += line;
    }
    record += "dat_count " + std::to_string(count_game_dats(game, file_name)) + "\r\n";

    // 2. the folders, none of them a junction (from the game folder down to the shadow)
    std::vector<std::wstring> folders;
    for (std::wstring p = shadow; p.size() > game.size() && _wcsnicmp(p.c_str(), game.c_str(), game.size()) == 0;) {
        folders.insert(folders.begin(), p);
        const size_t slash = p.find_last_of(L"\\/");
        if (slash == std::wstring::npos) break;
        p = p.substr(0, slash);
    }
    if (folders.empty()) folders.push_back(shadow);
    for (const auto& f : folders)
        if (!d::make_dir(f, &why)) return r;

    // 3. what the last build left
    for (const std::wstring& f : {name + L".kfc", name + L".kfc.bak", name + L".kfc_resources", std::wstring(L"shadow.txt")})
        if (!d::delete_name(cio::join(shadow, f), &why)) return r;
    for (const auto& e : detail::list_dir(shadow)) {
        if (e.attrs & FILE_ATTRIBUTE_DIRECTORY) continue;
        const long i = d::dat_index(e.name, name);
        const bool tmp = detail::ends_with_ci(e.name, L".tmp");
        if ((tmp || (i >= 0 && static_cast<uint64_t>(i) >= base.dat_count)) && !d::delete_name(cio::join(shadow, e.name), &why))
            return r;
    }
    if (!d::remove_tree(cio::join(shadow, L".cache"), &why) || !d::remove_tree(cio::join(shadow, L"mods"), &why)) return r;

    // 4. the links: the exe and the base's containers; one that is still the game's file stays
    std::vector<std::wstring> links = {name + L".exe"};
    for (uint32_t i = 0; i < base.dat_count; ++i) links.push_back(d::dat_name(name, i));
    for (const auto& f : links) {
        const std::wstring target = cio::join(game, f), link = cio::join(shadow, f);
        const cio::Stat t = cio::stat(target);
        if (!t.exists || t.dir) {
            why = cio::to_utf8(f) + " is not in the game folder (the base says it has " + std::to_string(base.dat_count) +
                  " containers)";
            return r;
        }
        if (cio::stat(link).exists) {
            if (d::same_file(link, target)) continue;
            if (!d::delete_name(link, &why)) return r;
        }
        DWORD error = 0;
        const bool made = io.link ? io.link(link, target, &error)
                                  : (CreateHardLinkW(link.c_str(), target.c_str(), nullptr) ? true : (error = GetLastError(), false));
        if (!made) {
            why = "the game folder's drive does not allow hard links (" + cio::to_utf8(f) + d::err(error) +
                  "), and EMF never copies the game's data instead";
            return r;
        }
    }

    // 5. private copies of the base's index files
    const std::wstring base_file = cio::join(game, name + (base.use_bak ? L".kfc.bak" : L".kfc"));
    if (!d::copy_private(base_file, cio::join(shadow, name + L".kfc"), &why) ||
        !d::copy_private(cio::join(game, name + L".kfc_resources"), cio::join(shadow, name + L".kfc_resources"), &why))
        return r;

    // 6. the mods: a real folder each, with copies of their files
    const std::wstring mods_to = cio::join(shadow, L"mods");
    if (!d::make_dir(mods_to, &why)) return r;
    for (const auto& m : mods) {
        const std::wstring from = cio::join(cio::join(game, L"mods"), m.folder), to = cio::join(mods_to, m.folder);
        if (!d::make_dir(to, &why)) return r;
        for (const auto& f : mod_files(from)) {
            for (size_t at = f.rel.find(L'\\'); at != std::wstring::npos; at = f.rel.find(L'\\', at + 1))
                if (!d::make_dir(cio::join(to, f.rel.substr(0, at)), &why)) return r;
            const std::wstring dst = cio::join(to, f.rel);
            if (!CopyFileW(cio::join(from, f.rel).c_str(), dst.c_str(), TRUE)) {
                why = "could not copy " + cio::to_utf8(cio::join(from, f.rel)) + " into the shadow copy" + d::err(GetLastError());
                return r;
            }
            const DWORD a = GetFileAttributesW(dst.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY))
                SetFileAttributesW(dst.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
        }
    }

    // 7. the record, last
    if (!cio::write_file_atomic(cio::join(shadow, L"shadow.txt"), record)) {
        why = "could not write shadow.txt in the shadow copy";
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace emf::loader
