// A stand-in for enshrouded.exe, for the content redirect spike's test only (tests/test_file_hooks.cpp); never shipped.
// Like the game it imports dinput8.dll statically, so EMF's loader copied next to it runs its DllMain before this
// program's own code, and the program's file calls go through its own import table, as the game's do. It writes what
// it read to standin_out.txt:
//   kfc=<the first line of enshrouded.kfc, opened by its full path with CreateFileW>
//   dat=<the first line of enshrouded_000.dat, opened relative to the current folder with CreateFileA>
//   attrs=<GetFileAttributesW of enshrouded.kfc_resources: "file" or "error <n>">
//   found=<what FindFirstFileW/FindNextFileW list for enshrouded_*.dat, comma-separated>
#include <windows.h>

#include <string>

extern "C" __declspec(dllimport) HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

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
    const HANDLE o = CreateFileW((dir + L"\\standin_out.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (o == INVALID_HANDLE_VALUE) return 1;
    DWORD written = 0;
    WriteFile(o, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    CloseHandle(o);
    return 0;
}
