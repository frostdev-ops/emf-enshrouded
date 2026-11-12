#pragma once
// Markers step 3 (API v35): canvases, drawing panels mods own, drawn by EMF's overlay over the game's HUD. The store
// keeps each canvas (its owner's: another mod cannot change or remove it) and its last drawing list; the overlay reads an
// immutable view rebuilt on every change. Numbers are checked; a bad command is left out; lists past 2048 are cut.
// canvas_layout turns a canvas's corner, offset and size (1080-pixel units) into display pixels. Game-free.
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

constexpr size_t kCanvasMax = 32;
constexpr size_t kCanvasCmdsMax = 2048;

struct CanvasCmd {
    uint8_t kind = 0;
    uint32_t rgba = 0;
    float a[10] = {};
    std::string s;
};

struct Canvas {
    std::string owner, id;
    EmfCanvas spec{};
    std::vector<CanvasCmd> cmds;
};

class CanvasStore {
public:
    enum class SetResult { Ok, Replaced, Removed, BadId, BadNumbers, Full, Taken, NotFound };
    SetResult set(const std::string& owner, const char* id, const EmfCanvas* c);  // null c removes
    // False when the canvas is not the owner's (or there is none); *kept: the commands kept.
    bool draw(const std::string& owner, const char* id, const EmfCanvasCmd* cmds, uint32_t count, uint32_t* kept);
    size_t remove_owner(const std::string& owner);
    std::shared_ptr<const std::vector<Canvas>> view() const;  // any thread

private:
    void publish();  // m_ held
    mutable std::mutex m_;
    std::vector<Canvas> canvases_;
    std::shared_ptr<const std::vector<Canvas>> view_ = std::make_shared<const std::vector<Canvas>>();
};

bool canvas_cmd_ok(const EmfCanvasCmd& c);
// The canvas's rect in display pixels: x, y, w, h.
void canvas_layout(const EmfCanvas& c, float display_w, float display_h, float out[4]);

}  // namespace emf::core
