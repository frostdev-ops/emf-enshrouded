#pragma once
// The mouse pointer of the open console. In play the game hides the Windows pointer and clips it to its window, so the
// console draws its own arrow moved by the raw mouse counts. In the game's menus (the inventory, the map...) the game
// shows the real Windows pointer: the console then follows that one and draws no arrow of its own, so there is one
// pointer (live, 2026-11-02). Pure logic (unit-tested).
#include <optional>

#include "imgui.h"

namespace emf::core {

class CursorModel {
public:
    // The display size; the first call puts the cursor in the middle, later calls keep it inside.
    void set_display(ImVec2 display);
    // Raw movement in mouse counts, times `speed`.
    void move(float dx, float dy, float speed);
    // Puts the arrow at `p` (kept inside the display): where the Windows pointer was, when the game hides it again.
    void place(ImVec2 p);
    ImVec2 pos() const { return pos_; }

private:
    void clamp();
    ImVec2 display_{0, 0};
    ImVec2 pos_{0, 0};
    bool placed_ = false;
};

enum class PointerSource { Own, Windows };

// Which pointer the console follows this frame: the Windows pointer while the game window is in front and the game
// shows it (its menus), or while the mouse reports absolute positions (remote desktop, virtual machines: raw counts are
// not there); else EMF's own arrow.
PointerSource pointer_source(bool windows_pointer_showing, bool game_in_front, bool absolute_mouse);

// A point in the game window's client area (pixels) at the display's (the swapchain's) scale, kept inside the display;
// nothing while the window has no client area (minimised).
std::optional<ImVec2> client_to_display(int x, int y, int client_w, int client_h, ImVec2 display);

}  // namespace emf::core
