// A stand-in for enshrouded.exe, for the content redirect spike's and the content loader's tests only
// (tests/test_file_hooks.cpp, tests/test_content_e2e.cpp); never shipped.
// The test copies it into a temp game folder as enshrouded.exe (the loader hooks only an exe of that name).
// Like the game it imports dinput8.dll statically, so EMF's loader copied next to it runs its DllMain before this
// program's own code, and the program's file calls go through its own import table, as the game's do. It writes what
// it read to standin_out.txt:
//   kfc=<the first line of enshrouded.kfc, opened by its full path with CreateFileW>
//   dat=<the first line of enshrouded_000.dat, opened relative to the current folder with CreateFileA>
//   attrs=<GetFileAttributesW of enshrouded.kfc_resources: "file" or "error <n>">
//   found=<what FindFirstFileW/FindNextFileW list for enshrouded_*.dat, comma-separated>
// With standin_check.txt (the content loader's tests: one module name per line; "wait <name>" waits up to 10 s for
// that one first) also:
//   loaded <name>=<yes|no>   for each module named there, when this program's own code ran
//   status=<EMF_CONTENT_STATUS: what the loader tells EMF's core about EML content>
//   at_tls <name>=<yes|no>   for chain_standin.dll, emf_core.dll and emf_content.dll, when this exe's TLS callback ran:
//                            after every DllMain of its static imports, before its entry point (so a DLL loaded
//                            there was loaded in a DllMain, not on a held entry point)
#include <windows.h>

#include <string>

extern "C" __declspec(dllimport) HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

namespace {
const wchar_t* const kTlsNames[3] = {L"chain_standin.dll", L"emf_core.dll", L"emf_content.dll"};
bool g_tls_ran = false;
bool g_at_tls[3] = {};

void NTAPI on_tls(PVOID, DWORD reason, PVOID) {
    if (reason != DLL_PROCESS_ATTACH || g_tls_ran) return;
    g_tls_ran = true;
    for (int i = 0; i < 3; ++i) g_at_tls[i] = GetModuleHandleW(kTlsNames[i]) != nullptr;
}
}  // namespace

#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:standin_tls_callback")
#pragma const_seg(".CRT$XLB")
extern "C" const PIMAGE_TLS_CALLBACK standin_tls_callback = on_tls;
#pragma const_seg()

namespace {
std::string first_line(HANDLE h) {
    if (h == INVALID_HANDLE_VALUE) return "error " + std::to_string(GetLastError());
    char buf[256];
    DWORD n = 0;
    const BOOL ok = ReadFile(h, buf, sizeof buf, &n, nullptr);
    CloseHandle(h);
    if (!ok) return "read error";
    std::string s(buf, n);
    return s.substr(0, s.find_first_of("\r\n"));
}
}  // namespace

int wmain() {
    // keeps dinput8.dll among the static imports, as the game has it (never called)
    volatile auto keep = &DirectInput8Create;
    (void)keep;
    wchar_t exe[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe, len);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    SetCurrentDirectoryW(dir.c_str());

    std::string out = "kfc=" + first_line(CreateFileW((dir + L"\\enshrouded.kfc").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) + "\n";
    out += "dat=" + first_line(CreateFileA("enshrouded_000.dat", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, nullptr)) + "\n";
    const DWORD a = GetFileAttributesW((dir + L"\\enshrouded.kfc_resources").c_str());
    out += "attrs=" + (a == INVALID_FILE_ATTRIBUTES ? "error " + std::to_string(GetLastError()) : std::string("file"));
    out += "\n";
    std::string found;
    WIN32_FIND_DATAW fd;
    const HANDLE f = FindFirstFileW((dir + L"\\enshrouded_*.dat").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!found.empty()) found += ",";
            for (const wchar_t* c = fd.cFileName; *c; ++c) found += static_cast<char>(*c);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    out += "found=" + found + "\n";
    const HANDLE c = CreateFileW((dir + L"\\standin_check.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (c != INVALID_HANDLE_VALUE) {
        std::string check;
        char buf[1024];
        DWORD n = 0;
        if (ReadFile(c, buf, sizeof buf, &n, nullptr)) check.assign(buf, n);
        CloseHandle(c);
        size_t from = 0;
        while (from < check.size()) {
            size_t to = check.find_first_of("\r\n", from);
            if (to == std::string::npos) to = check.size();
            std::string line = check.substr(from, to - from);
            from = to + 1;
            if (line.empty()) continue;
            const bool wait = line.rfind("wait ", 0) == 0;
            if (wait) line = line.substr(5);
            const std::wstring name(line.begin(), line.end());
            for (int i = 0; wait && i < 200 && !GetModuleHandleW(name.c_str()); ++i) Sleep(50);
            if (!wait) out += "loaded " + line + "=" + (GetModuleHandleW(name.c_str()) ? "yes" : "no") + "\n";
        }
        wchar_t status[1024] = {};
        const DWORD sn = GetEnvironmentVariableW(L"EMF_CONTENT_STATUS", status, 1024);
        std::string st;
        for (DWORD i = 0; i < sn && i < 1024; ++i) st += static_cast<char>(status[i] < 128 ? status[i] : '?');
        out += "status=" + st + "\n";
        for (int i = 0; i < 3; ++i) {
            std::string name;
            for (const wchar_t* ch = kTlsNames[i]; *ch; ++ch) name += static_cast<char>(*ch);
            out += "at_tls " + name + "=" + (g_at_tls[i] ? "yes" : "no") + "\n";
        }
    }
    const HANDLE o = CreateFileW((dir + L"\\standin_out.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (o == INVALID_HANDLE_VALUE) return 1;
    DWORD written = 0;
    WriteFile(o, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    CloseHandle(o);
    return 0;
}
