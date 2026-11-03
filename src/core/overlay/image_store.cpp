#include "image_store.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <system_error>

namespace emf::core {

namespace {

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool is_png(const std::filesystem::path& p) { return lower(p.extension().wstring()) == L".png"; }

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += (c < 128) ? static_cast<char>(c) : '?';
    return s;
}

}  // namespace

std::vector<std::wstring> frame_files(const std::wstring& path, std::string* why) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path);
    std::vector<std::wstring> out;
    if (fs::is_regular_file(p, ec)) {
        if (is_png(p)) out.push_back(p.wstring());
        else if (why) *why = "not a .png file";
        return out;
    }
    if (!fs::is_directory(p, ec)) {
        if (why) *why = "no such file or folder";
        return out;
    }
    for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && is_png(it->path())) out.push_back(it->path().wstring());
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
        return lower(fs::path(a).filename().wstring()) < lower(fs::path(b).filename().wstring());
    });
    if (out.empty() && why) *why = "the folder has no .png files";
    return out;
}

Grid pack(uint32_t frames, uint32_t w, uint32_t h) {
    Grid g;
    g.w = w;
    g.h = h;
    if (frames == 0) return g;
    g.cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(frames))));
    g.rows = (frames + g.cols - 1) / g.cols;
    return g;
}

UvRect frame_uv(const Grid& g, uint32_t frame) {
    const uint32_t cols = std::max<uint32_t>(1, g.cols), rows = std::max<uint32_t>(1, g.rows);
    const uint32_t c = frame % cols, r = (frame / cols) % rows;
    UvRect uv;
    uv.u0 = static_cast<float>(c) / static_cast<float>(cols);
    uv.u1 = static_cast<float>(c + 1) / static_cast<float>(cols);
    uv.v0 = static_cast<float>(r) / static_cast<float>(rows);
    uv.v1 = static_cast<float>(r + 1) / static_cast<float>(rows);
    return uv;
}

uint32_t frame_at(double seconds, uint32_t fps, uint32_t frames) {
    if (fps == 0 || frames <= 1 || !(seconds > 0)) return 0;
    const double n = std::floor(std::fmod(seconds * fps, static_cast<double>(frames)));
    const uint32_t i = static_cast<uint32_t>(n);
    return i < frames ? i : 0;
}

uint32_t ImageStore::add(const std::string& key, const std::vector<Rgba>& frames, std::string* why) {
    const auto refuse = [&](std::string s) -> uint32_t {
        if (why) *why = std::move(s);
        return 0;
    };
    {
        const std::lock_guard<std::mutex> g(m_);
        for (size_t i = 0; i < images_.size(); ++i)
            if (images_[i].key == key) return static_cast<uint32_t>(i + 1);
    }
    if (frames.empty()) return refuse("no frames");
    if (frames.size() > kMaxImageFrames)
        return refuse("more than " + std::to_string(kMaxImageFrames) + " frames");
    const uint32_t w = frames[0].w, h = frames[0].h;
    for (const Rgba& f : frames) {
        if (f.w != w || f.h != h) return refuse("its frames are not all the same size");
        if (f.px.size() != static_cast<size_t>(f.w) * f.h * 4) return refuse("a frame's pixels do not match its size");
    }
    if (w == 0 || h == 0 || w > kMaxImageSide || h > kMaxImageSide)
        return refuse("a frame must be 1 to " + std::to_string(kMaxImageSide) + " pixels wide and high");
    Image img;
    img.key = key;
    img.frames = static_cast<uint32_t>(frames.size());
    img.grid = pack(img.frames, w, h);
    img.tex_w = img.grid.cols * w;
    img.tex_h = img.grid.rows * h;
    const size_t bytes = static_cast<size_t>(img.tex_w) * img.tex_h * 4;
    img.px.assign(bytes, 0);
    for (uint32_t i = 0; i < img.frames; ++i) {
        const uint32_t x0 = (i % img.grid.cols) * w, y0 = (i / img.grid.cols) * h;
        for (uint32_t y = 0; y < h; ++y)
            std::memcpy(&img.px[(static_cast<size_t>(y0 + y) * img.tex_w + x0) * 4],
                        &frames[i].px[static_cast<size_t>(y) * w * 4], static_cast<size_t>(w) * 4);
    }
    const std::lock_guard<std::mutex> g(m_);
    for (size_t i = 0; i < images_.size(); ++i)  // another thread added it meanwhile
        if (images_[i].key == key) return static_cast<uint32_t>(i + 1);
    if (images_.size() >= kMaxImages) return refuse("too many pictures (" + std::to_string(kMaxImages) + " at most)");
    if (bytes_ + bytes > budget_) return refuse("over the pictures' memory budget");
    bytes_ += bytes;
    images_.push_back(std::move(img));
    return static_cast<uint32_t>(images_.size());
}

