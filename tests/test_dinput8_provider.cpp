// ShroudForge's dinput8 chain host contract in EMF's loader: ShroudForge's DLL Manager hands the game folder's
// dinput8.dll each DLL it loads in its "Dinput8" mode; the first with its own DirectInput8Create becomes the provider
// the game's DirectInput8Create goes to (KFC crates/dinput8-proxy/README.md). Driven here with fake modules.
#include <windows.h>

#include <doctest.h>

#include <map>
#include <string>
#include <vector>

#include "loader/dinput8_provider.h"

using emf::loader::Dinput8Provider;

namespace {
HMODULE mod(uintptr_t n) { return reinterpret_cast<HMODULE>(n * 0x10000); }
FARPROC addr(uintptr_t n) { return reinterpret_cast<FARPROC>(n * 0x10000 + 0x100); }

std::map<HMODULE, FARPROC> g_exports;  // module -> its DirectInput8Create (absent: no such export)
FARPROC fake_proc(HMODULE m, const char* name) {
    if (std::string(name) != "DirectInput8Create") return nullptr;
    auto it = g_exports.find(m);
    return it == g_exports.end() ? nullptr : it->second;
}
HMODULE fake_owner(const void* a) {  // each module spans 0x10000 bytes from its base
    return reinterpret_cast<HMODULE>(reinterpret_cast<uintptr_t>(a) & ~uintptr_t(0xFFFF));
}
const HMODULE kSelf = mod(1), kSystem = mod(2);
}  // namespace

TEST_CASE("dinput8 host: the first module with its own DirectInput8Create becomes the provider; again TRUE, another FALSE") {
    g_exports = {{mod(5), addr(5)}, {mod(6), addr(6)}};
    Dinput8Provider p(fake_proc, fake_owner);
    CHECK(p.target() == nullptr);
    CHECK(p.set(mod(5), kSelf, kSystem));
    CHECK(p.target() == addr(5));
    CHECK(p.set(mod(5), kSelf, kSystem));        // the same one again
    CHECK_FALSE(p.set(mod(6), kSelf, kSystem));  // the first keeps it
    CHECK(p.target() == addr(5));
}

TEST_CASE("dinput8 host: null, EMF itself, System32's dinput8, no export, or a forwarded one are refused") {
    g_exports = {{kSelf, addr(1)}, {kSystem, addr(2)}, {mod(7), addr(8)}};  // mod(7)'s export lies in mod(8)
    Dinput8Provider p(fake_proc, fake_owner);
    CHECK_FALSE(p.set(nullptr, kSelf, kSystem));
    CHECK_FALSE(p.set(kSelf, kSelf, kSystem));
    CHECK_FALSE(p.set(kSystem, kSelf, kSystem));
    CHECK_FALSE(p.set(mod(9), kSelf, kSystem));  // no DirectInput8Create at all
    CHECK_FALSE(p.set(mod(7), kSelf, kSystem));  // not its own: forwarded into another module
    CHECK(p.target() == nullptr);
}

TEST_CASE("dinput8 host: the call goes to the provider; a call back from inside it goes to System32's") {
    g_exports = {{mod(5), addr(5)}};
    Dinput8Provider p(fake_proc, fake_owner);
    std::vector<std::string> calls;
    const auto system = [&] {
        calls.push_back("system");
        return 1;
    };
    CHECK(p.dispatch<int>([&](FARPROC) { calls.push_back("provider"); return 2; }, system) == 1);  // none set yet
    REQUIRE(p.set(mod(5), kSelf, kSystem));
    const int r = p.dispatch<int>(
        [&](FARPROC f) {
            CHECK(f == addr(5));
            calls.push_back("provider");
            // the provider calls DirectInput8Create back (it reaches EMF's export again): System32's, no loop
            return 10 + p.dispatch<int>([&](FARPROC) { calls.push_back("provider again"); return 3; }, system);
        },
        system);
    CHECK(r == 11);
    CHECK(calls == std::vector<std::string>{"system", "provider", "system"});
    calls.clear();
    CHECK(p.dispatch<int>([&](FARPROC) { calls.push_back("provider"); return 4; }, system) == 4);  // the guard is reset
    CHECK(calls == std::vector<std::string>{"provider"});
}
