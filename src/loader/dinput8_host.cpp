// EMF's loader as ShroudForge's dinput8 chain host (dinput8_provider.h): the export ShroudforgeSetDinput8Provider and
// the loader's own DirectInput8Create, which goes to the provider ShroudForge handed over, else to System32's (the
// probe and the chain test stand-in keep src/proxy's plain forwarding).
#include <windows.h>

#include "dinput8_provider.h"

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out,
                                                   LPUNKNOWN outer);

namespace {
HMODULE owner_of(const void* address) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &m))
        return nullptr;
    return m;
}

FARPROC proc_of(HMODULE m, const char* name) { return GetProcAddress(m, name); }

// System32's dinput8.dll if it is loaded (a module handed over is loaded, so it is when that is the one)
HMODULE system_dinput8() {
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 16) return nullptr;
    wcscat_s(path, L"\\dinput8.dll");
    return GetModuleHandleW(path);
}

emf::loader::Dinput8Provider g_provider(proc_of, owner_of);
}  // namespace

extern "C" {

BOOL WINAPI ShroudforgeSetDinput8Provider(HMODULE provider) {
    return g_provider.set(provider, owner_of(reinterpret_cast<const void*>(&owner_of)), system_dinput8()) ? TRUE : FALSE;
}

HRESULT WINAPI Loader_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    return g_provider.dispatch<HRESULT>(
        [&](FARPROC f) { return reinterpret_cast<Fn>(f)(inst, version, riid, out, outer); },
        [&] { return Proxy_DirectInput8Create(inst, version, riid, out, outer); });
}

}  // extern "C"
