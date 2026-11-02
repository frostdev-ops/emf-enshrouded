#include "font_plan.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

// EMF's own copy of ImGui's stb_truetype (static: invisible outside this file), to check a font file before ImGui
// gets it: ImGui stops at an IM_ASSERT on a font it cannot parse, which turns the whole overlay off.
#pragma warning(push, 0)
#pragma warning(disable : 4505)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert(x) ((x) ? (void)0 : RaiseException(0xE0EF0F07u, EXCEPTION_NONCONTINUABLE, 0, nullptr))
#include "imstb_truetype.h"
#pragma warning(pop)

namespace emf::core {

const ImWchar kFontRanges[] = {
    0x0020, 0x00FF,  // Basic Latin, Latin-1 Supplement
    0x0100, 0x017F,  // Latin Extended-A
    0x0400, 0x052F,  // Cyrillic, Cyrillic Supplement
    0,
};

float base_pixel_size(int swapchain_height) {
    return std::clamp(static_cast<float>(swapchain_height) / 54.0f, 13.0f, 40.0f);
}

float scaled_pixel_size(float base, int percent) {
    return (std::max)(8.0f, std::round(base * static_cast<float>(percent) / 100.0f));
}

namespace {

std::wstring wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// The files to try for `choice`, in order. `bold`: the serif's bold first. A player's file falls back to `bundled`
// (the key's own default: the serif for the HUD and the tabs, Cascadia Mono for the console), then Consolas.
std::vector<std::wstring> chain(const FontChoice& choice, bool bold, const std::wstring& bundled, const FontPaths& p) {
    const std::wstring dir = p.fonts_dir + L"\\";
    const std::wstring serif = dir + kSerifFile, cascadia = dir + kCascadiaFile;
    switch (choice.kind) {
        case FontChoice::Kind::Serif:
            if (bold) return {dir + kSerifBoldFile, serif, p.consolas};
            return {serif, p.consolas};
        case FontChoice::Kind::Consolas: return {p.consolas, cascadia};
        case FontChoice::Kind::Cascadia: return {cascadia, p.consolas};
        case FontChoice::Kind::File: return {dir + wide(choice.file), dir + bundled, p.consolas};
        default: return {};
    }
}

}  // namespace

FontPlan plan_fonts(const LookSettings& look, int swapchain_height, const FontPaths& paths,
                    const std::function<bool(const std::wstring&)>& file_ok) {
    FontPlan plan;
    plan.pixel_size = base_pixel_size(swapchain_height);
    const float text = scaled_pixel_size(plan.pixel_size, look.text_size);
    const float hud = scaled_pixel_size(plan.pixel_size, look.hud_size);
    std::map<std::wstring, bool> checked;  // each file is checked once per plan, and said once
    const auto pick = [&](const std::vector<std::wstring>& files) -> std::wstring {
        for (const auto& f : files) {
            auto it = checked.find(f);
            if (it == checked.end()) {
                it = checked.emplace(f, file_ok && file_ok(f)).first;
                if (!it->second)
                    plan.problems.push_back("font " + narrow(f) + ": missing or not a font file, the next font in line "
                                            "is used (the bundled one, Consolas, then ImGui's own)");
            }
            if (it->second) return f;
        }
        return {};  // ImGui's built-in font
    };
    const std::vector<std::wstring> console = chain(look.console_font, false, kCascadiaFile, paths);
    const std::wstring console_file = pick(console);
    plan.faces.push_back({FontRole::Console, console_file, text});
    plan.faces.push_back({FontRole::Tabs, pick(chain(look.tabs_font, false, kSerifFile, paths)), text});
    if (look.hud_font.kind == FontChoice::Kind::Console) {  // the console's font, with no bold of its own
        plan.faces.push_back({FontRole::Hud, console_file, hud});
        plan.faces.push_back({FontRole::HudBold, console_file, hud});
    } else {
        plan.faces.push_back({FontRole::Hud, pick(chain(look.hud_font, false, kSerifFile, paths)), hud});
        plan.faces.push_back({FontRole::HudBold, pick(chain(look.hud_font, true, kSerifFile, paths)), hud});
    }
    return plan;
}

bool FontRebuild::due(float pixel_size) {
    if (pixel_size == built_) return false;
    built_ = pixel_size;
    return true;
}

bool read_font_file(const std::wstring& path, size_t max_bytes, std::vector<unsigned char>& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 && static_cast<uint64_t>(size.QuadPart) <= max_bytes;
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        size_t done = 0;
        while (ok && done < out.size()) {
            DWORD got = 0;
            const DWORD want = static_cast<DWORD>((std::min<size_t>)(out.size() - done, 1u << 20));
            ok = ReadFile(h, out.data() + done, want, &got, nullptr) && got > 0;
            done += got;
        }
    }
    CloseHandle(h);
    if (!ok) out.clear();
    return ok;
}

