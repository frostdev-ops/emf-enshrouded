// common/move_retry.h: a rename that waits a moment while Windows says "access denied" or "used by another process".
// A virus scanner often holds a file for a moment after it was written, and a folder cannot be renamed while a file
// in it is open; the save backup's last step (moving the finished backup folder into place) and EMF.exe's settings
// writes (emf_core.ini's chat settings) failed that way now and then on a PC with such a scanner.
#include <windows.h>

#include <doctest.h>

#include <chrono>
#include <string>
#include <thread>

#include "app/game_folder.h"
#include "common/move_retry.h"
#include "test_scratch.h"

namespace {
std::wstring scratch_dir(const wchar_t* tag) {
    const std::wstring d = test_scratch::temp_path(tag, L"");
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
void write_file(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    DWORD n = 0;
    WriteFile(h, "save", 4, &n, nullptr);
    CloseHandle(h);
}
// Opens the file the way a scanner does: reading, but without letting anyone delete or rename it meanwhile.
HANDLE hold(const std::wstring& path) {
    return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}
std::string read_all(const std::wstring& path) {
    std::string text(256, ' ');
    HANDLE r = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (r == INVALID_HANDLE_VALUE) return {};
    DWORD n = 0;
    ReadFile(r, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
    CloseHandle(r);
    text.resize(n);
    return text;
}
void remove_dir(const std::wstring& d) {
    DeleteFileW((d + L"\\world0.ksd").c_str());
    RemoveDirectoryW(d.c_str());
}
}  // namespace

TEST_CASE("move_retrying: a folder whose file is held for a moment is moved once the file is let go") {
    const std::wstring from = scratch_dir(L"emf_move_retry_from");
    const std::wstring to = from + L"_done";
    write_file(from + L"\\world0.ksd");
    HANDLE h = hold(from + L"\\world0.ksd");
    REQUIRE(h != INVALID_HANDLE_VALUE);
    CHECK_FALSE(MoveFileExW(from.c_str(), to.c_str(), 0));  // the cause: refused while the file is held
    std::thread let_go([h] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CloseHandle(h);
    });
    const bool moved = emf::move_retrying(from, to, 0, 5000);
    let_go.join();
    CHECK(moved);
    CHECK(GetFileAttributesW((to + L"\\world0.ksd").c_str()) != INVALID_FILE_ATTRIBUTES);
    remove_dir(to);
    remove_dir(from);
}

TEST_CASE("move_retrying: a file held longer than the wait gives false and the refusal's error") {
    const std::wstring from = scratch_dir(L"emf_move_retry_held");
    const std::wstring to = from + L"_done";
    write_file(from + L"\\world0.ksd");
    HANDLE h = hold(from + L"\\world0.ksd");
    REQUIRE(h != INVALID_HANDLE_VALUE);
    const auto t0 = std::chrono::steady_clock::now();
    const bool moved = emf::move_retrying(from, to, 0, 300);
    const DWORD e = GetLastError();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CloseHandle(h);
    CHECK_FALSE(moved);
    CHECK((e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION));
    CHECK(ms >= 250);
    CHECK(ms < 3000);
    remove_dir(from);
    remove_dir(to);
}

TEST_CASE("move_retrying: other errors come back at once (a missing folder, a name that exists)") {
    const std::wstring missing = test_scratch::temp_path(L"emf_move_retry_missing", L"");
    const auto t0 = std::chrono::steady_clock::now();
    CHECK_FALSE(emf::move_retrying(missing, missing + L"_done", 0, 5000));
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    const std::wstring a = scratch_dir(L"emf_move_retry_a");
    const std::wstring b = scratch_dir(L"emf_move_retry_b");
    CHECK_FALSE(emf::move_retrying(a, b, 0, 5000));
    const DWORD e = GetLastError();
    CHECK((e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 1000);
    RemoveDirectoryW(a.c_str());
    RemoveDirectoryW(b.c_str());
}

TEST_CASE("write_file_whole: a file a scanner holds for a moment is still replaced once it is let go") {
    const std::wstring dir = scratch_dir(L"emf_write_whole_held");
    const std::wstring path = dir + L"\\emf_core.ini";
    write_file(path);
    HANDLE h = hold(path);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    std::thread let_go([h] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CloseHandle(h);
    });
    const auto c = emf::app::write_file_whole(path, "[chat]\r\nplayers=1\r\n", "emf\\emf_core.ini");
    let_go.join();
    CHECK(c.ok);
    CHECK(read_all(path) == "[chat]\r\nplayers=1\r\n");
    DeleteFileW(path.c_str());
    DeleteFileW((path + L".emf-new").c_str());
    RemoveDirectoryW(dir.c_str());
}
