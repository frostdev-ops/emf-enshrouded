#pragma once
// Pictures in mod windows (API v26): the pictures mods loaded, decoded on the CPU and waiting for (or done with) their
// upload. A picture is one frame or the frames of a turning one, packed in a grid in one RGBA texture. The render
// thread uploads pending pictures and sets their texture ids; drawing asks for a picture's texture and the UVs of the
// frame for a time. Thread-safe; renderer-agnostic (the texture id is ImGui's, opaque here).
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "png_decode.h"

namespace emf::core {

constexpr uint32_t kMaxImageSide = 512;      // a frame's width and height, in pixels
constexpr uint32_t kMaxImageFrames = 64;     // so a picture's texture is at most 4096 x 4096
constexpr uint32_t kMaxImages = 128;         // pictures per game (the descriptor pool has room for them)
constexpr size_t kImageBudgetBytes = 64u << 20;  // pixels of every picture together

// The files of a picture: a .png file is itself; a folder gives its .png files sorted by name (ignoring case). Empty
// and `why` set when there are none, or the path is neither.
std::vector<std::wstring> frame_files(const std::wstring& path, std::string* why);

struct Grid {
    uint32_t cols = 1, rows = 1;
    uint32_t w = 0, h = 0;  // one frame
};
// `frames` frames of w x h in a near-square grid: cols = ceil(sqrt(frames)).
Grid pack(uint32_t frames, uint32_t w, uint32_t h);

struct UvRect {
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};
UvRect frame_uv(const Grid& g, uint32_t frame);

// The frame shown `seconds` into a turning picture of `frames` frames at `fps` (0: the first frame).
uint32_t frame_at(double seconds, uint32_t fps, uint32_t frames);

struct ImageFrame {
    uint64_t texture = 0;  // ImGui's texture id
    UvRect uv;
    float aspect = 1;      // one frame's width / height
};

class ImageStore {
public:
    explicit ImageStore(size_t budget_bytes = kImageBudgetBytes) : budget_(budget_bytes) {}

    // A new picture from decoded frames (packed into one texture), or the handle `key` already has. 0 and `why` when
    // refused: no frames, frames of different sizes, a frame larger than kMaxImageSide, more than kMaxImageFrames, more
    // than kMaxImages pictures, or over the budget.
    uint32_t add(const std::string& key, const std::vector<Rgba>& frames, std::string* why);
    uint32_t find_key(const std::string& key) const;

    // The render thread: pictures waiting for their upload (in handle order), a picture's packed pixels, its texture id
    // once uploaded, and forget_textures when the backend's textures are gone (every picture waits again).
    std::vector<uint32_t> pending() const;
    bool pixels(uint32_t handle, uint32_t* w, uint32_t* h, std::vector<uint8_t>* px) const;
    void set_texture(uint32_t handle, uint64_t texture);
    void forget_textures();

    // Drawing: the texture and the UVs of the frame at `seconds`; nothing for 0, an unknown handle, or a picture not
    // uploaded yet.
    std::optional<ImageFrame> frame(uint32_t handle, double seconds, uint32_t fps) const;

private:
    struct Image {
        std::string key;
        uint32_t frames = 0;
        Grid grid;
        uint32_t tex_w = 0, tex_h = 0;
        std::vector<uint8_t> px;
        uint64_t texture = 0;
    };
    mutable std::mutex m_;
    std::vector<Image> images_;  // handle = index + 1
    size_t budget_;
    size_t bytes_ = 0;
};

// frame_files + decode_png + add: the picture at `path` under `key` (the same key again: its handle, nothing decoded).
uint32_t load_image(ImageStore& store, const std::wstring& path, const std::string& key, std::string* why);

}  // namespace emf::core
