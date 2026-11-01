#pragma once
// Patching a module's import address table (IAT): the slot through which a module calls one imported function gets
// another function, for the life of the process. The content redirect spike (file_hooks.h) does this to enshrouded.exe
// only: the exe's own code (its statically linked C runtime included) calls kernel32 through that table, while the
// game's DLLs, EMF's core and the drivers have tables of their own that stay as they are.
//
// The loader does it in DllMain: dinput8.dll is a static import of the exe, so Windows has filled the exe's table
// before any DllMain runs, and the exe's own code (its TLS callbacks, its C runtime start-up, then WinMain) runs after.
// Imports by ordinal and delay-loaded imports are not covered.
#include <windows.h>

#include <cstdint>
#include <cstring>

namespace emf::loader {

// kernel32.dll, kernelbase.dll and the api-ms-win-core-* API sets they implement (any case): where a module imports
// the file functions from.
inline bool is_kernel_dll(const char* dll) {
    if (!dll) return false;
    if (_stricmp(dll, "kernel32.dll") == 0 || _stricmp(dll, "kernelbase.dll") == 0) return true;
    return _strnicmp(dll, "api-ms-win-core-", 16) == 0;
}

// Calls f(dll, name, slot) for every function `module` imports by name; `slot` is its entry in the IAT. A descriptor
// without its name table (an old linker's bound import) is skipped: its names cannot be read once the table is filled.
template <class F>
void for_each_import(HMODULE module, F f) {
    auto* base = reinterpret_cast<uint8_t*>(module);
    if (!base) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return;
    for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        if (!d->OriginalFirstThunk || !d->FirstThunk) continue;
        const char* dll = reinterpret_cast<const char*>(base + d->Name);
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (size_t i = 0; names[i].u1.AddressOfData; ++i) {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;
            const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
            f(dll, reinterpret_cast<const char*>(by_name->Name), reinterpret_cast<void**>(&slots[i].u1.Function));
        }
    }
}

// The protection that makes a page writable and keeps what else it allows: a page that may run code (an import table
// the linker put next to code) stays executable, since another thread may be running code on it at that moment.
inline DWORD writable_protection(DWORD current) {
    constexpr DWORD kExecute = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (current & kExecute) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
}

// Puts `hook` into `slot` (writable for the moment) and returns what was there before; nullptr when it could not.
inline void* patch_import_slot(void** slot, void* hook) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!slot || !VirtualQuery(slot, &mbi, sizeof mbi)) return nullptr;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), writable_protection(mbi.Protect), &old)) return nullptr;
    void* before = InterlockedExchangePointer(slot, hook);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), old, &ignored);
    return before;
}

}  // namespace emf::loader
