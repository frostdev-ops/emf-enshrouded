#pragma once
// The one window mods may have (API v24): opened from any thread, drawn on the render thread, handed back exactly once
// (close, finish, turned_off, close_mod), with the input capture switched on and off under the same lock so it never
// outlives the window. A window_open with the open window's id replaces it; any other id is refused while one is open.
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "core/window_request.h"

namespace emf::core {

class WindowGate {
public:
    enum class Refusal { None, NotReady, NoWindow, WheelBusy, OtherWindow };
    // capture(true, close_vk) when a window is accepted (again on a replacement), capture(false, 0) when handed back.
    explicit WindowGate(std::function<void(bool, uint16_t)> capture) : capture_(std::move(capture)) {}

    // Any thread. None = accepted; `replaced` gets the window it replaced (same id), to answer CLOSED (BY_MOD).
    Refusal open(WindowRequest r, const std::function<bool()>& overlay_ready, bool window_hooked, bool wheel_busy,
                 std::optional<WindowRequest>* replaced);
    // Any thread: new contents for the open window with contents.id (its cb, user and mod stay). False when none is.
    bool update(const WindowRequest& contents);
    // Any thread: closes the window `id`; `closed` gets it. False when it is not the open one.
    bool close(const std::string& id, std::optional<WindowRequest>* closed);
    // Render thread: the player closed it (Esc, the close key, the X).
    std::optional<WindowRequest> finish();
    // The overlay is turning off, or the world ended: the open window, handed back (nothing if none).
    std::optional<WindowRequest> turned_off();
    // A mod was turned off or its script reloaded: its window, handed back (nothing if the open one is another's).
    std::optional<WindowRequest> close_mod(const std::string& mod);
    bool busy() const;
    // Bumped by every open, replacement and update: the render thread copies current() when it changed.
    uint64_t version() const;
    std::optional<WindowRequest> current() const;

private:
    std::optional<WindowRequest> close_locked();
    std::function<void(bool, uint16_t)> capture_;
    mutable std::mutex m_;
    std::optional<WindowRequest> req_;
    uint64_t version_ = 0;
};

}  // namespace emf::core
