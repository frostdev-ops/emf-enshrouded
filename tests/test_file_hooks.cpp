// The content redirect spike's hooks (src/loader/file_hooks.h): the loader puts them into enshrouded.exe's import table
// in DllMain. Here they run against fake originals (what they pass on, the error they leave, the lines they log), and
// once for real in emf_tests.exe's own import table.
#include "loader/iat_patch.h"  // first: the header compiles on its own

#include <doctest.h>

#include <windows.h>

#include <share.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "loader/file_hooks.h"
#include "loader/iat_patch.h"
#include "test_scratch.h"

namespace fh = emf::loader::file_hooks;

namespace {
std::mutex g_log_mutex;
std::string g_log;
void capture(const char* data, size_t n) {
    const std::lock_guard<std::mutex> l(g_log_mutex);
    g_log.append(data, n);
}
std::string logged() {
    const std::lock_guard<std::mutex> l(g_log_mutex);
    return g_log;
}

unsigned long long g_now = 0;
unsigned long long fake_now() { return g_now; }
std::wstring g_cwd = L"C:\\Games\\Enshrouded";
std::wstring fake_cwd() { return g_cwd; }
std::vector<std::wstring> g_shadow_files;  // what the fake shadow folder holds
bool fake_is_file(const wchar_t* p) {
    SetLastError(ERROR_ACCESS_DENIED);  // the real check changes the error too; the caller must not see it
    for (const auto& f : g_shadow_files)
        if (_wcsicmp(f.c_str(), p) == 0) return true;
    return false;
}

// fake originals: record the path they got, leave a known error
std::wstring g_opened;
std::string g_opened_a;
DWORD g_open_error = 0;
HANDLE g_open_result = reinterpret_cast<HANDLE>(0x1a4);
HANDLE WINAPI fake_create_w(LPCWSTR p, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) {
    g_opened = p ? p : L"<null>";
    SetLastError(g_open_error);
    return g_open_result;
}
HANDLE WINAPI fake_create_a(LPCSTR p, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) {
    g_opened_a = p ? p : "<null>";
    SetLastError(g_open_error);
    return g_open_result;
}
DWORD WINAPI fake_attrs_w(LPCWSTR p) {
    g_opened = p;
    SetLastError(g_open_error);
    return g_open_error ? INVALID_FILE_ATTRIBUTES : FILE_ATTRIBUTE_ARCHIVE;
}
DWORD WINAPI fake_attrs_a(LPCSTR p) {
    g_opened_a = p;
    SetLastError(g_open_error);
    return g_open_error ? INVALID_FILE_ATTRIBUTES : FILE_ATTRIBUTE_ARCHIVE;
}
BOOL WINAPI fake_attrs_ex_w(LPCWSTR p, GET_FILEEX_INFO_LEVELS, LPVOID out) {
    g_opened = p;
    SetLastError(g_open_error);
    if (g_open_error) return FALSE;
    auto* d = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(out);
    *d = {};
    d->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;
    d->nFileSizeLow = 4096;
    return TRUE;
}
int g_found = 0;
HANDLE WINAPI fake_find_first_w(LPCWSTR p, LPWIN32_FIND_DATAW d) {
    g_opened = p;
    g_found = 0;
    wcscpy_s(d->cFileName, L"enshrouded_000.dat");
    SetLastError(0);
    return reinterpret_cast<HANDLE>(0x77);
}
HANDLE WINAPI fake_find_first_ex_w(LPCWSTR p, FINDEX_INFO_LEVELS, LPVOID d, FINDEX_SEARCH_OPS, LPVOID, DWORD) {
    return fake_find_first_w(p, static_cast<LPWIN32_FIND_DATAW>(d));
}
BOOL WINAPI fake_find_next_w(HANDLE, LPWIN32_FIND_DATAW d) {
    if (++g_found > 1) {
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    wcscpy_s(d->cFileName, L"enshrouded_001.dat");
    SetLastError(0);
    return TRUE;
}
BOOL WINAPI fake_set_dir_w(LPCWSTR p) {
    g_opened = p;
    SetLastError(0);
    return TRUE;
}

// injected failures: a step that throws, as building a string does when memory runs out
bool throwing_is_file(const wchar_t*) { throw std::bad_alloc(); }
std::wstring throwing_cwd() { throw std::bad_alloc(); }
void throwing_write(const char*, size_t) { throw std::bad_alloc(); }

int g_clock_reads = 0;
unsigned long long counting_now() {
    ++g_clock_reads;
    return g_now;
}

const std::wstring kGame = L"C:\\Games\\Enshrouded";
const std::wstring kShadow = L"C:\\Games\\Enshrouded\\emf\\content\\client\\";

// A clean hook state with fake originals; `log_opens`, `redirect` as the ini would set them.
void setup(bool log_opens, bool redirect) {
    fh::reset_for_test();
    auto& g = fh::state();
    g.real.CreateFileW = fake_create_w;
    g.real.CreateFileA = fake_create_a;
    g.real.GetFileAttributesW = fake_attrs_w;
    g.real.GetFileAttributesA = fake_attrs_a;
    g.real.GetFileAttributesExW = fake_attrs_ex_w;
    g.real.FindFirstFileW = fake_find_first_w;
    g.real.FindFirstFileExW = fake_find_first_ex_w;
    g.real.FindNextFileW = fake_find_next_w;
    g.real.SetCurrentDirectoryW = fake_set_dir_w;
    g.game_dir = kGame;
    g.sw.log_opens = log_opens;
    g.sw.redirect_test = redirect;
    g.sw.log_seconds = 300;
    g.write = capture;
    g.now_ms = fake_now;
    g.cwd = fake_cwd;
    g.is_file = fake_is_file;
    g.start_ms = 1000;
    g_now = 1500;
    g_cwd = kGame;
    g_shadow_files.clear();
    g_opened.clear();
    g_opened_a.clear();
    g_open_error = 0;
    g_open_result = reinterpret_cast<HANDLE>(0x1a4);
    const std::lock_guard<std::mutex> l(g_log_mutex);
    g_log.clear();
}
}  // namespace

TEST_CASE("file hooks: redirect on and the shadow copy there: the open goes to the shadow path and is logged") {
    setup(false, true);
    g_shadow_files = {kShadow + L"enshrouded_017.dat"};
    SetLastError(0);
    const HANDLE h = fh::create_file_w((kGame + L"\\enshrouded_017.dat").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    CHECK(h == reinterpret_cast<HANDLE>(0x1a4));
    CHECK(g_opened == kShadow + L"enshrouded_017.dat");
    CHECK(GetLastError() == 0);  // not the shadow check's error
    CHECK(logged() == "+500ms t" + std::to_string(GetCurrentThreadId()) +
                          " CreateFileW \"C:\\Games\\Enshrouded\\enshrouded_017.dat\" => "
                          "\"C:\\Games\\Enshrouded\\emf\\content\\client\\enshrouded_017.dat\" "
                          "access=0x80000000(GENERIC_READ) share=0x1(READ) disp=OPEN_EXISTING flags=0x40000000(OVERLAPPED) "
                          "-> handle 0x1a4\r\n");
    // a relative path, as the game may open it from its own folder
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    fh::create_file_w(L"enshrouded.kfc", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(g_opened == kShadow + L"enshrouded.kfc");
    g_cwd = L"C:\\Elsewhere";
    fh::create_file_w(L"enshrouded.kfc", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(g_opened == L"enshrouded.kfc");
}

TEST_CASE("file hooks: no shadow copy, or an open that writes: the game's own path, and the log says why") {
    setup(false, true);
    g_open_error = ERROR_FILE_NOT_FOUND;
    g_open_result = INVALID_HANDLE_VALUE;
    const HANDLE h = fh::create_file_w((kGame + L"\\enshrouded_002.dat").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, 0, nullptr);
    CHECK(h == INVALID_HANDLE_VALUE);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);  // the original's error, after the log line was written
    CHECK(g_opened == kGame + L"\\enshrouded_002.dat");
    CHECK(logged().find("[not redirected: no shadow copy]") != std::string::npos);
    CHECK(logged().find("-> error 2\r\n") != std::string::npos);

    setup(false, true);
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    fh::create_file_w((kGame + L"\\enshrouded.kfc").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                      nullptr);
    CHECK(g_opened == kGame + L"\\enshrouded.kfc");
    CHECK(logged().find("[not redirected: opened to write]") != std::string::npos);
}

TEST_CASE("file hooks: redirect off: every open goes through unchanged; with log_opens each is logged") {
    setup(true, false);
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    g_open_error = ERROR_ALREADY_EXISTS;  // what a successful OPEN_ALWAYS leaves
    fh::create_file_w((kGame + L"\\enshrouded.kfc").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0,
                      nullptr);
    CHECK(GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(g_opened == kGame + L"\\enshrouded.kfc");
    fh::create_file_w(L"C:\\Users\\x\\Saved Games\\a.json", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    const std::string log = logged();
    CHECK(log.find("CreateFileW \"C:\\Games\\Enshrouded\\enshrouded.kfc\" access=") != std::string::npos);
    CHECK(log.find("=>") == std::string::npos);
    CHECK(log.find("CreateFileW \"C:\\Users\\x\\Saved Games\\a.json\"") != std::string::npos);

    // neither switch: nothing logged, nothing changed (the loader does not put the hooks in then at all)
    setup(false, false);
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    fh::create_file_w((kGame + L"\\enshrouded.kfc").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(g_opened == kGame + L"\\enshrouded.kfc");
    CHECK(logged().empty());
}

TEST_CASE("file hooks: CreateFileA logs the path as UTF-8 and redirects through the wide function") {
    setup(true, true);
    g_shadow_files = {kShadow + L"enshrouded.kfc_resources"};
    fh::create_file_a("C:\\Games\\Enshrouded\\enshrouded.kfc_resources", GENERIC_READ, FILE_SHARE_READ, nullptr,
                      OPEN_EXISTING, 0, nullptr);
    CHECK(g_opened == kShadow + L"enshrouded.kfc_resources");
    CHECK(g_opened_a.empty());  // not the ANSI original
    CHECK(logged().find("CreateFileA \"C:\\Games\\Enshrouded\\enshrouded.kfc_resources\" => ") != std::string::npos);
    g_opened.clear();
    fh::create_file_a("C:\\Games\\Enshrouded\\readme.txt", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(g_opened_a == "C:\\Games\\Enshrouded\\readme.txt");
    CHECK(g_opened.empty());
}

TEST_CASE("file hooks: attribute queries of a content file follow the redirect too") {
    setup(false, true);
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    CHECK(fh::get_file_attributes_w((kGame + L"\\enshrouded.kfc").c_str()) == FILE_ATTRIBUTE_ARCHIVE);
    CHECK(g_opened == kShadow + L"enshrouded.kfc");
    CHECK(fh::get_file_attributes_a("C:\\Games\\Enshrouded\\enshrouded.kfc") == FILE_ATTRIBUTE_ARCHIVE);
    CHECK(g_opened == kShadow + L"enshrouded.kfc");
    WIN32_FILE_ATTRIBUTE_DATA d{};
    CHECK(fh::get_file_attributes_ex_w((kGame + L"\\enshrouded.kfc").c_str(), GetFileExInfoStandard, &d));
    CHECK(g_opened == kShadow + L"enshrouded.kfc");
    CHECK(d.nFileSizeLow == 4096);
    const std::string log = logged();
    CHECK(log.find("GetFileAttributesW \"C:\\Games\\Enshrouded\\enshrouded.kfc\" => ") != std::string::npos);
    CHECK(log.find("GetFileAttributesA \"C:\\Games\\Enshrouded\\enshrouded.kfc\" => ") != std::string::npos);
    CHECK(log.find("GetFileAttributesExW \"C:\\Games\\Enshrouded\\enshrouded.kfc\" => ") != std::string::npos);
    CHECK(log.find("level=0 -> attrs 0x20 size 4096\r\n") != std::string::npos);
    // a missing file keeps its error
    setup(true, false);
    g_open_error = ERROR_PATH_NOT_FOUND;
    CHECK(fh::get_file_attributes_w(L"C:\\nope\\x") == INVALID_FILE_ATTRIBUTES);
    CHECK(GetLastError() == ERROR_PATH_NOT_FOUND);
}

TEST_CASE("file hooks: folder listings and current-folder changes are logged, never changed") {
    setup(true, true);
    WIN32_FIND_DATAW d{};
    const HANDLE h = fh::find_first_file_w((kGame + L"\\enshrouded_*.dat").c_str(), &d);
    CHECK(h == reinterpret_cast<HANDLE>(0x77));
    CHECK(g_opened == kGame + L"\\enshrouded_*.dat");
    CHECK(fh::find_next_file_w(h, &d));
    CHECK_FALSE(fh::find_next_file_w(h, &d));
    CHECK(GetLastError() == ERROR_NO_MORE_FILES);
    CHECK(fh::find_first_file_ex_w((kGame + L"\\*").c_str(), FindExInfoBasic, &d, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH) == reinterpret_cast<HANDLE>(0x77));
    CHECK(fh::set_current_directory_w(kGame.c_str()));
    const std::string log = logged();
    CHECK(log.find("FindFirstFileW \"C:\\Games\\Enshrouded\\enshrouded_*.dat\" -> handle 0x77 first "
                   "\"enshrouded_000.dat\"\r\n") != std::string::npos);
    CHECK(log.find("FindNextFileW handle 0x77 -> \"enshrouded_001.dat\"\r\n") != std::string::npos);
    CHECK(log.find("FindNextFileW handle 0x77 -> error 18\r\n") != std::string::npos);
    CHECK(log.find("FindFirstFileExW \"C:\\Games\\Enshrouded\\*\" level=1 search=0 flags=0x2 -> handle 0x77") !=
          std::string::npos);
    CHECK(log.find("SetCurrentDirectoryW \"C:\\Games\\Enshrouded\" -> ok\r\n") != std::string::npos);
}

TEST_CASE("file hooks: after log_seconds only the content files are logged; the log stops at its line limit") {
    setup(true, true);
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    g_now = 1000 + 300 * 1000;  // the window is over
    fh::create_file_w(L"C:\\Users\\x\\a.json", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    fh::create_file_w(L"C:\\Users\\x\\b.json", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    fh::create_file_w((kGame + L"\\enshrouded.kfc").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    std::string log = logged();
    CHECK(log.find("a.json") == std::string::npos);
    CHECK(log.find("enshrouded.kfc\" => ") != std::string::npos);
    const size_t note = log.find("# the logging window (300 s, [content] log_seconds) is over");
    CHECK(note != std::string::npos);
    CHECK(log.find("# the logging window", note + 1) == std::string::npos);  // said once

    setup(true, false);
    fh::state().max_lines = 2;
    for (int i = 0; i < 5; ++i)
        fh::create_file_w(L"C:\\Users\\x\\a.json", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    log = logged();
    size_t lines = 0;
    for (size_t at = log.find("CreateFileW"); at != std::string::npos; at = log.find("CreateFileW", at + 1)) ++lines;
    CHECK(lines == 2);
    const size_t stop = log.find("# the log stops here: 2 lines");
    CHECK(stop != std::string::npos);
    CHECK(log.find("# the log stops here", stop + 1) == std::string::npos);
}

TEST_CASE("file hooks: once the log is full (or there is none) a call does not build its line") {
    setup(false, true);
    auto& g = fh::state();
    g.now_ms = counting_now;  // each line's head reads the clock: a line not built does not
    g.max_lines = 1;
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    g_clock_reads = 0;
    fh::create_file_w((kGame + L"\\enshrouded.kfc").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(g_clock_reads == 1);
    g_clock_reads = 0;
    for (int i = 0; i < 3; ++i)
        fh::create_file_w((kGame + L"\\enshrouded.kfc").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(g_clock_reads == 0);
    CHECK(g_opened == kShadow + L"enshrouded.kfc");  // the redirect goes on all the same
    const std::string log = logged();
    CHECK(log.find("CreateFileW") == log.rfind("CreateFileW"));  // one line
    CHECK(log.find("# the log stops here: 1 lines") != std::string::npos);

    setup(false, true);
    fh::state().write = nullptr;  // no log file
    fh::state().now_ms = counting_now;
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    g_clock_reads = 0;
    fh::create_file_w((kGame + L"\\enshrouded.kfc").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(g_clock_reads == 0);
    CHECK(g_opened == kShadow + L"enshrouded.kfc");
}

TEST_CASE("file hooks: a step that throws never leaves a hook: the call goes through unchanged, with its own error") {
    const std::wstring kfc = kGame + L"\\enshrouded.kfc";
    // deciding throws (the shadow check, the current folder): the game's own path
    setup(true, true);
    fh::state().is_file = throwing_is_file;
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    g_open_error = ERROR_SHARING_VIOLATION;
    HANDLE h = nullptr;
    CHECK_NOTHROW(h = fh::create_file_w(kfc.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    CHECK(GetLastError() == ERROR_SHARING_VIOLATION);
    CHECK(h == reinterpret_cast<HANDLE>(0x1a4));
    CHECK(g_opened == kfc);
    CHECK_NOTHROW(h = fh::create_file_a("C:\\Games\\Enshrouded\\enshrouded.kfc", GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                        0, nullptr));
    CHECK(GetLastError() == ERROR_SHARING_VIOLATION);
    CHECK(g_opened_a == "C:\\Games\\Enshrouded\\enshrouded.kfc");
    DWORD a = 0;
    CHECK_NOTHROW(a = fh::get_file_attributes_w(kfc.c_str()));
    CHECK(a == INVALID_FILE_ATTRIBUTES);
    CHECK(GetLastError() == ERROR_SHARING_VIOLATION);
    CHECK(g_opened == kfc);
    g_opened_a.clear();
    CHECK_NOTHROW(fh::get_file_attributes_a("C:\\Games\\Enshrouded\\enshrouded.kfc"));
    CHECK(g_opened_a == "C:\\Games\\Enshrouded\\enshrouded.kfc");
    WIN32_FILE_ATTRIBUTE_DATA data{};
    g_opened.clear();
    CHECK_NOTHROW(fh::get_file_attributes_ex_w(kfc.c_str(), GetFileExInfoStandard, &data));
    CHECK(GetLastError() == ERROR_SHARING_VIOLATION);
    CHECK(g_opened == kfc);
    fh::state().is_file = fake_is_file;
    fh::state().cwd = throwing_cwd;  // a relative path needs it
    CHECK_NOTHROW(fh::create_file_w(L"enshrouded.kfc", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    CHECK(g_opened == L"enshrouded.kfc");
    CHECK(GetLastError() == ERROR_SHARING_VIOLATION);

    // logging throws (after the real call): the result and its error stand
    setup(true, true);
    fh::state().write = throwing_write;
    g_shadow_files = {kShadow + L"enshrouded.kfc"};
    g_open_error = ERROR_ALREADY_EXISTS;
    CHECK_NOTHROW(h = fh::create_file_w(kfc.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    CHECK(h == reinterpret_cast<HANDLE>(0x1a4));
    CHECK(GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(g_opened == kShadow + L"enshrouded.kfc");
    CHECK_NOTHROW(fh::create_file_a("C:\\x.txt", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    CHECK(GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK_NOTHROW(fh::get_file_attributes_w(L"C:\\x.txt"));
    CHECK_NOTHROW(fh::get_file_attributes_a("C:\\x.txt"));
    CHECK_NOTHROW(fh::get_file_attributes_ex_w(L"C:\\x.txt", GetFileExInfoStandard, &data));
    WIN32_FIND_DATAW fd{};
    CHECK_NOTHROW(h = fh::find_first_file_w(L"C:\\x\\*", &fd));
    CHECK(h == reinterpret_cast<HANDLE>(0x77));
    CHECK_NOTHROW(fh::find_first_file_ex_w(L"C:\\x\\*", FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0));
    CHECK_NOTHROW(fh::find_next_file_w(h, &fd));
    BOOL ok = FALSE;
    CHECK_NOTHROW(ok = fh::set_current_directory_w(L"C:\\x"));
    CHECK(ok);
    CHECK(GetLastError() == 0);
}

TEST_CASE("file hooks: nothing is made before start(): the hooks' state comes with them") {
    fh::discard_for_test();  // as the DLL is when it loads
    CHECK_FALSE(fh::has_state());
    const std::wstring game = test_scratch::temp_path(L"emf_file_hooks_state");
    test_scratch::remove_at_exit(game);
    REQUIRE(CreateDirectoryW(game.c_str(), nullptr));
    REQUIRE(CreateDirectoryW((game + L"\\emf").c_str(), nullptr));
    emf::loader::ContentSwitches sw;
    sw.log_opens = true;
    const HMODULE self = GetModuleHandleW(nullptr);
    fh::start(self, game, sw);
    CHECK(fh::has_state());
    fh::uninstall_for_test(self);
    fh::close_log_for_test();
    fh::reset_for_test();
}

TEST_CASE("file hooks: the exe's file functions come from kernel32 or its API sets") {
    using emf::loader::is_kernel_dll;
    CHECK(is_kernel_dll("KERNEL32.dll"));
    CHECK(is_kernel_dll("kernel32.DLL"));
    CHECK(is_kernel_dll("KernelBase.dll"));
    CHECK(is_kernel_dll("api-ms-win-core-file-l1-1-0.dll"));
    CHECK(is_kernel_dll("API-MS-WIN-CORE-FILE-L2-1-0.DLL"));
    CHECK_FALSE(is_kernel_dll("USER32.dll"));
    CHECK_FALSE(is_kernel_dll("kernel32x.dll"));
    CHECK_FALSE(is_kernel_dll("api-ms-win-crt-stdio-l1-1-0.dll"));
    CHECK_FALSE(is_kernel_dll(""));
}

TEST_CASE("file hooks: an import slot is made writable for the moment with its execute right kept") {
    using emf::loader::writable_protection;
    // a slot on a page that also holds code stays executable while it is written (another thread may run that code)
    CHECK(writable_protection(PAGE_EXECUTE_READ) == PAGE_EXECUTE_READWRITE);
    CHECK(writable_protection(PAGE_EXECUTE) == PAGE_EXECUTE_READWRITE);
    CHECK(writable_protection(PAGE_EXECUTE_WRITECOPY) == PAGE_EXECUTE_READWRITE);
    CHECK(writable_protection(PAGE_EXECUTE_READWRITE) == PAGE_EXECUTE_READWRITE);
    CHECK(writable_protection(PAGE_READONLY) == PAGE_READWRITE);
    CHECK(writable_protection(PAGE_READWRITE) == PAGE_READWRITE);
    CHECK(writable_protection(PAGE_WRITECOPY) == PAGE_READWRITE);
    CHECK(writable_protection(PAGE_EXECUTE_READ | PAGE_GUARD) == PAGE_EXECUTE_READWRITE);

    // a slot on an executable page: written, and the page is as it was afterwards
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    auto* page = static_cast<void**>(VirtualAlloc(nullptr, si.dwPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    REQUIRE(page);
    int a = 0, b = 0;
    page[3] = &a;
    DWORD old = 0;
    REQUIRE(VirtualProtect(page, si.dwPageSize, PAGE_EXECUTE_READ, &old));
    CHECK(emf::loader::patch_import_slot(&page[3], &b) == &a);
    CHECK(page[3] == &b);
    MEMORY_BASIC_INFORMATION mbi{};
    REQUIRE(VirtualQuery(&page[3], &mbi, sizeof mbi));
    CHECK(mbi.Protect == PAGE_EXECUTE_READ);
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST_CASE("file hooks: emf_tests.exe's own import table: GetFileAttributesW is found under KERNEL32.dll") {
    const HMODULE self = GetModuleHandleW(nullptr);
    void** slot = nullptr;
    std::string dll;
    emf::loader::for_each_import(self, [&](const char* d, const char* name, void** s) {
        if (std::string(name) == "GetFileAttributesW" && emf::loader::is_kernel_dll(d)) {
            slot = s;
            dll = d;
        }
    });
    REQUIRE(slot);
    CHECK(_stricmp(dll.c_str(), "KERNEL32.dll") == 0);
    CHECK(*slot == reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFileAttributesW")));
}

TEST_CASE("file hooks: installed into emf_tests.exe's import table, calls go through them; the header says what") {
    const HMODULE self = GetModuleHandleW(nullptr);
    fh::reset_for_test();
    auto& g = fh::state();
    g.game_dir = kGame;
    g.sw.log_opens = true;
    g.sw.log_seconds = 300;
    g.write = capture;
    // never the real shadow check here: it calls GetFileAttributesW through this exe's own table, which is hooked now
    g.is_file = fake_is_file;
    {
        const std::lock_guard<std::mutex> l(g_log_mutex);
        g_log.clear();
    }
    const fh::Installed inst = fh::install(self);
    // this call is compiled as a call through emf_tests.exe's import table, like the game's own calls
    const DWORD attrs = ::GetFileAttributesW(L"C:\\emf-no-such-folder\\enshrouded.kfc");
    const DWORD err = GetLastError();
    fh::uninstall_for_test(self);
    ::GetFileAttributesW(L"C:\\emf-no-such-folder\\after.txt");
    CHECK(attrs == INVALID_FILE_ATTRIBUTES);
    CHECK((err == ERROR_PATH_NOT_FOUND || err == ERROR_FILE_NOT_FOUND));
    const std::string log = logged();
    CHECK(log.find("GetFileAttributesW \"C:\\emf-no-such-folder\\enshrouded.kfc\" -> error ") != std::string::npos);
    CHECK(log.find("after.txt") == std::string::npos);  // the slot is back
    bool hooked = false;
    for (const auto& h : inst.hooked) hooked = hooked || h == "GetFileAttributesW (KERNEL32.dll)";
    CHECK(hooked);
    const std::string header = fh::header_text(&inst, L"C:\\Games\\Enshrouded\\enshrouded.exe", 0x6720a1b2, kGame,
                                               false, g.sw, "", "2026-11-01 14:03:22");
    CHECK(header.find("# EMF file-open log") == 0);
    CHECK(header.find("# started 2026-11-01 14:03:22 local time; exe \"C:\\Games\\Enshrouded\\enshrouded.exe\" "
                      "(PE time stamp 0x6720a1b2)\r\n") != std::string::npos);
    CHECK(header.find("# switches ([content] in emf\\emf_core.ini): log_opens=1 log_seconds=300 redirect_test=0\r\n") !=
          std::string::npos);
    CHECK(header.find("GetFileAttributesW (KERNEL32.dll)") != std::string::npos);
    CHECK(header.find("# the exe imports from: ") != std::string::npos);
    CHECK(header.find("KERNEL32.dll") != std::string::npos);
    CHECK(header.find("\"C:\\Games\\Enshrouded\\emf\\content\\client\": not there") != std::string::npos);
}

TEST_CASE("file hooks: the loader's step writes emf\\file_opens.log: the header first, then the calls") {
    const std::wstring game = test_scratch::temp_path(L"emf_file_hooks_game");
    test_scratch::remove_at_exit(game);
    REQUIRE(CreateDirectoryW(game.c_str(), nullptr));
    REQUIRE(CreateDirectoryW((game + L"\\emf").c_str(), nullptr));
    const HMODULE self = GetModuleHandleW(nullptr);
    fh::reset_for_test();
    // never the real shadow check here: it calls GetFileAttributesW through this exe's own table, which start() hooks
    fh::state().is_file = fake_is_file;
    emf::loader::ContentSwitches sw;
    sw.log_opens = true;
    const std::vector<std::string> lines = fh::start(self, game + L"\\", sw);
    ::GetFileAttributesW((game + L"\\enshrouded.kfc").c_str());  // through the hook: not there
    fh::uninstall_for_test(self);
    fh::close_log_for_test();
    fh::reset_for_test();
    REQUIRE(lines.size() == 1);  // with redirect_test=0 the shadow copy is not checked
    const std::string& line = lines[0];
    CHECK(line.find("content spike: hooked ") == 0);
    CHECK(line.find("the log is emf\\file_opens.log") != std::string::npos);
    std::string text;
    if (FILE* f = _wfsopen((game + L"\\emf\\file_opens.log").c_str(), L"rb", _SH_DENYNO)) {
        char buf[4096];
        size_t n = 0;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        fclose(f);
    }
    CHECK(text.find("# EMF file-open log") == 0);
    CHECK(text.find("# switches ([content] in emf\\emf_core.ini): log_opens=1 log_seconds=300 redirect_test=0") !=
          std::string::npos);
    CHECK(text.find("GetFileAttributesW \"" + emf::loader::utf8(game) + "\\enshrouded.kfc\" -> error 2\r\n") !=
          std::string::npos);
    DeleteFileW((game + L"\\emf\\file_opens.log").c_str());
    RemoveDirectoryW((game + L"\\emf").c_str());
    RemoveDirectoryW(game.c_str());
}

namespace {
std::wstring build_dir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    const std::wstring exe(buf, n);
    return exe.substr(0, exe.find_last_of(L'\\'));
}

void put_file(const std::wstring& path, const std::string& text) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    FILE* f = nullptr;
    REQUIRE(_wfopen_s(&f, path.c_str(), L"wb") == 0);
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

std::string file_text(const std::wstring& path) {
    std::string text;
    if (FILE* f = _wfsopen(path.c_str(), L"rb", _SH_DENYNO)) {
        char buf[4096];
        size_t n = 0;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        fclose(f);
    }
    return text;
}

// A temp game folder with the stand-in game (tools/standin_game) under the game's name (or `exe_name`), EMF's built
// dinput8.dll and fake content files.
struct StandinGame {
    std::wstring dir;
    std::wstring exe;
    explicit StandinGame(const wchar_t* name, const wchar_t* exe_name = L"enshrouded.exe")
        : dir(test_scratch::temp_path(name)), exe(exe_name) {
        test_scratch::remove_at_exit(dir);
        std::filesystem::create_directories(dir + L"\\emf");
        std::filesystem::copy_file(build_dir() + L"\\standin_game\\standin_game.exe", dir + L"\\" + exe);
        std::filesystem::copy_file(build_dir() + L"\\dist\\dinput8.dll", dir + L"\\dinput8.dll");
        put_file(dir + L"\\enshrouded.kfc", "vanilla kfc\r\n");
        put_file(dir + L"\\enshrouded.kfc_resources", "vanilla resources\r\n");
        put_file(dir + L"\\enshrouded_000.dat", "vanilla dat 0\r\n");
        put_file(dir + L"\\enshrouded_001.dat", "vanilla dat 1\r\n");
    }
    ~StandinGame() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    // runs the stand-in and returns its standin_out.txt
    std::string run() {
        std::wstring cmd = L"\"" + dir + L"\\" + exe + L"\"";
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        REQUIRE(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si,
                               &pi));
        const DWORD w = WaitForSingleObject(pi.hProcess, 30000);
        if (w != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
        DWORD code = 99;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CHECK(w == WAIT_OBJECT_0);
        CHECK(code == 0);
        return file_text(dir + L"\\standin_out.txt");
    }
};

// "source <name> <size> <last write time>" for a file as it is now, as content_shadow.ps1 -Make records it.
std::string source_line(const std::wstring& dir, const char* name) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    const std::wstring path = dir + L"\\" + std::wstring(name, name + std::strlen(name));
    REQUIRE(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d));
    const unsigned long long size = (static_cast<unsigned long long>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    const unsigned long long written =
        (static_cast<unsigned long long>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
    return "source " + std::string(name) + " " + std::to_string(size) + " " + std::to_string(written) + "\r\n";
}

// shadow.txt for the game folder `dir` as it is now, with `dats` containers.
std::string record_for(const std::wstring& dir, unsigned dats) {
    return "# made by the test\r\ncopy enshrouded.kfc\r\nlink enshrouded_000.dat\r\n" + source_line(dir, "enshrouded.exe") +
           source_line(dir, "enshrouded.kfc") + source_line(dir, "enshrouded.kfc_resources") + "dat_count " +
           std::to_string(dats) + "\r\n";
}

std::string lines_text(const std::vector<std::string>& lines) {
    std::string s;
    for (const auto& l : lines) s += l + "\n";
    return s;
}

// fh::start() on emf_tests.exe's own imports, then the hooks out again: the lines for emf_loader.log, whether the
// redirect stayed on, how many slots were hooked.
std::vector<std::string> start_and_stop(const std::wstring& game, const emf::loader::ContentSwitches& sw, bool* redirect,
                                        size_t* hooked) {
    const HMODULE self = GetModuleHandleW(nullptr);
    fh::reset_for_test();
    // never the real shadow check here: it calls GetFileAttributesW through this exe's own table, which start() hooks
    fh::state().is_file = fake_is_file;
    const auto lines = fh::start(self, game, sw);
    *redirect = fh::state().sw.redirect_test;
    *hooked = fh::state().patched.size();
    fh::uninstall_for_test(self);
    fh::close_log_for_test();
    fh::reset_for_test();
    return lines;
}
}  // namespace

TEST_CASE("file hooks: with redirect_test=1 start() first checks the shadow copy's record against the game's files") {
    const std::wstring game = test_scratch::temp_path(L"emf_file_hooks_stamps");
    test_scratch::remove_at_exit(game);
    put_file(game + L"\\enshrouded.exe", "exe\r\n");
    put_file(game + L"\\enshrouded.kfc", "kfc\r\n");
    put_file(game + L"\\enshrouded.kfc_resources", "resources\r\n");
    put_file(game + L"\\enshrouded_000.dat", "dat 0\r\n");
    put_file(game + L"\\enshrouded_001.dat", "dat 1\r\n");
    std::filesystem::create_directories(game + L"\\emf\\content\\client");
    const std::wstring opens_log = game + L"\\emf\\file_opens.log";
    emf::loader::ContentSwitches sw;
    sw.redirect_test = true;
    bool redirect = true;
    size_t hooked = 99;

    // no record: no redirect, and with log_opens=0 nothing left to do, so no hooks
    auto lines = start_and_stop(game, sw, &redirect, &hooked);
    CHECK_FALSE(redirect);
    CHECK(hooked == 0);
    CHECK(lines_text(lines).find("content redirect off: there is no emf\\content\\client\\shadow.txt") != std::string::npos);
    std::string log = file_text(opens_log);
    CHECK(log.find("# EMF file-open log") == 0);
    CHECK(log.find("# content redirect off: there is no emf\\content\\client\\shadow.txt") != std::string::npos);
    CHECK(log.find("# no hooks: ") != std::string::npos);

    // a record that matches the game's files
    put_file(game + L"\\emf\\content\\client\\shadow.txt", record_for(game, 2));
    lines = start_and_stop(game, sw, &redirect, &hooked);
    CHECK(redirect);
    CHECK(hooked > 0);
    CHECK(lines_text(lines).find("content redirect off") == std::string::npos);
    CHECK(file_text(opens_log).find("content redirect off") == std::string::npos);

    // a game update patched enshrouded.kfc: no redirect; with log_opens=1 the hooks still log
    put_file(game + L"\\enshrouded.kfc", "kfc after an update\r\n");
    sw.log_opens = true;
    lines = start_and_stop(game, sw, &redirect, &hooked);
    CHECK_FALSE(redirect);
    CHECK(hooked > 0);
    CHECK(lines_text(lines).find("content redirect off: the game changed since the shadow copy was made (enshrouded.kfc); "
                                 "run content_shadow.ps1 -Remove and -Make again") != std::string::npos);
    log = file_text(opens_log);
    CHECK(log.find("# content redirect off: the game changed since the shadow copy was made (enshrouded.kfc)") !=
          std::string::npos);
    CHECK(log.find("redirect_test=1") != std::string::npos);  // the switches as the ini has them
    CHECK(log.find("# no hooks") == std::string::npos);

    // a new container after a record that matched
    put_file(game + L"\\emf\\content\\client\\shadow.txt", record_for(game, 2));
    put_file(game + L"\\enshrouded_002.dat", "dat 2\r\n");
    lines = start_and_stop(game, sw, &redirect, &hooked);
    CHECK_FALSE(redirect);
    CHECK(lines_text(lines).find("(enshrouded_NNN.dat files: 2 then, 3 now)") != std::string::npos);
}

TEST_CASE("file hooks: end to end: EMF's loader hooks a stand-in game's imports in DllMain and redirects to the shadow") {
    REQUIRE(std::filesystem::exists(build_dir() + L"\\standin_game\\standin_game.exe"));
    StandinGame g(L"emf_standin_game_on");
    put_file(g.dir + L"\\emf\\emf_core.ini", "[content]\r\nlog_opens=1\r\nredirect_test=1\r\n");
    put_file(g.dir + L"\\emf\\content\\client\\enshrouded.kfc", "shadow kfc\r\n");
    put_file(g.dir + L"\\emf\\content\\client\\enshrouded_000.dat", "shadow dat 0\r\n");
    put_file(g.dir + L"\\emf\\content\\client\\shadow.txt", record_for(g.dir, 2));
    const std::string out = g.run();
    CHECK(out.find("kfc=shadow kfc\n") != std::string::npos);
    CHECK(out.find("dat=shadow dat 0\n") != std::string::npos);  // relative, with CreateFileA
    CHECK(out.find("attrs=file\n") != std::string::npos);
    CHECK(out.find("found=enshrouded_000.dat,enshrouded_001.dat\n") != std::string::npos);  // listings are not redirected
    const std::string log = file_text(g.dir + L"\\emf\\file_opens.log");
    CHECK(log.find("# EMF file-open log") == 0);
    CHECK(log.find("CreateFileW (KERNEL32.dll)") != std::string::npos);
    CHECK(log.find("\\enshrouded.kfc\" => \"") != std::string::npos);
    CHECK(log.find("CreateFileA \"enshrouded_000.dat\" => \"") != std::string::npos);
    CHECK(log.find("\\emf\\content\\client\\enshrouded_000.dat\"") != std::string::npos);
    CHECK(log.find("GetFileAttributesW \"") != std::string::npos);
    CHECK(log.find("enshrouded.kfc_resources\" [not redirected: no shadow copy] -> attrs 0x") != std::string::npos);
    CHECK(log.find("FindFirstFileW \"") != std::string::npos);
    CHECK(file_text(g.dir + L"\\emf\\emf_loader.log").find("content spike: hooked ") != std::string::npos);
    CHECK(file_text(g.dir + L"\\enshrouded.kfc") == "vanilla kfc\r\n");  // the game's own file is untouched
}

TEST_CASE("file hooks: end to end: a shadow copy older than the game's files is not used, and both logs say why") {
    REQUIRE(std::filesystem::exists(build_dir() + L"\\standin_game\\standin_game.exe"));
    StandinGame g(L"emf_standin_game_stale");
    put_file(g.dir + L"\\emf\\emf_core.ini", "[content]\r\nlog_opens=1\r\nredirect_test=1\r\n");
    put_file(g.dir + L"\\emf\\content\\client\\enshrouded.kfc", "shadow kfc\r\n");
    put_file(g.dir + L"\\emf\\content\\client\\shadow.txt", record_for(g.dir, 2));
    put_file(g.dir + L"\\enshrouded.kfc", "vanilla kfc after an update\r\n");  // Steam updated the game since
    const std::string out = g.run();
    CHECK(out.find("kfc=vanilla kfc after an update\n") != std::string::npos);
    const std::string why = "content redirect off: the game changed since the shadow copy was made (enshrouded.kfc); "
                            "run content_shadow.ps1 -Remove and -Make again";
    const std::string log = file_text(g.dir + L"\\emf\\file_opens.log");
    CHECK(log.find("# " + why + "\r\n") != std::string::npos);
    CHECK(log.find(" => ") == std::string::npos);
    CHECK(file_text(g.dir + L"\\emf\\emf_loader.log").find("emf_loader: " + why + "\n") != std::string::npos);
}

TEST_CASE("file hooks: end to end: without the [content] switches the loader hooks nothing") {
    REQUIRE(std::filesystem::exists(build_dir() + L"\\standin_game\\standin_game.exe"));
    StandinGame g(L"emf_standin_game_off");
    put_file(g.dir + L"\\emf\\emf_core.ini", "[overlay]\r\nenabled=1\r\n");
    put_file(g.dir + L"\\emf\\content\\client\\enshrouded.kfc", "shadow kfc\r\n");
    const std::string out = g.run();
    CHECK(out.find("kfc=vanilla kfc\n") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(g.dir + L"\\emf\\file_opens.log"));
    CHECK(file_text(g.dir + L"\\emf\\emf_loader.log").find("content spike") == std::string::npos);
}

TEST_CASE("file hooks: end to end: an exe with another name than enshrouded.exe gets no hooks, even with the switches") {
    REQUIRE(std::filesystem::exists(build_dir() + L"\\standin_game\\standin_game.exe"));
    StandinGame g(L"emf_standin_game_other", L"standin_game.exe");
    put_file(g.dir + L"\\emf\\emf_core.ini", "[content]\r\nlog_opens=1\r\nredirect_test=1\r\n");
    put_file(g.dir + L"\\emf\\content\\client\\enshrouded.kfc", "shadow kfc\r\n");
    const std::string out = g.run();
    CHECK(out.find("kfc=vanilla kfc\n") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(g.dir + L"\\emf\\file_opens.log"));
    CHECK(file_text(g.dir + L"\\emf\\emf_loader.log").find("content spike") == std::string::npos);
}