uint32_t ImageStore::find_key(const std::string& key) const {
    const std::lock_guard<std::mutex> g(m_);
    for (size_t i = 0; i < images_.size(); ++i)
        if (images_[i].key == key) return static_cast<uint32_t>(i + 1);
    return 0;
}

std::vector<uint32_t> ImageStore::pending() const {
    const std::lock_guard<std::mutex> g(m_);
    std::vector<uint32_t> out;
    for (size_t i = 0; i < images_.size(); ++i)
        if (!images_[i].texture) out.push_back(static_cast<uint32_t>(i + 1));
    return out;
}

bool ImageStore::pixels(uint32_t handle, uint32_t* w, uint32_t* h, std::vector<uint8_t>* px) const {
    const std::lock_guard<std::mutex> g(m_);
    if (handle == 0 || handle > images_.size()) return false;
    const Image& img = images_[handle - 1];
    *w = img.tex_w;
    *h = img.tex_h;
    *px = img.px;
    return true;
}

void ImageStore::set_texture(uint32_t handle, uint64_t texture) {
    const std::lock_guard<std::mutex> g(m_);
    if (handle != 0 && handle <= images_.size()) images_[handle - 1].texture = texture;
}

void ImageStore::forget_textures() {
    const std::lock_guard<std::mutex> g(m_);
    for (Image& img : images_) img.texture = 0;
}

std::optional<ImageFrame> ImageStore::frame(uint32_t handle, double seconds, uint32_t fps) const {
    const std::lock_guard<std::mutex> g(m_);
    if (handle == 0 || handle > images_.size()) return std::nullopt;
    const Image& img = images_[handle - 1];
    if (!img.texture) return std::nullopt;
    ImageFrame f;
    f.texture = img.texture;
    f.uv = frame_uv(img.grid, frame_at(seconds, fps, img.frames));
    f.aspect = static_cast<float>(img.grid.w) / static_cast<float>(img.grid.h);
    return f;
}

namespace {

double srgb_to_linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

// SMPTE ST 2084: the code value for `l` = luminance / 10000 nits.
double pq_encode(double l) {
    constexpr double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
    constexpr double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
    const double p = std::pow(std::clamp(l, 0.0, 1.0), m1);
    return std::pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}

uint8_t to_byte(double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); }

}  // namespace

void convert_for_swapchain(std::vector<uint8_t>& px, PictureSpace space) {
    if (space == PictureSpace::Srgb) return;
    double lin[256];
    for (int i = 0; i < 256; ++i) lin[i] = srgb_to_linear(i / 255.0);
    constexpr double kWhite = 203.0 / 10000.0;  // BT.2408 reference white, as a fraction of PQ's 10000 nits
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        const double r = lin[px[i]], g = lin[px[i + 1]], b = lin[px[i + 2]];
        if (space == PictureSpace::LinearScrgb) {
            px[i] = to_byte(r);
            px[i + 1] = to_byte(g);
            px[i + 2] = to_byte(b);
            continue;
        }
        // BT.709 primaries to BT.2020 (ITU-R BT.2087), then PQ
        const double r2 = 0.6274 * r + 0.3293 * g + 0.0433 * b;
        const double g2 = 0.0691 * r + 0.9195 * g + 0.0114 * b;
        const double b2 = 0.0164 * r + 0.0880 * g + 0.8956 * b;
        px[i] = to_byte(pq_encode(r2 * kWhite));
        px[i + 1] = to_byte(pq_encode(g2 * kWhite));
        px[i + 2] = to_byte(pq_encode(b2 * kWhite));
    }
}

uint32_t load_image(ImageStore& store, const std::wstring& path, const std::string& key, std::string* why) {
    if (const uint32_t known = store.find_key(key)) return known;
    const std::vector<std::wstring> files = frame_files(path, why);
    if (files.empty()) return 0;
    if (files.size() > kMaxImageFrames) {
        if (why) *why = "more than " + std::to_string(kMaxImageFrames) + " frames";
        return 0;
    }
    std::vector<Rgba> frames(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        std::string e;
        if (!decode_png(files[i], &frames[i], &e)) {
            if (why) *why = narrow(std::filesystem::path(files[i]).filename().wstring()) + ": " + e;
            return 0;
        }
    }
    return store.add(key, frames, why);
}

}  // namespace emf::core
