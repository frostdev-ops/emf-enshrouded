// The content redirect spike (docs/engine_parts/file_redirect_spike.md): which paths the loader sends to the shadow copy
// <game>\emf\content\client\, the lines of emf\file_opens.log, and the [content] switches in emf\emf_core.ini.
#include <doctest.h>

#include <windows.h>

#include <cstdio>
#include <map>
#include <string>

#include "loader/file_redirect.h"
#include "test_scratch.h"

using emf::loader::is_content_file_name;
using emf::loader::normalize_path;
using emf::loader::shadow_path;

namespace {
const std::wstring kGame = L"C:\\Games\\Enshrouded";
const std::wstring kShadow = L"C:\\Games\\Enshrouded\\emf\\content\\client\\";
}  // namespace

TEST_CASE("redirect: the content files are enshrouded.kfc, enshrouded.kfc_resources and enshrouded_NNN.dat, any case") {
    CHECK(is_content_file_name(L"enshrouded.kfc"));
    CHECK(is_content_file_name(L"enshrouded.kfc_resources"));
    CHECK(is_content_file_name(L"enshrouded_000.dat"));
    CHECK(is_content_file_name(L"enshrouded_054.dat"));
    CHECK(is_content_file_name(L"ENSHROUDED_063.DAT"));
    CHECK(is_content_file_name(L"Enshrouded.KFC"));
    CHECK(is_content_file_name(L"enshrouded_1234.dat"));
    CHECK_FALSE(is_content_file_name(L"enshrouded.kfc.bak"));
    CHECK_FALSE(is_content_file_name(L"enshrouded.kfc_resources.tmp"));
    CHECK_FALSE(is_content_file_name(L"enshrouded_.dat"));
    CHECK_FALSE(is_content_file_name(L"enshrouded_00a.dat"));
    CHECK_FALSE(is_content_file_name(L"enshrouded_000.dat.part"));
    CHECK_FALSE(is_content_file_name(L"enshrouded.exe"));
    CHECK_FALSE(is_content_file_name(L"xenshrouded.kfc"));
    CHECK_FALSE(is_content_file_name(L"enshrouded_server.kfc"));
    CHECK_FALSE(is_content_file_name(L""));
}

TEST_CASE("redirect: paths are made absolute: slashes, . and .., relative to the current folder, \\\\?\\ prefixes") {
    auto norm = [](const wchar_t* p, const wchar_t* cwd = L"") {
        const auto n = normalize_path(p, cwd);
        return n ? n->path : std::wstring(L"<none>");
    };
    CHECK(norm(L"C:\\Games\\Enshrouded\\enshrouded.kfc") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L"C:/Games/Enshrouded/enshrouded.kfc") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L"C:\\Games\\\\Enshrouded\\.\\x\\..\\enshrouded.kfc") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L"C:\\Games\\Enshrouded\\") == L"C:\\Games\\Enshrouded");
    CHECK(norm(L"C:\\..\\..\\a") == L"C:\\a");  // .. stops at the root
    CHECK(norm(L"enshrouded.kfc", L"C:\\Games\\Enshrouded") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L".\\enshrouded.kfc", L"C:\\Games\\Enshrouded\\") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L"..\\Enshrouded\\enshrouded.kfc", L"C:\\Games\\Other") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L"\\Games\\Enshrouded\\enshrouded.kfc", L"D:\\x") == L"D:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L"C:enshrouded.kfc", L"c:\\Games\\Enshrouded") == L"c:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L"D:enshrouded.kfc", L"C:\\Games\\Enshrouded") == L"<none>");  // another drive's own current folder
    CHECK(norm(L"enshrouded.kfc") == L"<none>");                             // relative, and no current folder known
    CHECK(norm(L"") == L"<none>");
    // Windows drops trailing dots and spaces from a name ("enshrouded.kfc." opens enshrouded.kfc)
    CHECK(norm(L"C:\\Games\\Enshrouded\\enshrouded.kfc. ") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    // \\?\ and \\.\ prefixes, UNC paths
    const auto v = normalize_path(L"\\\\?\\C:\\Games\\Enshrouded\\enshrouded.kfc", L"");
    REQUIRE(v);
    CHECK(v->path == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(v->verbatim);
    CHECK_FALSE(normalize_path(L"C:\\a", L"")->verbatim);
    CHECK(norm(L"\\\\.\\C:\\Games\\Enshrouded\\enshrouded.kfc") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L"\\\\server\\share\\Games\\enshrouded.kfc") == L"\\\\server\\share\\Games\\enshrouded.kfc");
    CHECK(norm(L"//server/share/Games/../enshrouded.kfc") == L"\\\\server\\share\\enshrouded.kfc");
    CHECK(norm(L"\\\\?\\UNC\\server\\share\\Games\\enshrouded.kfc") == L"\\\\server\\share\\Games\\enshrouded.kfc");
    CHECK(norm(L"\\\\server\\share\\..\\..\\x") == L"\\\\server\\share\\x");  // .. never climbs above the share
    CHECK(norm(L"\\\\.\\pipe\\emf") == L"<none>");                           // a device, not a file path
    CHECK(norm(L"\\\\?\\Volume{0}\\x") == L"<none>");
    CHECK(norm(L"\\\\server") == L"<none>");
}

