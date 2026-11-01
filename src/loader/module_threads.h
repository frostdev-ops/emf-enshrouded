#pragma once
// The threads of this process that started inside a loaded module (their Win32 start address lies in the module's
// image). A module about to be unloaded must have none: FreeLibrary would pull the code out from under them and the
// process would crash when such a thread runs again. The loader checks emf_content.dll with it after the build and
// before FreeLibrary; the tests and emf_content_run use it too. Header-only, Windows only.
#include <windows.h>

#include <tlhelp32.h>

#include <cstdint>
#include <vector>

namespace emf::loader {

// Thread ids, in no particular order; empty for a null module, or when Windows will not say.
inline std::vector<DWORD> threads_started_in(HMODULE module) {
    std::vector<DWORD> ids;
    if (!module) return ids;
    const auto base = reinterpret_cast<uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const uintptr_t end = base + nt->OptionalHeader.SizeOfImage;

    using QueryThread = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto query = ntdll ? reinterpret_cast<QueryThread>(GetProcAddress(ntdll, "NtQueryInformationThread")) : nullptr;
    if (!query) return ids;
    constexpr ULONG kThreadQuerySetWin32StartAddress = 9;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return ids;
    const DWORD pid = GetCurrentProcessId();
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!thread) continue;
        void* start = nullptr;
        if (query(thread, kThreadQuerySetWin32StartAddress, &start, sizeof start, nullptr) >= 0) {
            const auto at = reinterpret_cast<uintptr_t>(start);
            if (at >= base && at < end) ids.push_back(te.th32ThreadID);
        }
        CloseHandle(thread);
    }
    CloseHandle(snap);
    return ids;
}

}  // namespace emf::loader