namespace {

uint32_t be16(const unsigned char* p) { return (uint32_t(p[0]) << 8) | p[1]; }
uint32_t be32(const unsigned char* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

// The table directory at `start` and every table it names lie inside the file.
bool tables_inside(const unsigned char* d, size_t size, size_t start) {
    if (start > size || size - start < 12) return false;
    const uint32_t n = be16(d + start + 4);
    if (n == 0 || (size - start - 12) / 16 < n) return false;
    for (uint32_t i = 0; i < n; ++i) {
        const unsigned char* rec = d + start + 12 + 16 * i;
        const uint32_t off = be32(rec + 8), len = be32(rec + 12);
        if (off > size || len > size - off) return false;
    }
    return true;
}

// stb_truetype reads the font and draws two glyphs; a fault or a failed stb assert means it is no font to trust.
// (Plain data only here: __try allows no objects that need unwinding.)
bool stb_reads(const unsigned char* data, int offset) {
    __try {
        stbtt_fontinfo info;
        if (!stbtt_InitFont(&info, data, offset)) return false;
        const float scale = stbtt_ScaleForPixelHeight(&info, 20.0f);
        for (int c : {int('A'), 0x416}) {
            const int glyph = stbtt_FindGlyphIndex(&info, c);
            if (!glyph) continue;
            int w = 0, h = 0, x = 0, y = 0;
            unsigned char* bmp = stbtt_GetGlyphBitmap(&info, scale, scale, glyph, &w, &h, &x, &y);
            if (bmp) stbtt_FreeBitmap(bmp, nullptr);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool font_bytes_ok(const unsigned char* data, size_t size) {
    if (!data || size < 1024 || size > kMaxFontBytes) return false;
    size_t start = 0;
    if (std::memcmp(data, "ttcf", 4) == 0) {  // a collection: ImGui takes its first font
        if (be32(data + 8) == 0) return false;
        start = be32(data + 12);
    } else if (!stbtt__isfont(const_cast<unsigned char*>(data))) {
        return false;
    }
    if (!tables_inside(data, size, start)) return false;
    return stb_reads(data, static_cast<int>(start));
}

bool font_file_ok(const std::wstring& path, size_t max_bytes) {
    std::vector<unsigned char> bytes;
    return read_font_file(path, max_bytes, bytes) && font_bytes_ok(bytes.data(), bytes.size());
}

OverlayFonts build_font_atlas(ImFontAtlas& atlas, const FontPlan& plan, std::vector<std::string>* problems) {
    atlas.Clear();
    struct Made {
        std::wstring file;
        float size;
        ImFont* font;
    };
    std::vector<Made> made;
    ImFont* roles[4] = {};
    for (size_t i = 0; i < plan.faces.size() && i < 4; ++i) {
        const FontFace& f = plan.faces[i];
        const auto same = std::find_if(made.begin(), made.end(),
                                       [&](const Made& m) { return m.file == f.file && m.size == f.pixel_size; });
        if (same != made.end()) {
            roles[i] = same->font;
            continue;
        }
        ImFont* font = nullptr;
        if (!f.file.empty()) {
            std::vector<unsigned char> bytes;
            if (read_font_file(f.file, kMaxFontBytes, bytes) && font_bytes_ok(bytes.data(), bytes.size())) {
                void* data = IM_ALLOC(bytes.size());  // the atlas owns it and frees it
                std::memcpy(data, bytes.data(), bytes.size());
                ImFontConfig cfg;
                cfg.FontDataOwnedByAtlas = true;
                font = atlas.AddFontFromMemoryTTF(data, static_cast<int>(bytes.size()), f.pixel_size, &cfg, kFontRanges);
            } else if (problems) {
                problems->push_back("font " + narrow(f.file) + ": could not be loaded, ImGui's own font is used");
            }
        }
        if (!font) {
            ImFontConfig cfg;
            cfg.SizePixels = f.pixel_size;
            font = atlas.AddFontDefault(&cfg);
        }
        made.push_back({f.file, f.pixel_size, font});
        roles[i] = font;
    }
    for (auto& r : roles)
        if (!r) r = roles[0];  // a plan with fewer faces: the console's font
    if (atlas.Fonts.Size == 0 || !atlas.Build()) {
        if (atlas.Fonts.Size && problems) problems->push_back("the font atlas could not be built, ImGui's own font is used");
        atlas.Clear();
        ImFont* d = atlas.AddFontDefault();
        atlas.Build();
        for (auto& r : roles) r = d;
    }
    return OverlayFonts{roles[0], roles[1], roles[2], roles[3]};
}

}  // namespace emf::core