TEST_CASE("redirect: a content file in the game folder maps to emf\\content\\client\\<same name>") {
    CHECK(shadow_path(L"C:\\Games\\Enshrouded\\enshrouded.kfc", kGame, L"") == kShadow + L"enshrouded.kfc");
    CHECK(shadow_path(L"C:\\Games\\Enshrouded\\enshrouded.kfc_resources", kGame, L"") ==
          kShadow + L"enshrouded.kfc_resources");
    CHECK(shadow_path(L"C:\\Games\\Enshrouded\\enshrouded_017.dat", kGame, L"") == kShadow + L"enshrouded_017.dat");
    // any case, both slash kinds, the game folder written with a trailing slash or forward slashes
    CHECK(shadow_path(L"c:/games/ENSHROUDED/Enshrouded_017.DAT", kGame, L"") == kShadow + L"Enshrouded_017.DAT");
    CHECK(shadow_path(L"C:\\Games\\Enshrouded\\enshrouded.kfc", L"C:/Games/Enshrouded/", L"") ==
          kShadow + L"enshrouded.kfc");
    // relative to the current folder
    CHECK(shadow_path(L"enshrouded.kfc", kGame, L"C:\\Games\\Enshrouded") == kShadow + L"enshrouded.kfc");
    CHECK(shadow_path(L".\\enshrouded_000.dat", kGame, L"c:\\games\\enshrouded") == kShadow + L"enshrouded_000.dat");
    CHECK(shadow_path(L"enshrouded.kfc", kGame, L"C:\\Games\\Other").empty());
    CHECK(shadow_path(L"enshrouded.kfc", kGame, L"").empty());
    // a \\?\ path stays one
    CHECK(shadow_path(L"\\\\?\\C:\\Games\\Enshrouded\\enshrouded.kfc", kGame, L"") ==
          L"\\\\?\\" + kShadow + L"enshrouded.kfc");
    // a game folder on a network share
    CHECK(shadow_path(L"\\\\nas\\games\\Enshrouded\\enshrouded.kfc", L"\\\\nas\\games\\Enshrouded", L"") ==
          L"\\\\nas\\games\\Enshrouded\\emf\\content\\client\\enshrouded.kfc");
    CHECK(shadow_path(L"\\\\?\\UNC\\nas\\games\\Enshrouded\\enshrouded.kfc", L"\\\\nas\\games\\Enshrouded", L"") ==
          L"\\\\?\\UNC\\nas\\games\\Enshrouded\\emf\\content\\client\\enshrouded.kfc");
}

