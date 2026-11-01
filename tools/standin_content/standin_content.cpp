// A stand-in for emf_content.dll, for the content loader's tests only (tests/test_content_*.cpp); never shipped. It has
// the same two exports (src/loader/content_abi.h) and does what <game_dir>\standin.txt says (its first word):
//   ok           what EML does, in small: refuses a .kfc, .kfc_resources or .kfc.bak with more than one name (a hard
//                link), keeps the .kfc as .kfc.bak, writes the .kfc anew ("built kfc <n>"), writes the next container
//                (<name>_NNN.dat after the last one there, deleted first), lists the mods in mods\ (real folders only,
//                as EML) and writes built.txt: "seq=<n> chain_loaded=<yes|no> core_loaded=<yes|no>", n counted in
//                standin_seq.txt (chain_standin.dll and emf_core.dll: the e2e test's chained DLL and EMF's core)
//   slow         sleeps 2 s, then as ok
//   fail         code 2
//   mods_failed  code 1 with one mod error
//   big          a reply of 1.5 MB (more than the loader's buffer): the cut reply, as the real DLL makes it
//   thread       starts a thread in this DLL that waits for the event Local\emf_standin_content_stop_<pid>, then
//                unloads its own reference (so the loader must not unload the DLL meanwhile)
// It logs two lines through the callback: "standin: building <game_dir> (<word>)" (info) and a warning.
// Built with STANDIN_ABI=2 it says it has ABI 2 (the loader must refuse it).
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "core/json.h"
#include "loader/content_abi.h"
#include "loader/content_io.h"

#ifndef STANDIN_ABI
#define STANDIN_ABI 1
#endif

namespace abi = emf::loader::content_abi;
namespace cio = emf::loader::cio;

namespace {
HMODULE g_self = nullptr;

struct Reply {
    int32_t code = 0;
    std::string message;
    std::vector<std::pair<std::string, std::string>> mods;  // id, version
    std::vector<std::pair<std::string, std::string>> errors;  // path, error
    uint32_t ms = 0;
};

std::string reply_json(const Reply& r, bool cut) {
    std::string s = "{\"code\": " + std::to_string(r.code) + ", \"message\": " + cio::json_string(r.message) + ", \"mods\": [";
    if (!cut)
        for (size_t i = 0; i < r.mods.size(); ++i)
            s += (i ? ", " : "") + std::string("{\"id\": ") + cio::json_string(r.mods[i].first) +
                 ", \"version\": " + cio::json_string(r.mods[i].second) + "}";
    s += "], \"mod_errors\": [";
    if (!cut)
        for (size_t i = 0; i < r.errors.size(); ++i)
            s += (i ? ", " : "") + std::string("{\"id\": null, \"path\": ") + cio::json_string(r.errors[i].first) +
                 ", \"error\": " + cio::json_string(r.errors[i].second) + "}";
    s += "], \"dlls\": [], \"ms\": " + std::to_string(r.ms);
    return s + (cut ? ", \"truncated\": true}" : "}");
}

// As the real DLL: the whole reply when it fits, else a cut one with the real code, else REPLY_TOO_SMALL.
int32_t finish(Reply r, char* reply, size_t cap, size_t* len) {
    const std::string full = reply_json(r, false);
    *len = full.size() + 1;
    if (reply && cap >= full.size() + 1) {
        std::copy(full.begin(), full.end(), reply);
        reply[full.size()] = '\0';
        return r.code;
    }
    Reply cut = r;
    cut.message.clear();
    const std::string empty = reply_json(cut, true);
    if (!reply || cap < empty.size() + 1) return abi::EMF_CONTENT_REPLY_TOO_SMALL;
    const size_t room = cap - empty.size() - 1;
    cut.message = r.message.substr(0, (std::min)(room / 2, r.message.size()));  // escapes may double it
    const std::string s = reply_json(cut, true);
    std::copy(s.begin(), s.end(), reply);
    reply[s.size()] = '\0';
    return r.code;
}

// Retried for a while: a file another process just wrote may stay locked for a moment (a virus scanner).
bool write_file(const std::wstring& path, const std::string& text) {
    for (int i = 0; i < 40; ++i) {
        const HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD n = 0;
            const BOOL ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
            CloseHandle(f);
            return ok && n == text.size();
        }
        Sleep(100);
    }
    return false;
}

DWORD links_of(const std::wstring& path) {
    const HANDLE f = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    BY_HANDLE_FILE_INFORMATION info{};
    const DWORD n = GetFileInformationByHandle(f, &info) ? info.nNumberOfLinks : 0;
    CloseHandle(f);
    return n;
}

DWORD WINAPI wait_then_leave(void*) {
    const std::wstring name = L"Local\\emf_standin_content_stop_" + std::to_wstring(GetCurrentProcessId());
    HANDLE e = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    if (e) {
        WaitForSingleObject(e, 60000);
        CloseHandle(e);
    }
    FreeLibraryAndExitThread(g_self, 0);
}

