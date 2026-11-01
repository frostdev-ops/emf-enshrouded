#pragma once
// The small window a start that builds EML content shows on the held main thread (content_start.h): a build takes
// about 30 s, and without it the game would seem not to start. It is topmost but never active (it never takes the
// focus the game's own window gets next), shows one line of what it does and the build's last log line, and goes
// before the game's code runs. run_with_splash runs the build on a worker thread and keeps the window alive meanwhile.
#include <windows.h>

#include <functional>
#include <memory>
#include <string>

#include "content_io.h"

namespace emf::loader {

class ContentSplash {
public:
    ContentSplash() = default;
    ContentSplash(const ContentSplash&) = delete;
    ContentSplash& operator=(const ContentSplash&) = delete;
    ~ContentSplash() { end(); }

    // Shows the window; false when it could not (the build runs all the same).
    bool begin(const std::string& title) {
        if (hwnd_) return true;
        title_ = cio::from_utf8(title);
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ContentSplash::proc), &self);
        instance_ = self;
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = &ContentSplash::proc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClass;
        registered_ = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (!registered_) return false;
        const int w = 620, h = 110;
        const int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"EMF", WS_POPUP | WS_BORDER,
                                x, y, w, h, nullptr, nullptr, instance_, this);
        if (!hwnd_) return false;
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd_);
        return true;
    }

    // The last log line to show; any thread.
    void line(const std::string& text) {
        AcquireSRWLockExclusive(&lock_);
        line_ = text;
        const HWND w = hwnd_;
        ReleaseSRWLockExclusive(&lock_);
        if (w) PostMessageW(w, kRepaint, 0, 0);
    }

    std::string last_line() const {
        AcquireSRWLockShared(&lock_);
        std::string s = line_;
        ReleaseSRWLockShared(&lock_);
        return s;
    }

    HWND window() const { return hwnd_; }

    // Closes the window (on the thread that made it).
    void end() {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            AcquireSRWLockExclusive(&lock_);
            hwnd_ = nullptr;
            ReleaseSRWLockExclusive(&lock_);
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        }
        if (registered_) {
            UnregisterClassW(kClass, instance_);
            registered_ = false;
        }
    }

private:
    static constexpr const wchar_t* kClass = L"EMF_ContentSplash";
    static constexpr UINT kRepaint = WM_APP + 1;

    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        } else if (msg == kRepaint) {
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        } else if (msg == WM_MOUSEACTIVATE) {
            return MA_NOACTIVATE;
        } else if (msg == WM_PAINT) {
            auto* self = reinterpret_cast<ContentSplash*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (self) {
                RECT r;
                GetClientRect(hwnd, &r);
                InflateRect(&r, -16, -14);
                SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
                SetBkMode(dc, TRANSPARENT);
                DrawTextW(dc, self->title_.c_str(), -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
                r.top += 34;
                const std::wstring last = cio::from_utf8(self->last_line());
                SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
                DrawTextW(dc, last.c_str(), -1, &r, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    bool registered_ = false;
    std::wstring title_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::string line_;
};

// Runs `work` on a worker thread and keeps `splash` (may be null) alive until it ends or `timeout_ms` passes; false
// then (the work goes on, owning what it uses). Without a thread it runs here.
inline bool run_with_splash(ContentSplash* splash, std::function<void()> work, DWORD timeout_ms) {
    (void)splash;  // its window is this thread's: the messages below keep it drawn
    auto owned = std::make_unique<std::function<void()>>(std::move(work));
    const HANDLE t = CreateThread(
        nullptr, 0,
        [](void* p) -> DWORD {
            const std::unique_ptr<std::function<void()>> w(static_cast<std::function<void()>*>(p));
            try {
                (*w)();
            } catch (...) {
            }
            return 0;
        },
        owned.get(), 0, nullptr);
    if (!t) {
        try {
            (*owned)();
        } catch (...) {
        }
        return true;
    }
    owned.release();  // the thread's now
    const ULONGLONG until = GetTickCount64() + timeout_ms;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        const DWORD left = now >= until ? 0 : static_cast<DWORD>(until - now);
        const DWORD w = MsgWaitForMultipleObjects(1, &t, FALSE, left, QS_ALLINPUT);
        if (w == WAIT_OBJECT_0) {
            CloseHandle(t);
            return true;
        }
        if (w == WAIT_OBJECT_0 + 1) {
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            continue;
        }
        CloseHandle(t);
        return false;  // the time is up (or the wait failed)
    }
}

}  // namespace emf::loader