TEST_CASE("redirect: other folders and other files are left alone") {
    CHECK(shadow_path(L"C:\\Games\\Enshrouded\\sub\\enshrouded.kfc", kGame, L"").empty());       // a subfolder
    CHECK(shadow_path(L"C:\\Games\\enshrouded.kfc", kGame, L"").empty());                        // the parent
    CHECK(shadow_path(L"C:\\Games\\Enshrouded2\\enshrouded.kfc", kGame, L"").empty());           // a sibling
    CHECK(shadow_path(L"D:\\Games\\Enshrouded\\enshrouded.kfc", kGame, L"").empty());            // another drive
    CHECK(shadow_path(L"C:\\Games\\Enshrouded\\enshrouded.exe", kGame, L"").empty());
    CHECK(shadow_path(L"C:\\Games\\Enshrouded\\enshrouded.kfc.bak", kGame, L"").empty());
    CHECK(shadow_path(L"C:\\Games\\Enshrouded\\steam_api64.dll", kGame, L"").empty());
    // the shadow copy itself is never mapped again
    CHECK(shadow_path(kShadow + L"enshrouded.kfc", kGame, L"").empty());
    CHECK(shadow_path(L"", kGame, L"").empty());
    CHECK(shadow_path(L"C:\\Games\\Enshrouded\\enshrouded.kfc", L"", L"").empty());
}

TEST_CASE("redirect: a shadow path too long for a plain path gets the \\\\?\\ prefix") {
    const std::wstring deep = L"C:\\" + std::wstring(230, L'g');
    const std::wstring out = shadow_path(deep + L"\\enshrouded.kfc_resources", deep, L"");
    CHECK(out == L"\\\\?\\" + deep + L"\\emf\\content\\client\\enshrouded.kfc_resources");
    const std::wstring unc = L"\\\\nas\\" + std::wstring(230, L'g');
    CHECK(shadow_path(unc + L"\\enshrouded.kfc_resources", unc, L"") ==
          L"\\\\?\\UNC\\nas\\" + std::wstring(230, L'g') + L"\\emf\\content\\client\\enshrouded.kfc_resources");
}

