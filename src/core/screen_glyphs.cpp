#include "core/screen_glyphs.h"

#include <algorithm>

namespace emf::core {

namespace {
constexpr uint32_t kReplacement = 0xFFFD;
}  // namespace

std::vector<uint32_t> utf8_code_points(std::string_view s) {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        size_t n = 0;
        uint32_t cp = 0, min = 0;
        if (b < 0x80) {
            out.push_back(b);
            ++i;
            continue;
        }
        if ((b & 0xE0) == 0xC0) n = 1, cp = b & 0x1F, min = 0x80;
        else if ((b & 0xF0) == 0xE0) n = 2, cp = b & 0x0F, min = 0x800;
        else if ((b & 0xF8) == 0xF0) n = 3, cp = b & 0x07, min = 0x10000;
        bool ok = n > 0 && i + n < s.size();  // the lead byte and its n continuation bytes are all there
        for (size_t k = 1; ok && k <= n; ++k) {
            const auto c = static_cast<unsigned char>(s[i + k]);
            ok = (c & 0xC0) == 0x80;
            cp = (cp << 6) | (c & 0x3F);
        }
        ok = ok && cp >= min && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
        if (!ok) {
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += n + 1;
    }
    return out;
}

bool in_base_ranges(uint32_t cp) {
    return (cp >= 0x20 && cp <= 0xFF) || (cp >= 0x100 && cp <= 0x17F) || (cp >= 0x400 && cp <= 0x52F);
}

std::vector<uint32_t> extra_glyphs(const std::vector<std::string>& texts) {
    std::vector<uint32_t> out;
    for (const std::string& t : texts)
        for (uint32_t cp : utf8_code_points(t))
            if (cp >= 0x20 && cp != kReplacement && !in_base_ranges(cp)) out.push_back(cp);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.size() > kMaxExtraGlyphs) out.resize(kMaxExtraGlyphs);
    return out;
}

void ScreenGlyphs::want(uint64_t key, uint32_t language, std::vector<uint32_t> glyphs) {
    std::lock_guard<std::mutex> lock(mu_);
    want_.key = key;
    want_.language = language;
    want_.glyphs = std::move(glyphs);
    want_.version += 1;
    version_.store(want_.version, std::memory_order_release);
}

GlyphWant ScreenGlyphs::wanted() const {
    std::lock_guard<std::mutex> lock(mu_);
    return want_;
}

uint64_t ScreenGlyphs::collected_key() const {
    std::lock_guard<std::mutex> lock(mu_);
    return want_.key;
}

void ScreenGlyphs::baked(uint64_t version, std::vector<uint32_t> drawable) {
    std::sort(drawable.begin(), drawable.end());
    std::lock_guard<std::mutex> lock(mu_);
    baked_any_ = true;
    baked_version_ = version;
    drawable_ = std::move(drawable);
}

std::optional<bool> ScreenGlyphs::can_draw(std::string_view utf8) const {
    const std::vector<uint32_t> cps = utf8_code_points(utf8);
    std::lock_guard<std::mutex> lock(mu_);
    if (!baked_any_) return std::nullopt;
    for (uint32_t cp : cps) {
        if (in_base_ranges(cp)) continue;
        if (cp > 0xFFFF || !std::binary_search(drawable_.begin(), drawable_.end(), cp)) return false;
    }
    return true;
}

ScreenGlyphs& screen_glyphs() {
    static ScreenGlyphs g;
    return g;
}

}  // namespace emf::core
