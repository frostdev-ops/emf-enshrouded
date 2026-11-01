#pragma once
// MoveFileExW that waits a moment while Windows refuses with "access denied" or "used by another process". A virus
// scanner (or the search indexer) often holds a file for a moment after it was written, and a folder cannot be renamed
// while a file in it is open: the save backup's last step, moving the finished backup folder into place, failed that
// way now and then. Other errors (a missing file, a name that exists) come back at once. On false, GetLastError()
// holds the last refusal.
#include <windows.h>

#include <string>

namespace emf::saves {

inline bool move_retrying(const std::wstring& from, const std::wstring& to, DWORD flags, DWORD wait_ms = 5000) {
    const ULONGLONG until = GetTickCount64() + wait_ms;
    for (;;) {
        if (MoveFileExW(from.c_str(), to.c_str(), flags)) return true;
        const DWORD e = GetLastError();
        const bool held = e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION;
        if (!held || GetTickCount64() >= until) {
            SetLastError(e);
            return false;
        }
        Sleep(100);
    }
}

}  // namespace emf::saves
