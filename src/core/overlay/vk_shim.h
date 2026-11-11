#pragma once
// EMF's in-process Vulkan hook, the overlay's path where the Vulkan loader never loads EMF's layer (Wine / Proton: Wine's
// vulkan-1.dll forwards everything to winevulkan and lists no layers), or with [overlay] path=hook. The loader's
// GetProcAddress hook (src/loader/overlay_path.h) asks emf_vk_shim_proc for every vk* entry point the game asks
// vulkan-1.dll for; EMF's vkGetInstanceProcAddr then wraps instance and device creation, vkGetDeviceProcAddr, the
// queues, the swapchain and present, and hands the overlay the real functions (private research
// linux_proton_overlay_2026-11-02, option a). Tables are keyed by handle value: under Wine every dispatchable handle
// starts with the same word, so the layer's dispatch-pointer key would map them all to one.
#include <windows.h>

#include "vk_overlay.h"

extern "C" __declspec(dllexport) FARPROC emf_vk_shim_proc(const char* name, FARPROC real);

namespace emf::core {

// What the shim tells the overlay (the real overlay functions; tests replace them).
struct ShimOverlay {
    bool (*attach)(const LayerDevice& d) = nullptr;
    void (*detach)(VkDevice device) = nullptr;
    void (*remember_queue)(VkQueue queue, uint32_t family) = nullptr;
    VkResult (*present)(VkQueue queue, const VkPresentInfoKHR* info) = nullptr;
    VkResult (*create_swapchain)(VkDevice device, const VkSwapchainCreateInfoKHR* ci, const VkAllocationCallbacks* alloc,
                                 VkSwapchainKHR* out) = nullptr;
    void (*destroy_swapchain)(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* alloc) = nullptr;
};
void vk_shim_set_overlay(const ShimOverlay& o);
// vulkan-1.dll's own function of a name, for a handle the shim never saw (tests replace it).
void vk_shim_set_export_resolver(PFN_vkVoidFunction (*resolve)(const char* name));

// EMF's wrapper for an entry point the game asked vulkan-1.dll for (vkGetInstanceProcAddr above all, which also
// remembers `real` as the real one), or `real` for anything EMF does not wrap.
PFN_vkVoidFunction vk_shim_proc(const char* name, PFN_vkVoidFunction real);
// Presents seen through the shim (the debug bridge and the log), and whether it handed out its gipa at all.
uint64_t vk_shim_presents();
bool vk_shim_active();
void vk_shim_reset_for_tests();

}  // namespace emf::core
