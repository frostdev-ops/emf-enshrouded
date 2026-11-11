// The overlay's way into the game's Vulkan (private research linux_proton_overlay_2026-11-02, option a): the loader layer
// on Windows, an in-process hook under Wine / Proton (Wine's vulkan-1.dll never loads layers); [overlay] path picks.
#include <windows.h>

#include <doctest.h>

#include <string>

#include "loader/overlay_path.h"

using namespace emf::loader;

namespace {

int g_sentinel_real = 0, g_sentinel_shim = 0;
const char* g_shim_name = nullptr;
FARPROC g_shim_real = nullptr;
FARPROC WINAPI fake_real(HMODULE, LPCSTR) { return reinterpret_cast<FARPROC>(&g_sentinel_real); }
FARPROC fake_shim(const char* name, FARPROC real) {
    g_shim_name = name;
    g_shim_real = real;
    return reinterpret_cast<FARPROC>(&g_sentinel_shim);
}
HMODULE const kVulkan = reinterpret_cast<HMODULE>(0x1000);
HMODULE const kOther = reinterpret_cast<HMODULE>(0x2000);
std::wstring fake_name(HMODULE m) { return m == kVulkan ? L"C:\\Windows\\System32\\vulkan-1.dll" : L"C:\\x\\kernel32.dll"; }

}  // namespace

TEST_CASE("overlay path: auto is the layer on Windows and the hook under Wine") {
    CHECK(decide_overlay_path(L"", true, false) == OverlayPath::Layer);
    CHECK(decide_overlay_path(L"auto", true, true) == OverlayPath::Hook);
    CHECK(decide_overlay_path(L"AUTO", true, false) == OverlayPath::Layer);
    CHECK(decide_overlay_path(L"", true, true) == OverlayPath::Hook);
}

TEST_CASE("overlay path: layer and hook as asked; off when the overlay is off; an unknown word is auto") {
    CHECK(decide_overlay_path(L"hook", true, false) == OverlayPath::Hook);
    CHECK(decide_overlay_path(L"Layer", true, true) == OverlayPath::Layer);
    CHECK(decide_overlay_path(L"hook", false, true) == OverlayPath::Off);
    CHECK(decide_overlay_path(L"layer", false, false) == OverlayPath::Off);
    CHECK(decide_overlay_path(L"banana", true, true) == OverlayPath::Hook);
    CHECK(decide_overlay_path(L"banana", true, false) == OverlayPath::Layer);
}

TEST_CASE("overlay path: the Vulkan loader module by its file name, in any case or folder") {
    CHECK(is_vulkan_loader_module(L"vulkan-1.dll"));
    CHECK(is_vulkan_loader_module(L"C:\\Windows\\System32\\VULKAN-1.DLL"));
    CHECK(is_vulkan_loader_module(L"Z:\\x/vulkan-1.dll"));
    CHECK_FALSE(is_vulkan_loader_module(L"vulkan-1-999-0-0-0.dll"));
    CHECK_FALSE(is_vulkan_loader_module(L"winevulkan.dll"));
    CHECK_FALSE(is_vulkan_loader_module(L"myvulkan-1.dll"));
    CHECK_FALSE(is_vulkan_loader_module(L""));
}

TEST_CASE("overlay path: a Vulkan entry point's name, never an ordinal") {
    CHECK(is_vk_name("vkGetInstanceProcAddr"));
    CHECK(is_vk_name("vkCreateInstance"));
    CHECK_FALSE(is_vk_name("GetProcAddress"));
    CHECK_FALSE(is_vk_name("vk"));
    CHECK_FALSE(is_vk_name(nullptr));
    CHECK_FALSE(is_vk_name(reinterpret_cast<const char*>(7)));  // GetProcAddress by ordinal
}

TEST_CASE("overlay path: GetProcAddress hands a vulkan-1 entry point to EMF's shim, everything else as it is") {
    g_shim_name = nullptr;
    CHECK(overlay_get_proc(kVulkan, "vkGetInstanceProcAddr", &fake_real, &fake_name, &fake_shim) ==
          reinterpret_cast<FARPROC>(&g_sentinel_shim));
    CHECK(std::string(g_shim_name) == "vkGetInstanceProcAddr");
    CHECK(g_shim_real == reinterpret_cast<FARPROC>(&g_sentinel_real));  // the shim wraps the real one
    g_shim_name = nullptr;
    CHECK(overlay_get_proc(kOther, "vkGetInstanceProcAddr", &fake_real, &fake_name, &fake_shim) ==
          reinterpret_cast<FARPROC>(&g_sentinel_real));
    CHECK(overlay_get_proc(kVulkan, "GetSomething", &fake_real, &fake_name, &fake_shim) ==
          reinterpret_cast<FARPROC>(&g_sentinel_real));
    CHECK(overlay_get_proc(kVulkan, reinterpret_cast<LPCSTR>(12), &fake_real, &fake_name, &fake_shim) ==
          reinterpret_cast<FARPROC>(&g_sentinel_real));
    CHECK(g_shim_name == nullptr);
    CHECK(overlay_get_proc(kVulkan, "vkGetInstanceProcAddr", &fake_real, &fake_name, nullptr) ==
          reinterpret_cast<FARPROC>(&g_sentinel_real));  // no core (its export missing): the game's own
}

TEST_CASE("overlay path: a comment after the value and spaces around it are left out (review I1)") {
    CHECK(decide_overlay_path(L"hook      ; how EMF draws", true, false) == OverlayPath::Hook);
    CHECK(decide_overlay_path(L"  layer\t", true, true) == OverlayPath::Layer);
    CHECK(decide_overlay_path(L"hook # forced", true, false) == OverlayPath::Hook);
    CHECK(decide_overlay_path(L"; only a comment", true, false) == OverlayPath::Layer);
}
