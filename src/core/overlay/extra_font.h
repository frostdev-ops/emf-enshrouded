#pragma once
// The extra font for item names in Japanese, Korean, Chinese and Thai, which EMF's own fonts lack: the player's Noto
// file in <game>\emf\fonts (README_EMF.md links the free download), else the font Windows itself has for that script.
// Neither the repository nor EMF's zips carry one.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

namespace emf::core {

// The files to try for `language` (the game's LanguageId), in order: the Noto files the README names in `emf_fonts`
// (the static Regular, the variable font of the same download, the CJK collection), then Windows' own in
// `windows_fonts`. Empty for a language EMF's bundled fonts cover.
std::vector<std::wstring> extra_font_files(uint32_t language, const std::wstring& emf_fonts,
                                           const std::wstring& windows_fonts);

// The first of extra_font_files that `file_ok` accepts; "" when none.
std::wstring pick_extra_font(uint32_t language, const std::wstring& emf_fonts, const std::wstring& windows_fonts,
                             const std::function<bool(const std::wstring&)>& file_ok);

// The Google Fonts page of the language's Noto family (where the README sends players), "" for other languages.
const char* extra_font_download(uint32_t language);

// The README's file for the language ("NotoSansJP-Regular.ttf"), "" for other languages.
const wchar_t* extra_font_file_name(uint32_t language);

// ImGui glyph ranges of `cps` alone (sorted; above U+FFFF left out), neighbours joined into pairs, 0-terminated.
std::vector<ImWchar> glyph_pairs(const std::vector<uint32_t>& cps);

// ImGui glyph ranges: kFontRanges' pairs, then `extra` (sorted; those above U+FFFF left out: ImWchar is 16-bit here)
// as pairs with neighbours joined, 0-terminated.
std::vector<ImWchar> glyph_ranges(const std::vector<uint32_t>& extra);

}  // namespace emf::core
