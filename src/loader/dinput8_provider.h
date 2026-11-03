#pragma once
// ShroudForge's dinput8 chain host contract (its kfc-parser fork, crates/dinput8-proxy/README.md), offered by EMF's
// loader: ShroudForge's DLL Manager (1.7.7) loads the dinput8-chain.ini lines inside mods\<folder>\ in its "Dinput8"
// mode, and first asks the game folder's dinput8.dll for ShroudforgeSetDinput8Provider. It then hands it each loaded DLL
// that has its own DirectInput8Create; the first one becomes the provider the game's DirectInput8Create goes to.
// Without this export ShroudForge refuses those lines ("needs ShroudForge's dinput8 chain host") while EMF holds
// dinput8.dll. Pure logic (unit-tested); dinput8_host.cpp exports it.
#include <windows.h>

#include <atomic>
#include <mutex>

namespace emf::loader {

// This thread is inside the provider's DirectInput8Create: a call back from it goes to System32's.
inline thread_local bool t_dinput8_in_provider = false;

class Dinput8Provider {
public:
    using Proc = FARPROC (*)(HMODULE, const char*);  // GetProcAddress
    using OwnerOf = HMODULE (*)(const void*);        // the module an address lies in (null: none)
    Dinput8Provider(Proc proc, OwnerOf owner) : proc_(proc), owner_(owner) {}

    // True when `m` is now, or already was, the provider. False for null, EMF's loader itself (`self`), System32's
    // dinput8.dll (`system`), a module without a DirectInput8Create of its own (missing, or forwarded into another
    // module), and any other module once one is set: the first keeps it.
    bool set(HMODULE m, HMODULE self, HMODULE system) {
        if (!m || m == self || (system && m == system)) return false;
        const FARPROC f = proc_ ? proc_(m, "DirectInput8Create") : nullptr;
        if (!f || !owner_ || owner_(reinterpret_cast<const void*>(f)) != m) return false;
        std::lock_guard<std::mutex> l(m_);
        if (module_) return module_ == m;
        module_ = m;
        target_.store(f);
        return true;
    }
    // The provider's DirectInput8Create, or null.
    FARPROC target() const { return target_.load(); }

    // call_provider(target) when a provider is set and this thread is not inside it already, else call_system(): a
    // provider that calls DirectInput8Create back (it reaches EMF's export again) gets System32's, never a loop.
    template <class R, class CallProvider, class CallSystem>
    R dispatch(CallProvider call_provider, CallSystem call_system) const {
        const FARPROC f = target();
        if (!f || t_dinput8_in_provider) return call_system();
        t_dinput8_in_provider = true;
        struct Reset {
            ~Reset() { t_dinput8_in_provider = false; }
        } reset;
        return call_provider(f);
    }

private:
    Proc proc_;
    OwnerOf owner_;
    std::mutex m_;
    HMODULE module_ = nullptr;
    std::atomic<FARPROC> target_{nullptr};
};

}  // namespace emf::loader
