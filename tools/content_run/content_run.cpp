// emf_content_run: builds the content of a game folder's EML mods with emf_content.dll, for tests and checks by hand.
// Never packaged. Usage: emf_content_run <game_dir> [--force] [--dll <path>]
// It loads the DLL (by default next to this exe, then the build's content folder), prints the DLL's log lines to
// stderr as "[level] line" and the reply JSON to stdout, unloads the DLL, and exits with the DLL's return code.
// Exit 10: wrong arguments; 11: the DLL could not be loaded or has another ABI.
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "content/emf_content.h"

namespace {

constexpr int kUsage = 10;
constexpr int kNoDll = 11;

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const char* s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20) {
            char esc[8];
            std::snprintf(esc, sizeof esc, "\\u%04x", c);
            out += esc;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::wstring exe_folder() {
    wchar_t p[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}

bool is_file(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void log_line(void*, int level, const char* line) {
    static const char* const names[] = {"error", "warning", "info", "debug"};
    std::fprintf(stderr, "[%s] %s\n", level >= 0 && level <= 3 ? names[level] : "log", line);
}

int usage() {
    std::fprintf(stderr, "usage: emf_content_run <game_dir> [--force] [--dll <path>]\n");
    return kUsage;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring game_dir, dll;
    bool force = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--force") {
            force = true;
        } else if (a == L"--dll") {
            if (++i >= argc) return usage();
            dll = argv[i];
        } else if (a.rfind(L"--", 0) == 0 || !game_dir.empty()) {
            return usage();
        } else {
            game_dir = a;
        }
    }
    if (game_dir.empty()) return usage();
    while (game_dir.size() > 3 && (game_dir.back() == L'\\' || game_dir.back() == L'/')) game_dir.pop_back();

    if (dll.empty()) {
        dll = exe_folder() + L"\\emf_content.dll";
#ifdef EMF_CONTENT_DLL
        if (!is_file(dll)) dll = widen(EMF_CONTENT_DLL);
#endif
    }
    HMODULE module = LoadLibraryW(dll.c_str());
    if (!module) {
        std::fprintf(stderr, "emf_content_run: cannot load %s (error %lu)\n", utf8(dll).c_str(), GetLastError());
        return kNoDll;
    }
    using AbiFn = uint32_t (*)();
    using BuildFn = int32_t (*)(const char*, emf_content_log_fn, void*, char*, size_t, size_t*);
    const auto abi = reinterpret_cast<AbiFn>(GetProcAddress(module, "emf_content_abi"));
    const auto build = reinterpret_cast<BuildFn>(GetProcAddress(module, "emf_content_build"));
    if (!abi || !build || abi() != EMF_CONTENT_ABI) {
        std::fprintf(stderr, "emf_content_run: %s is not an emf_content.dll with ABI %d\n", utf8(dll).c_str(),
                     EMF_CONTENT_ABI);
        FreeLibrary(module);
        return kNoDll;
    }

    const std::string request =
        "{\"game_dir\": " + json_string(utf8(game_dir)) + ", \"force\": " + (force ? "true" : "false") + "}";
    std::vector<char> reply(256 * 1024);
    size_t len = 0;
    int32_t code = build(request.c_str(), &log_line, nullptr, reply.data(), reply.size(), &len);
    if (code == EMF_CONTENT_REPLY_TOO_SMALL) {  // rare: the build runs again with a buffer of the size it named
        reply.resize(len);
        code = build(request.c_str(), &log_line, nullptr, reply.data(), reply.size(), &len);
    }
    if (code != EMF_CONTENT_REPLY_TOO_SMALL && len > 0 && len <= reply.size()) {
        std::fwrite(reply.data(), 1, len - 1, stdout);
        std::fputc('\n', stdout);
    }
    std::fflush(stdout);
    FreeLibrary(module);
    return code;
}
