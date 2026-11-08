#include "core/hud_editor/layout_store.h"

#include <windows.h>

#include <algorithm>

#include "core/profiles.h"
#include "loader/content_io.h"

namespace emf::core::hud {

namespace {

std::string last_error_text(const char* what) { return std::string(what) + " (Windows error " + std::to_string(GetLastError()) + ")"; }

// The file's text, at most kLayoutFileMax + 1 bytes (one more than allowed, so the reader can say "larger").
bool read_whole(const std::wstring& path, std::string* out, std::string* error) {
    const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        *error = last_error_text("cannot open the file");
        return false;
    }
    out->assign(kLayoutFileMax + 1, '\0');
    DWORD got = 0;
    const BOOL ok = ReadFile(f, out->data(), static_cast<DWORD>(out->size()), &got, nullptr);
    CloseHandle(f);
    if (!ok) {
        *error = last_error_text("cannot read the file");
        return false;
    }
    out->resize(got);
    return true;
}

bool ends_with_json(const std::wstring& name) {
    if (name.size() < 5) return false;
    std::wstring tail = name.substr(name.size() - 5);
    std::transform(tail.begin(), tail.end(), tail.begin(), [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? wchar_t(c + 32) : c; });
    return tail == L".json";
}

// The stem as lower-case ASCII; false when it holds anything else.
bool ascii_stem(const std::wstring& name, std::string* out) {
    out->clear();
    for (size_t i = 0; i + 5 < name.size(); ++i) {
        wchar_t c = name[i];
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
        if (c == 0 || c > 127) return false;
        out->push_back(static_cast<char>(c));
    }
    return true;
}

}  // namespace

std::vector<LayoutFile> read_layout_dir(const std::wstring& dir) {
    std::vector<LayoutFile> out;
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW(loader::cio::join(dir, L"*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring name = fd.cFileName;
        if (!ends_with_json(name)) continue;  // the 8.3 short-name match is loose
        LayoutFile lf;
        std::string slug;
        if (!ascii_stem(name, &slug) || !valid_profile_slug(slug)) {
            lf.slug = slug.empty() ? "?" : slug;
            lf.error = "not a layout's file name (letters, digits and dashes only)";
            out.push_back(std::move(lf));
            continue;
        }
        lf.slug = slug;
        std::string text;
        if (!read_whole(loader::cio::join(dir, name), &text, &lf.error)) {
            out.push_back(std::move(lf));
            continue;
        }
        LayoutRead r = layout_from_json(text);
        lf.error = r.error;
        lf.notes = std::move(r.notes);
        if (r.layout) {
            HudLayout l = std::move(*r.layout);
            if (l.name.empty()) {
                l.name = slug;
            } else if (profile_slug(l.name) != slug) {
                lf.notes.push_back("name \"" + l.name + "\" does not match the file name; shown as \"" + slug + "\"");
                l.name = slug;
            }
            lf.layout = std::move(l);
        }
        out.push_back(std::move(lf));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const LayoutFile& a, const LayoutFile& b) { return a.slug < b.slug; });
    return out;
}

bool write_layout_file(const std::wstring& dir, const HudLayout& l, uint32_t build, std::string* error) {
    const std::string slug = profile_slug(l.name);
    if (!valid_profile_slug(slug)) {
        if (error) *error = "not a layout name";
        return false;
    }
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        if (error) *error = last_error_text("cannot create the folder");
        return false;
    }
    const std::wstring path = loader::cio::join(dir, std::wstring(slug.begin(), slug.end()) + L".json");
    if (!loader::cio::write_file_atomic(path, layout_to_json(l, build))) {
        if (error) *error = "cannot write the file";
        return false;
    }
    return true;
}

bool delete_layout_file(const std::wstring& dir, const std::string& slug, std::string* error) {
    if (!valid_profile_slug(slug)) {
        if (error) *error = "not a layout's file name";
        return false;
    }
    const std::wstring path = loader::cio::join(dir, std::wstring(slug.begin(), slug.end()) + L".json");
    if (DeleteFileW(path.c_str())) return true;
    const DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;
    if (error) *error = last_error_text("cannot delete the file");
    return false;
}

bool set_aside_layout_file(const std::wstring& dir, const std::string& slug, std::string* new_name) {
    const std::wstring base = loader::cio::join(dir, std::wstring(slug.begin(), slug.end()) + L".json");
    if (GetFileAttributesW(base.c_str()) == INVALID_FILE_ATTRIBUTES) return true;  // nothing there
    for (int n = 1; n < 1000; ++n) {
        const std::string suffix = n == 1 ? ".bad" : ".bad" + std::to_string(n);
        const std::wstring target = base + std::wstring(suffix.begin(), suffix.end());
        if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        if (!MoveFileExW(base.c_str(), target.c_str(), 0)) return false;
        if (new_name) *new_name = slug + ".json" + suffix;
        return true;
    }
    return false;
}

}  // namespace emf::core::hud