TEST_CASE("redirect: only opens that read an existing file are redirected") {
    using emf::loader::redirect_allowed;
    CHECK(redirect_allowed(GENERIC_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL));
    CHECK(redirect_allowed(GENERIC_READ, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING));
    CHECK(redirect_allowed(FILE_READ_ATTRIBUTES | SYNCHRONIZE, OPEN_EXISTING, 0));
    CHECK(redirect_allowed(0, OPEN_EXISTING, 0));  // a query of the attributes
    CHECK(redirect_allowed(GENERIC_READ, OPEN_ALWAYS, 0));
    CHECK_FALSE(redirect_allowed(GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(FILE_WRITE_DATA, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(FILE_APPEND_DATA, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(DELETE, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(GENERIC_ALL, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(MAXIMUM_ALLOWED, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(FILE_WRITE_ATTRIBUTES, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(GENERIC_READ, CREATE_ALWAYS, 0));
    CHECK_FALSE(redirect_allowed(GENERIC_READ, TRUNCATE_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(GENERIC_READ, CREATE_NEW, 0));
    CHECK_FALSE(redirect_allowed(GENERIC_READ, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE));
}

TEST_CASE("redirect: the lines of emf\\file_opens.log") {
    using namespace emf::loader;
    const LineHead h{1234, 5678, "CreateFileW"};
    CHECK(open_line(h, L"C:\\G\\enshrouded.kfc", L"", "", GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, reinterpret_cast<HANDLE>(0x1a4), 0) ==
          "+1234ms t5678 CreateFileW \"C:\\G\\enshrouded.kfc\" access=0x80000000(GENERIC_READ) share=0x1(READ) "
          "disp=OPEN_EXISTING flags=0x80(NORMAL) -> handle 0x1a4\r\n");
    CHECK(open_line(h, L"C:\\G\\enshrouded_001.dat", L"C:\\G\\emf\\content\\client\\enshrouded_001.dat", "",
                    GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, reinterpret_cast<HANDLE>(0x2b0), 0) ==
          "+1234ms t5678 CreateFileW \"C:\\G\\enshrouded_001.dat\" => \"C:\\G\\emf\\content\\client\\enshrouded_001.dat\" "
          "access=0x80000000(GENERIC_READ) share=0x3(READ|WRITE) disp=OPEN_EXISTING "
          "flags=0x60000000(OVERLAPPED|NO_BUFFERING) -> handle 0x2b0\r\n");
    CHECK(open_line(h, L"C:\\G\\enshrouded_002.dat", L"", "not redirected: no shadow copy", 0, 0, 7, 0,
                    INVALID_HANDLE_VALUE, 2) ==
          "+1234ms t5678 CreateFileW \"C:\\G\\enshrouded_002.dat\" [not redirected: no shadow copy] access=0x0 "
          "share=0x0 disp=7 flags=0x0 -> error 2\r\n");
    CHECK(open_line(h, L"x", L"", "", FILE_READ_ATTRIBUTES | SYNCHRONIZE | FILE_WRITE_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, CREATE_ALWAYS,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_HIDDEN,
                    reinterpret_cast<HANDLE>(8), 0) ==
          "+1234ms t5678 CreateFileW \"x\" access=0x100082(SYNCHRONIZE|WRITE_DATA|READ_ATTRIBUTES) "
          "share=0x7(READ|WRITE|DELETE) disp=CREATE_ALWAYS flags=0xa000002(SEQUENTIAL_SCAN|BACKUP_SEMANTICS|HIDDEN) "
          "-> handle 0x8\r\n");
    // a path with letters outside ASCII is written as UTF-8
    CHECK(open_line({1, 2, "CreateFileA"}, L"C:\\Spiele\\\u00e9\u4e16.txt", L"", "", GENERIC_READ, 0, OPEN_EXISTING,
                    0, INVALID_HANDLE_VALUE, 3) ==
          "+1ms t2 CreateFileA \"C:\\Spiele\\\xc3\xa9\xe4\xb8\x96.txt\" access=0x80000000(GENERIC_READ) share=0x0 "
          "disp=OPEN_EXISTING flags=0x0 -> error 3\r\n");

    CHECK(attributes_line({5, 1, "GetFileAttributesW"}, L"C:\\G\\enshrouded_000.dat", L"", "", 0x20, 0) ==
          "+5ms t1 GetFileAttributesW \"C:\\G\\enshrouded_000.dat\" -> attrs 0x20\r\n");
    CHECK(attributes_line({5, 1, "GetFileAttributesW"}, L"C:\\G\\enshrouded.kfc",
                          L"C:\\G\\emf\\content\\client\\enshrouded.kfc", "", 0x20, 0) ==
          "+5ms t1 GetFileAttributesW \"C:\\G\\enshrouded.kfc\" => \"C:\\G\\emf\\content\\client\\enshrouded.kfc\" "
          "-> attrs 0x20\r\n");
    CHECK(attributes_line({5, 1, "GetFileAttributesA"}, L"C:\\G\\no", L"", "", INVALID_FILE_ATTRIBUTES, 2) ==
          "+5ms t1 GetFileAttributesA \"C:\\G\\no\" -> error 2\r\n");

    WIN32_FILE_ATTRIBUTE_DATA data{};
    data.dwFileAttributes = 0x20;
    data.nFileSizeHigh = 1;
    data.nFileSizeLow = 0x10;
    CHECK(attributes_ex_line({6, 1, "GetFileAttributesExW"}, L"C:\\G\\enshrouded_003.dat", L"", "", 0, true, &data, 0) ==
          "+6ms t1 GetFileAttributesExW \"C:\\G\\enshrouded_003.dat\" level=0 -> attrs 0x20 size 4294967312\r\n");
    CHECK(attributes_ex_line({6, 1, "GetFileAttributesExW"}, L"C:\\G\\no", L"", "", 0, false, nullptr, 2) ==
          "+6ms t1 GetFileAttributesExW \"C:\\G\\no\" level=0 -> error 2\r\n");

    CHECK(find_first_line({7, 1, "FindFirstFileW"}, L"C:\\G\\enshrouded_*.dat", "", reinterpret_cast<HANDLE>(0x10),
                          L"enshrouded_000.dat", 0) ==
          "+7ms t1 FindFirstFileW \"C:\\G\\enshrouded_*.dat\" -> handle 0x10 first \"enshrouded_000.dat\"\r\n");
    CHECK(find_first_line({7, 1, "FindFirstFileExW"}, L"C:\\G\\*", "level=1 search=0 flags=0x2",
                          reinterpret_cast<HANDLE>(0x10), L".", 0) ==
          "+7ms t1 FindFirstFileExW \"C:\\G\\*\" level=1 search=0 flags=0x2 -> handle 0x10 first \".\"\r\n");
    CHECK(find_first_line({7, 1, "FindFirstFileW"}, L"C:\\G\\none*", "", INVALID_HANDLE_VALUE, L"", 2) ==
          "+7ms t1 FindFirstFileW \"C:\\G\\none*\" -> error 2\r\n");
    CHECK(find_next_line({8, 1, "FindNextFileW"}, reinterpret_cast<HANDLE>(0x10), true, L"enshrouded_001.dat", 0) ==
          "+8ms t1 FindNextFileW handle 0x10 -> \"enshrouded_001.dat\"\r\n");
    CHECK(find_next_line({8, 1, "FindNextFileW"}, reinterpret_cast<HANDLE>(0x10), false, L"", 18) ==
          "+8ms t1 FindNextFileW handle 0x10 -> error 18\r\n");
    CHECK(set_dir_line({9, 1, "SetCurrentDirectoryW"}, L"C:\\G", true, 0) ==
          "+9ms t1 SetCurrentDirectoryW \"C:\\G\" -> ok\r\n");
    CHECK(set_dir_line({9, 1, "SetCurrentDirectoryW"}, L"C:\\nope", false, 3) ==
          "+9ms t1 SetCurrentDirectoryW \"C:\\nope\" -> error 3\r\n");
}

TEST_CASE("redirect: the [content] switches are off by default; only 1 turns one on") {
    using emf::loader::content_switches;
    std::map<std::wstring, int> ini;
    auto get = [&](const wchar_t* section, const wchar_t* key, int fallback) {
        CHECK(std::wstring(section) == L"content");
        const auto it = ini.find(key);
        return it == ini.end() ? fallback : it->second;
    };
    auto s = content_switches(get);
    CHECK_FALSE(s.log_opens);
    CHECK_FALSE(s.redirect_test);
    CHECK_FALSE(s.hooks());
    CHECK(s.log_seconds == 300);
    ini[L"log_opens"] = 1;
    s = content_switches(get);
    CHECK(s.log_opens);
    CHECK_FALSE(s.redirect_test);
    CHECK(s.hooks());
    ini[L"log_opens"] = 2;
    ini[L"redirect_test"] = 1;
    ini[L"log_seconds"] = 60;
    s = content_switches(get);
    CHECK_FALSE(s.log_opens);
    CHECK(s.redirect_test);
    CHECK(s.hooks());
    CHECK(s.log_seconds == 60);
    ini[L"log_seconds"] = 0;
    CHECK(content_switches(get).log_seconds == 300);
    ini[L"log_seconds"] = -5;
    CHECK(content_switches(get).log_seconds == 300);
}

TEST_CASE("redirect: the switches read from a real emf_core.ini; no file and no section mean off") {
    const std::wstring dir = test_scratch::temp_path(L"emf_redirect_ini");
    REQUIRE(CreateDirectoryW(dir.c_str(), nullptr));
    const std::wstring ini = dir + L"\\emf_core.ini";
    auto write = [&](const char* text) {
        FILE* f = nullptr;
        REQUIRE(_wfopen_s(&f, ini.c_str(), L"wb") == 0);
        fputs(text, f);
        fclose(f);
    };
    auto s = emf::loader::read_content_switches(ini);  // no file
    CHECK_FALSE(s.hooks());
    write("[overlay]\r\nenabled=1\r\n[debug]\r\nbridge=1\r\n");
    s = emf::loader::read_content_switches(ini);
    CHECK_FALSE(s.hooks());
    write("[overlay]\r\nenabled=1\r\n[content]\r\nredirect_test=1\r\nlog_seconds=90\r\n");
    s = emf::loader::read_content_switches(ini);
    CHECK(s.redirect_test);
    CHECK_FALSE(s.log_opens);
    CHECK(s.log_seconds == 90);
    write("[content]\r\nlog_opens=1\r\nredirect_test=0\r\n");
    s = emf::loader::read_content_switches(ini);
    CHECK(s.log_opens);
    CHECK_FALSE(s.redirect_test);
    DeleteFileW(ini.c_str());
    RemoveDirectoryW(dir.c_str());
}
