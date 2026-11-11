#pragma once
// How EMF's overlay reaches the game's Vulkan (private research linux_proton_overlay_2026-11-02):
// - Layer: the Khronos loader loads emf_core.dll as an explicit layer (the variables the loader sets): Windows' way.
// - Hook: the exe's GetProcAddress hands EMF's own vkGetInstanceProcAddr to the game when it asks vulkan-1.dll; EMF
//   wraps instance and device creation, the swapchain and present itself. Wine / Proton need it: Wine's vulkan-1.dll
//   forwards everything to winevulkan and never loads layers.
// [overlay] path = auto (the default: the hook under Wine, the layer elsewhere), layer or hook; never both in one start.
#include <windows.h>

#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

namespace emf::loader {

enum class OverlayPath { Layer, Hook, Off };

inline bool same_word(std::wstring_view a, const wchar_t* b) {
    const size_t n = std::wcslen(b);
    return a.size() == n && _wcsnicmp(a.data(), b, n) == 0;
}

// The setting's word: GetPrivateProfileString keeps a comment after the value ("hook   ; how EMF draws"), so it is cut
// at ';' or '#' and the spaces and tabs around it go.
inline std::wstring_view setting_word(std::wstring_view v) {
    if (const size_t c = v.find_first_of(L";#"); c != std::wstring_view::npos) v = v.substr(0, c);
    while (!v.empty() && (v.front() == L' ' || v.front() == L'\t')) v.remove_prefix(1);
    while (!v.empty() && (v.back() == L' ' || v.back() == L'\t')) v.remove_suffix(1);
    return v;
}

// The path for this start: off when [overlay] enabled=0; "layer" / "hook" as asked; anything else (auto, empty, a typo)
// the hook under Wine and the layer elsewhere.
inline OverlayPath decide_overlay_path(std::wstring_view setting, bool overlay_enabled, bool wine) {
    if (!overlay_enabled) return OverlayPath::Off;
    setting = setting_word(setting);
    if (same_word(setting, L"layer")) return OverlayPath::Layer;
    if (same_word(setting, L"hook")) return OverlayPath::Hook;
    return wine ? OverlayPath::Hook : OverlayPath::Layer;
}

// Whether a module's file name (a full path or not) is vulkan-1.dll, the Vulkan loader the game loads by name.
inline bool is_vulkan_loader_module(std::wstring_view file_name) {
    const size_t slash = file_name.find_last_of(L"\\/");
    const std::wstring_view leaf = slash == std::wstring_view::npos ? file_name : file_name.substr(slash + 1);
    return same_word(leaf, L"vulkan-1.dll");
}

// Whether GetProcAddress was asked for a Vulkan entry point by name ("vk" and more; a value below 0x10000 is an ordinal).
inline bool is_vk_name(const char* name) {
    if (reinterpret_cast<uintptr_t>(name) < 0x10000) return false;
    return name[0] == 'v' && name[1] == 'k' && name[2] != '\0';
}

using GetProcFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using ModuleNameFn = std::wstring (*)(HMODULE);
// EMF's shim (emf_core.dll's emf_vk_shim_proc): its wrapper for `name`, given the real function, or the real one.
using ShimProcFn = FARPROC (*)(const char* name, FARPROC real);

// The exe's GetProcAddress in hook mode: the real answer, handed to EMF's shim when it is a Vulkan entry point asked of
// vulkan-1.dll (and a shim is there); everything else as it is.
inline FARPROC overlay_get_proc(HMODULE module, LPCSTR name, GetProcFn real, ModuleNameFn module_name, ShimProcFn shim) {
    const FARPROC answer = real(module, name);
    if (!shim || !answer || !is_vk_name(name) || !is_vulkan_loader_module(module_name(module))) return answer;
    return shim(name, answer);
}

// Wine (and so Proton) is there: its ntdll exports wine_get_version.
inline bool running_under_wine() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

}  // namespace emf::loader
