// src/loader/module_threads.h: the threads of this process that started inside a given module. A module about to
// be unloaded (emf_content.dll after its build) must have none, or FreeLibrary pulls code out from under them.
#include <windows.h>

#include <doctest.h>

#include <algorithm>
#include <vector>

#include "loader/module_threads.h"

namespace {

DWORD WINAPI wait_for_event(void* event) {
    WaitForSingleObject(static_cast<HANDLE>(event), 10000);
    return 0;
}

bool holds(const std::vector<DWORD>& ids, DWORD id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

}  // namespace

TEST_CASE("module threads: a thread started in this exe is found, and gone once it ends") {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    REQUIRE(event != nullptr);
    DWORD id = 0;
    HANDLE thread = CreateThread(nullptr, 0, &wait_for_event, event, 0, &id);
    REQUIRE(thread != nullptr);

    const HMODULE exe = GetModuleHandleW(nullptr);
    CHECK(holds(emf::loader::threads_started_in(exe), id));
    CHECK_FALSE(holds(emf::loader::threads_started_in(GetModuleHandleW(L"kernel32.dll")), id));

    SetEvent(event);
    WaitForSingleObject(thread, 10000);
    CloseHandle(thread);
    CloseHandle(event);
    CHECK_FALSE(holds(emf::loader::threads_started_in(exe), id));
}

TEST_CASE("module threads: no module, no threads") { CHECK(emf::loader::threads_started_in(nullptr).empty()); }
