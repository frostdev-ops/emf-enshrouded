// The content redirect spike (docs/engine_parts/file_redirect_spike.md): which paths the loader sends to the shadow copy
// <game>\emf\content\client\, the lines of emf\file_opens.log, and the [content] switches in emf\emf_core.ini.
#include "loader/file_redirect.h"  // first: the header compiles on its own

#include <doctest.h>

#include <windows.h>

#include <cstdio>
#include <map>
#include <string>

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

TEST_CASE("redirect: a \\\\?\\ path is taken literally, as Windows takes it: no . or .., / is no separator") {
    auto norm = [](const wchar_t* p) {
        const auto n = normalize_path(p, L"");
        return n ? n->path : std::wstring(L"<none>");
    };
    // Windows hands a \\?\ path to the file system as it is: ".." and "." stay names, "/" is a character, "\\" an empty
    // name. The redirect does not guess what that opens: such a path is never redirected.
    CHECK(norm(L"\\\\?\\C:\\Games\\Other\\..\\Enshrouded\\enshrouded.kfc") == L"<none>");
    CHECK(norm(L"\\\\?\\C:\\Games\\.\\Enshrouded\\enshrouded.kfc") == L"<none>");
    CHECK(norm(L"\\\\?\\C:\\Games\\Enshrouded\\..") == L"<none>");
    CHECK(norm(L"\\\\?\\C:/Games/Enshrouded/enshrouded.kfc") == L"<none>");
    CHECK(norm(L"\\\\?\\C:\\Games/Enshrouded\\enshrouded.kfc") == L"<none>");
    CHECK(norm(L"\\\\?\\C:\\Games\\\\Enshrouded\\enshrouded.kfc") == L"<none>");
    CHECK(norm(L"\\\\?\\UNC/nas/games/enshrouded.kfc") == L"<none>");
    CHECK(norm(L"\\\\?\\UNC\\nas\\games\\..\\enshrouded.kfc") == L"<none>");
    // only the exact prefix \\?\ is verbatim; "//?/" and mixed forms are not redirected either
    CHECK(norm(L"//?/C:/Games/Enshrouded/enshrouded.kfc") == L"<none>");
    CHECK(norm(L"\\\\?/C:\\Games\\Enshrouded\\enshrouded.kfc") == L"<none>");
    // a name keeps its trailing dots and spaces (so "enshrouded.kfc." is not the content file)
    CHECK(norm(L"\\\\?\\C:\\Games\\Enshrouded\\enshrouded.kfc.") == L"C:\\Games\\Enshrouded\\enshrouded.kfc.");
    CHECK(norm(L"\\\\?\\C:\\Games\\Enshrouded\\") == L"C:\\Games\\Enshrouded");  // one trailing backslash: the folder
    CHECK(norm(L"\\\\?\\C:\\") == L"C:\\");
    // the plain forms stay as they were
    CHECK(norm(L"\\\\?\\C:\\Games\\Enshrouded\\enshrouded.kfc") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    CHECK(norm(L"\\\\?\\UNC\\nas\\games\\enshrouded.kfc") == L"\\\\nas\\games\\enshrouded.kfc");
    // \\.\ paths are normalized by Windows like plain ones
    CHECK(norm(L"\\\\.\\C:\\Games\\Other\\..\\Enshrouded\\enshrouded.kfc") == L"C:\\Games\\Enshrouded\\enshrouded.kfc");
    // so none of them reaches the shadow copy
    CHECK(shadow_path(L"\\\\?\\C:\\Games\\Other\\..\\Enshrouded\\enshrouded.kfc", kGame, L"").empty());
    CHECK(shadow_path(L"\\\\?\\C:\\Games\\Enshrouded\\.\\enshrouded.kfc", kGame, L"").empty());
    CHECK(shadow_path(L"\\\\?\\C:\\Games\\Enshrouded/enshrouded.kfc", kGame, L"").empty());
    CHECK(shadow_path(L"\\\\?\\C:\\Games\\Enshrouded\\enshrouded.kfc.", kGame, L"").empty());
    CHECK(shadow_path(L"\\\\?\\C:\\Games\\Enshrouded\\enshrouded.kfc", kGame, L"") ==
          L"\\\\?\\" + kShadow + L"enshrouded.kfc");
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

TEST_CASE("redirect: shadow_path_in takes a game folder that is normal already (the hooks keep it from start)") {
    using emf::loader::shadow_path_in;
    CHECK(shadow_path_in(L"C:\\Games\\Enshrouded\\enshrouded.kfc", kGame, L"") == kShadow + L"enshrouded.kfc");
    CHECK(shadow_path_in(L"c:/games/enshrouded/ENSHROUDED_001.DAT", kGame, L"") == kShadow + L"ENSHROUDED_001.DAT");
    CHECK(shadow_path_in(L"enshrouded.kfc", kGame, L"C:\\Games\\Enshrouded") == kShadow + L"enshrouded.kfc");
    CHECK(shadow_path_in(L"enshrouded.kfc", L"C:\\", L"C:\\") == L"C:\\emf\\content\\client\\enshrouded.kfc");
    CHECK(shadow_path_in(L"\\\\?\\C:\\Games\\Enshrouded\\enshrouded.kfc", kGame, L"") ==
          L"\\\\?\\" + kShadow + L"enshrouded.kfc");
    CHECK(shadow_path_in(L"C:\\Games\\enshrouded.kfc", kGame, L"").empty());
    CHECK(shadow_path_in(L"C:\\Games\\Enshrouded\\enshrouded.exe", kGame, L"").empty());
    CHECK(shadow_path_in(L"C:\\Games\\Enshrouded\\enshrouded.kfc", L"", L"").empty());
}

TEST_CASE("redirect: a game folder at a drive root gives C:\\emf\\content\\client, never C:\\\\emf") {
    using emf::loader::normal_game_dir;
    using emf::loader::path_join;
    using emf::loader::shadow_folder;
    // the loader's game folder for C:\enshrouded.exe is "C:" (the exe's path up to its last backslash)
    CHECK(normal_game_dir(L"C:").value_or(L"<none>") == L"C:\\");
    CHECK(normal_game_dir(L"C:\\").value_or(L"<none>") == L"C:\\");
    CHECK(normal_game_dir(L"C:\\Games\\Enshrouded\\").value_or(L"<none>") == L"C:\\Games\\Enshrouded");
    CHECK(normal_game_dir(L"\\\\nas\\games").value_or(L"<none>") == L"\\\\nas\\games");
    CHECK_FALSE(normal_game_dir(L"Games\\Enshrouded"));
    CHECK_FALSE(normal_game_dir(L""));
    CHECK(path_join(L"C:\\", L"emf") == L"C:\\emf");
    CHECK(path_join(L"C:\\Games", L"emf") == L"C:\\Games\\emf");
    CHECK(path_join(L"\\\\nas\\games", L"emf") == L"\\\\nas\\games\\emf");
    CHECK(shadow_folder(L"C:\\") == L"C:\\emf\\content\\client");
    CHECK(shadow_folder(L"C:") == L"C:\\emf\\content\\client");
    CHECK(shadow_path(L"C:\\enshrouded.kfc", L"C:\\", L"") == L"C:\\emf\\content\\client\\enshrouded.kfc");
    CHECK(shadow_path(L"C:\\enshrouded_003.dat", L"C:", L"") == L"C:\\emf\\content\\client\\enshrouded_003.dat");
    CHECK(shadow_path(L"\\\\?\\C:\\enshrouded.kfc", L"C:\\", L"") == L"\\\\?\\C:\\emf\\content\\client\\enshrouded.kfc");
    CHECK(shadow_path(L"enshrouded.kfc", L"C:\\", L"C:\\") == L"C:\\emf\\content\\client\\enshrouded.kfc");
    CHECK(shadow_path(L"C:\\Games\\enshrouded.kfc", L"C:\\", L"").empty());
    // a share's root as the game folder
    CHECK(shadow_path(L"\\\\nas\\games\\enshrouded.kfc", L"\\\\nas\\games", L"") ==
          L"\\\\nas\\games\\emf\\content\\client\\enshrouded.kfc");
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
    // what the game passes (the live run): GENERIC_READ, share READ|DELETE, OPEN_EXISTING
    CHECK(redirect_allowed(GENERIC_READ, OPEN_EXISTING, 0));
    CHECK(redirect_allowed(GENERIC_READ | GENERIC_EXECUTE | READ_CONTROL | SYNCHRONIZE, OPEN_EXISTING, 0));
    CHECK(redirect_allowed(FILE_READ_DATA | FILE_READ_EA | FILE_READ_ATTRIBUTES | FILE_EXECUTE, OPEN_EXISTING, 0));
    // only OPEN_EXISTING: any other disposition goes to the game's own path
    CHECK_FALSE(redirect_allowed(GENERIC_READ, OPEN_ALWAYS, 0));
    CHECK_FALSE(redirect_allowed(GENERIC_READ, 0, 0));
    CHECK_FALSE(redirect_allowed(GENERIC_READ, 99, 0));
    // an allowlist of the read rights: anything else (known or not) is not redirected
    CHECK_FALSE(redirect_allowed(GENERIC_READ | ACCESS_SYSTEM_SECURITY, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(FILE_WRITE_EA, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(WRITE_DAC, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(WRITE_OWNER, OPEN_EXISTING, 0));
    CHECK_FALSE(redirect_allowed(GENERIC_READ | 0x00000200, OPEN_EXISTING, 0));  // a bit Windows gives no name here
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

TEST_CASE("redirect: the hooks are for the game client only: an exe named enshrouded.exe") {
    using emf::loader::is_game_client_exe;
    CHECK(is_game_client_exe(L"C:\\Games\\Enshrouded\\enshrouded.exe"));
    CHECK(is_game_client_exe(L"C:\\Games\\Enshrouded\\ENSHROUDED.EXE"));
    CHECK(is_game_client_exe(L"C:/Games/Enshrouded/enshrouded.exe"));
    CHECK(is_game_client_exe(L"enshrouded.exe"));
    CHECK_FALSE(is_game_client_exe(L"C:\\Games\\Enshrouded\\enshrouded_server.exe"));
    CHECK_FALSE(is_game_client_exe(L"C:\\Games\\Enshrouded\\standin_game.exe"));
    CHECK_FALSE(is_game_client_exe(L"C:\\Games\\Enshrouded\\enshrouded.exe.bak"));
    CHECK_FALSE(is_game_client_exe(L"C:\\Games\\enshrouded.exe\\other.exe"));
    CHECK_FALSE(is_game_client_exe(L""));
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
    test_scratch::remove_at_exit(dir);  // also when a check below stops the test
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

namespace {
using emf::loader::FileStamp;
const FileStamp kExe{81234432, 133750000000000001ull};
const FileStamp kKfc{9437184, 133750000000000002ull};
const FileStamp kRes{224395264, 133750000000000003ull};

// a record as content_shadow.ps1 -Make writes it
const char* const kRecord =
    "# made by content_shadow.ps1 at 2026-11-01T10:00:00Z; content_shadow.ps1 -Remove takes out exactly what is listed\r\n"
    "folder emf\\content\r\n"
    "folder emf\\content\\client\r\n"
    "copy enshrouded.kfc\r\n"
    "copy enshrouded.kfc_resources\r\n"
    "link enshrouded_000.dat\r\n"
    "link enshrouded_001.dat\r\n"
    "# the game's files when this copy was made: name, size in bytes, last write time (UTC, a Windows file time)\r\n"
    "source enshrouded.exe 81234432 133750000000000001\r\n"
    "source enshrouded.kfc 9437184 133750000000000002\r\n"
    "source enshrouded.kfc_resources 224395264 133750000000000003\r\n"
    "dat_count 55\r\n";

// the game folder matching kRecord
emf::loader::ShadowFacts matching_facts() {
    emf::loader::ShadowFacts f;
    f.record_there = true;
    f.record = kRecord;
    f.now[0] = kExe;
    f.now[1] = kKfc;
    f.now[2] = kRes;
    f.dats_now = 55;
    return f;
}
}  // namespace

TEST_CASE("redirect: shadow.txt's record of the game's files is read defensively") {
    using emf::loader::parse_shadow_record;
    auto r = parse_shadow_record(kRecord);
    REQUIRE(r.source[0]);
    REQUIRE(r.source[1]);
    REQUIRE(r.source[2]);
    CHECK(*r.source[0] == kExe);
    CHECK(*r.source[1] == kKfc);
    CHECK(*r.source[2] == kRes);
    REQUIRE(r.dat_count);
    CHECK(*r.dat_count == 55);

    // LF only, extra spaces and tabs, names in another case, a UTF-8 mark at the start (a record saved by Notepad)
    r = parse_shadow_record(
        "\xEF\xBB\xBFsource ENSHROUDED.EXE 81234432 133750000000000001\n"
        "  source\tenshrouded.kfc   9437184 133750000000000002  \n"
        "source enshrouded.kfc_resources 224395264 133750000000000003\ndat_count 55");
    CHECK(r.source[0].value_or(FileStamp{}) == kExe);
    CHECK(r.source[1].value_or(FileStamp{}) == kKfc);
    CHECK(r.source[2].value_or(FileStamp{}) == kRes);
    CHECK(r.dat_count.value_or(0) == 55);

    // what is not exactly "source <name> <digits> <digits>" or "dat_count <digits>" counts as not recorded
    r = parse_shadow_record(
        "source enshrouded.exe 81234432\r\n"                       // too few
        "source enshrouded.kfc 9437184 1337x\r\n"                  // not a number
        "source enshrouded.kfc_resources -5 133750000000000003\r\n"  // a sign
        "source enshrouded.dll 1 2\r\n"                            // not a stamped file
        "dat_count lots\r\n");
    CHECK_FALSE(r.source[0]);
    CHECK_FALSE(r.source[1]);
    CHECK_FALSE(r.source[2]);
    CHECK_FALSE(r.dat_count);
    r = parse_shadow_record(
        "source enshrouded.exe 81234432 133750000000000001 extra\r\n"     // too many
        "source enshrouded.kfc 99999999999999999999999 1\r\n"             // too big for 64 bits
        "source enshrouded.kfc_resources 18446744073709551616 1\r\n"      // 2^64
        "dat_count 99999999999\r\n");
    CHECK_FALSE(r.source[0]);
    CHECK_FALSE(r.source[1]);
    CHECK_FALSE(r.source[2]);
    CHECK_FALSE(r.dat_count);
    CHECK(parse_shadow_record("source enshrouded.kfc 18446744073709551615 0\r\n").source[1].value_or(FileStamp{}) ==
          FileStamp{18446744073709551615ull, 0});

    // a file recorded twice with different stamps is not trusted; twice the same is fine
    r = parse_shadow_record(
        "source enshrouded.kfc 9437184 133750000000000002\r\nsource enshrouded.kfc 9437185 133750000000000002\r\n"
        "source enshrouded.kfc 9437184 133750000000000002\r\n"
        "source enshrouded.exe 81234432 133750000000000001\r\nsource enshrouded.exe 81234432 133750000000000001\r\n"
        "dat_count 55\r\ndat_count 56\r\n");
    CHECK_FALSE(r.source[1]);
    CHECK(r.source[0].value_or(FileStamp{}) == kExe);
    CHECK_FALSE(r.dat_count);

    r = parse_shadow_record("");
    CHECK_FALSE(r.source[0]);
    CHECK_FALSE(r.dat_count);
}

TEST_CASE("redirect: the shadow copy is used only while the game's files match its record") {
    using emf::loader::shadow_problem;
    CHECK(shadow_problem(matching_facts()).empty());
    const std::string again = "; run content_shadow.ps1 -Remove and -Make again";

    auto f = matching_facts();
    f.now[1] = FileStamp{kKfc.size + 4096, kKfc.written};  // a game update patched enshrouded.kfc
    CHECK(shadow_problem(f) == "content redirect off: the game changed since the shadow copy was made (enshrouded.kfc)" +
                                   again);
    f = matching_facts();
    f.now[0] = FileStamp{kExe.size, kExe.written + 1};  // the same size, written later
    CHECK(shadow_problem(f) == "content redirect off: the game changed since the shadow copy was made (enshrouded.exe)" +
                                   again);
    f = matching_facts();
    f.now[0] = FileStamp{1, 1};
    f.now[2] = std::nullopt;  // gone (or unreadable)
    f.dats_now = 56;
    CHECK(shadow_problem(f) ==
          "content redirect off: the game changed since the shadow copy was made (enshrouded.exe, no "
          "enshrouded.kfc_resources, enshrouded_NNN.dat files: 55 then, 56 now)" + again);

    f = matching_facts();
    f.record_there = false;
    f.record = std::nullopt;
    CHECK(shadow_problem(f) == "content redirect off: there is no emf\\content\\client\\shadow.txt (make the shadow copy "
                               "with content_shadow.ps1 -Make)");
    f = matching_facts();
    f.record = std::nullopt;
    CHECK(shadow_problem(f) == "content redirect off: could not read emf\\content\\client\\shadow.txt" + again);
    // a record without the stamps: -Make stopped half-way (it writes them last), or an older content_shadow.ps1
    f = matching_facts();
    f.record = "copy enshrouded.kfc\r\nlink enshrouded_000.dat\r\n";
    CHECK(shadow_problem(f) == "content redirect off: emf\\content\\client\\shadow.txt does not record the game's files "
                               "(content_shadow.ps1 -Make did not finish, or an older one made it)" + again);
    f.record = std::string(kRecord).substr(0, std::string(kRecord).find("dat_count"));
    CHECK(shadow_problem(f).find("does not record the game's files") != std::string::npos);
}

TEST_CASE("redirect: the game's containers are the enshrouded_NNN.dat files") {
    using emf::loader::is_dat_file_name;
    CHECK(is_dat_file_name(L"enshrouded_000.dat"));
    CHECK(is_dat_file_name(L"ENSHROUDED_054.DAT"));
    CHECK_FALSE(is_dat_file_name(L"enshrouded.kfc"));
    CHECK_FALSE(is_dat_file_name(L"enshrouded.kfc_resources"));
    CHECK_FALSE(is_dat_file_name(L"enshrouded_000.dat.part"));
    CHECK_FALSE(is_dat_file_name(L"enshrouded_.dat"));
}
