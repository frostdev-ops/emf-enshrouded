#include "extra_font.h"

#include "font_plan.h"

namespace emf::core {

namespace {

struct ScriptFonts {
    uint32_t language;
    const wchar_t* noto;           // the README's file (the static Regular from the Google Fonts download)
    const wchar_t* noto_variable;  // the variable font of the same download
    bool cjk;                      // NotoSansCJK-Regular.ttc covers it too
    const wchar_t* windows[3];     // Windows' own, best first
    const char* download;
};

// The game's LanguageIds (core/game_text.cpp): 11 ja_jp, 13 ko_kr, 17 th_th, 20 zh_cn, 21 zh_tw.
constexpr ScriptFonts kScripts[] = {
    {11, L"NotoSansJP-Regular.ttf", L"NotoSansJP-VariableFont_wght.ttf", true,
     {L"YuGothR.ttc", L"meiryo.ttc", L"msgothic.ttc"}, "https://fonts.google.com/noto/specimen/Noto+Sans+JP"},
    {13, L"NotoSansKR-Regular.ttf", L"NotoSansKR-VariableFont_wght.ttf", true, {L"malgun.ttf", nullptr, nullptr},
     "https://fonts.google.com/noto/specimen/Noto+Sans+KR"},
    {17, L"NotoSansThai-Regular.ttf", L"NotoSansThai-VariableFont_wdth,wght.ttf", false,
     {L"LeelawUI.ttf", L"leelawad.ttf", L"tahoma.ttf"}, "https://fonts.google.com/noto/specimen/Noto+Sans+Thai"},
    {20, L"NotoSansSC-Regular.ttf", L"NotoSansSC-VariableFont_wght.ttf", true, {L"msyh.ttc", L"simsun.ttc", nullptr},
     "https://fonts.google.com/noto/specimen/Noto+Sans+SC"},
    {21, L"NotoSansTC-Regular.ttf", L"NotoSansTC-VariableFont_wght.ttf", true, {L"msjh.ttc", L"mingliu.ttc", nullptr},
     "https://fonts.google.com/noto/specimen/Noto+Sans+TC"},
};

const ScriptFonts* script_of(uint32_t language) {
    for (const ScriptFonts& s : kScripts)
        if (s.language == language) return &s;
    return nullptr;
}

}  // namespace

std::vector<std::wstring> extra_font_files(uint32_t language, const std::wstring& emf_fonts,
                                           const std::wstring& windows_fonts) {
    std::vector<std::wstring> out;
    const ScriptFonts* s = script_of(language);
    if (!s) return out;
    out.push_back(emf_fonts + L"\\" + s->noto);
    out.push_back(emf_fonts + L"\\" + s->noto_variable);
    if (s->cjk) out.push_back(emf_fonts + L"\\NotoSansCJK-Regular.ttc");
    for (const wchar_t* w : s->windows)
        if (w) out.push_back(windows_fonts + L"\\" + w);
    return out;
}

std::wstring pick_extra_font(uint32_t language, const std::wstring& emf_fonts, const std::wstring& windows_fonts,
                             const std::function<bool(const std::wstring&)>& file_ok) {
    for (const std::wstring& f : extra_font_files(language, emf_fonts, windows_fonts))
        if (file_ok(f)) return f;
    return L"";
}

const char* extra_font_download(uint32_t language) {
    const ScriptFonts* s = script_of(language);
    return s ? s->download : "";
}

const wchar_t* extra_font_file_name(uint32_t language) {
    const ScriptFonts* s = script_of(language);
    return s ? s->noto : L"";
}

std::vector<ImWchar> glyph_pairs(const std::vector<uint32_t>& cps) {
    std::vector<ImWchar> out;
    for (size_t i = 0; i < cps.size(); ++i) {
        const uint32_t first = cps[i];
        if (first == 0 || first > 0xFFFF) continue;
        uint32_t last = first;
        while (i + 1 < cps.size() && cps[i + 1] == last + 1 && cps[i + 1] <= 0xFFFF) last = cps[++i];
        out.push_back(static_cast<ImWchar>(first));
        out.push_back(static_cast<ImWchar>(last));
    }
    out.push_back(0);
    return out;
}

std::vector<ImWchar> glyph_ranges(const std::vector<uint32_t>& extra) {
    std::vector<ImWchar> out;
    for (const ImWchar* r = kFontRanges; r[0] && r[1]; r += 2) {
        out.push_back(r[0]);
        out.push_back(r[1]);
    }
    const std::vector<ImWchar> pairs = glyph_pairs(extra);
    out.insert(out.end(), pairs.begin(), pairs.end());  // ends with the 0
    return out;
}

}  // namespace emf::core