void build_ok(const std::wstring& dir, Reply& r) {
    for (const wchar_t* name : {L"enshrouded.kfc", L"enshrouded.kfc_resources", L"enshrouded.kfc.bak"}) {
        if (links_of(cio::join(dir, name)) > 1) {
            r.code = abi::EMF_CONTENT_RUN_FAILED;
            r.message = cio::to_utf8(name) + " has more than one name (a hard link): refused";
            return;
        }
    }
    const auto seq_text = cio::read_file(cio::join(dir, L"standin_seq.txt"), 64).value_or("0");
    const int seq = std::atoi(seq_text.c_str()) + 1;
    write_file(cio::join(dir, L"standin_seq.txt"), std::to_string(seq));
    const std::wstring kfc = cio::join(dir, L"enshrouded.kfc"), bak = cio::join(dir, L"enshrouded.kfc.bak");
    DeleteFileW(bak.c_str());
    CopyFileW(kfc.c_str(), bak.c_str(), TRUE);
    DeleteFileW(kfc.c_str());  // EML removes a file before it writes it again
    write_file(kfc, "built kfc " + std::to_string(seq) + "\r\n");
    int next = 0;
    wchar_t dat[32];
    for (;; ++next) {
        swprintf_s(dat, L"enshrouded_%03d.dat", next);
        if (!cio::stat(cio::join(dir, dat)).exists) break;
    }
    write_file(cio::join(dir, dat), "built dat " + std::to_string(seq) + "\r\n");
    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileW(cio::join(dir, L"mods\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                continue;
            const auto text = cio::read_file(cio::join(dir, L"mods\\" + n + L"\\mod.json"), 1 << 20);
            const auto v = text ? emf::core::json::parse(*text).value : std::nullopt;
            const auto* id = v ? v->get("id") : nullptr;
            const auto* version = v ? v->get("version") : nullptr;
            if (id && version) r.mods.emplace_back(id->s, version->s);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(r.mods.begin(), r.mods.end());
    const bool chain = GetModuleHandleW(L"chain_standin.dll") != nullptr;
    const bool core = GetModuleHandleW(L"emf_core.dll") != nullptr;
    // EMF's hold writes "mov rax, imm64; jmp rax" (48 B8 ...) over the exe's entry point
    const auto* exe = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + reinterpret_cast<const IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const uint8_t* entry = exe + nt->OptionalHeader.AddressOfEntryPoint;
    const bool held = entry[0] == 0x48 && entry[1] == 0xB8;
    write_file(cio::join(dir, L"built.txt"), "seq=" + std::to_string(seq) + " chain_loaded=" + (chain ? "yes" : "no") +
                                                 " core_loaded=" + (core ? "yes" : "no") +
                                                 " entry_held=" + (held ? "yes" : "no") + "\n");
    r.message = "standin: built " + std::to_string(r.mods.size()) + " mods";
}
}  // namespace

extern "C" __declspec(dllexport) uint32_t emf_content_abi(void) { return STANDIN_ABI; }

extern "C" __declspec(dllexport) int32_t emf_content_build(const char* request_json, abi::emf_content_log_fn log,
                                                           void* log_ctx, char* reply, size_t reply_cap,
                                                           size_t* reply_len) {
    if (!reply_len) return abi::EMF_CONTENT_BAD_REQUEST;
    static std::atomic<bool> busy{false};
    Reply r;
    if (busy.exchange(true)) {
        r.code = abi::EMF_CONTENT_BAD_REQUEST;
        r.message = "a build already runs";
        return finish(r, reply, reply_cap, reply_len);
    }
    const ULONGLONG t0 = GetTickCount64();
    const auto parsed = emf::core::json::parse(request_json ? request_json : "");
    const auto* game_dir = parsed.value ? parsed.value->get("game_dir") : nullptr;
    if (!game_dir || game_dir->s.empty()) {
        r.code = abi::EMF_CONTENT_BAD_REQUEST;
        r.message = "no game_dir in the request";
    } else {
        const std::wstring dir = cio::from_utf8(game_dir->s);
        std::string word = cio::read_file(cio::join(dir, L"standin.txt"), 4096).value_or("ok");
        word = word.substr(0, word.find_first_of(" \t\r\n"));
        const auto say = [&](int level, const std::string& line) {
            if (log) log(log_ctx, level, line.c_str());
        };
        say(2, "standin: building " + game_dir->s + " (" + word + ")");
        say(1, "standin: a warning for the test");
        if (word == "fail") {
            r.code = abi::EMF_CONTENT_RUN_FAILED;
            r.message = "standin: the build failed on purpose";
        } else if (word == "mods_failed") {
            r.code = abi::EMF_CONTENT_MODS_FAILED;
            r.message = "standin: a mod could not be read";
            r.errors.emplace_back(game_dir->s + "\\mods\\broken", "broken on purpose");
        } else if (word == "big") {
            r.message.assign(1500000, 'x');
        } else if (word == "thread") {
            HMODULE ref = nullptr;  // the thread's own reference, dropped by FreeLibraryAndExitThread
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&wait_then_leave), &ref);
            if (HANDLE t = CreateThread(nullptr, 0, wait_then_leave, nullptr, 0, nullptr))
                CloseHandle(t);
            else if (ref)
                FreeLibrary(ref);
            r.message = "standin: left a thread running";
        } else {
            if (word == "slow") Sleep(2000);
            build_ok(dir, r);
        }
    }
    r.ms = static_cast<uint32_t>(GetTickCount64() - t0);
    const int32_t rc = finish(r, reply, reply_cap, reply_len);
    busy = false;
    return rc;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        // installed in a (test) game's emf folder: one file per process that loads it, so a test can count the loads
        wchar_t path[MAX_PATH];
        const std::wstring self(path, GetModuleFileNameW(inst, path, MAX_PATH));
        const std::wstring folder = self.substr(0, self.find_last_of(L'\\'));
        if (folder.size() >= 4 && _wcsicmp(folder.c_str() + folder.size() - 4, L"\\emf") == 0) {
            const std::wstring mark = folder + L"\\standin_attach_" + std::to_wstring(GetCurrentProcessId()) + L".txt";
            const HANDLE f = CreateFileW(mark.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        }
    }
    return TRUE;
}
