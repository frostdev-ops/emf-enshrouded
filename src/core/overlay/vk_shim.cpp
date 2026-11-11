#include "vk_shim.h"

#include <atomic>
#include <cstring>
#include <iterator>
#include <unordered_map>

namespace emf::core {
namespace {

struct InstanceRec {
    VkInstance instance = VK_NULL_HANDLE;
    uint32_t api = VK_API_VERSION_1_0;
    PFN_vkDestroyInstance destroy = nullptr;
    PFN_vkEnumeratePhysicalDevices enum_phys = nullptr;
    PFN_vkEnumeratePhysicalDeviceGroups enum_groups = nullptr;
    PFN_vkEnumeratePhysicalDeviceGroups enum_groups_khr = nullptr;
};

struct DeviceRec {
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    PFN_vkDestroyDevice destroy = nullptr;
    PFN_vkGetDeviceQueue get_queue = nullptr;
    PFN_vkGetDeviceQueue2 get_queue2 = nullptr;
    PFN_vkCreateSwapchainKHR create_swapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroy_swapchain = nullptr;
    PFN_vkQueuePresentKHR present = nullptr;
    bool overlay = false;  // the device the console draws on
};

ShimOverlay default_overlay() {
    ShimOverlay o;
    o.attach = overlay_attach_device;
    o.detach = overlay_detach_device;
    o.remember_queue = overlay_remember_queue;
    o.present = overlay_present;
    o.create_swapchain = overlay_create_swapchain;
    o.destroy_swapchain = overlay_destroy_swapchain;
    return o;
}

SRWLOCK g_lock = SRWLOCK_INIT;
std::atomic<PFN_vkGetInstanceProcAddr> g_real_gipa{nullptr};
std::atomic<PFN_vkGetDeviceProcAddr> g_real_gdpa{nullptr};  // the first device's: for a device the shim never saw
std::unordered_map<const void*, InstanceRec> g_instances;
std::unordered_map<const void*, VkInstance> g_phys;    // physical device -> its instance
std::unordered_map<const void*, DeviceRec> g_devices;
std::unordered_map<const void*, VkDevice> g_queues;    // queue -> its device
ShimOverlay g_overlay = default_overlay();
std::atomic<uint64_t> g_presents{0};
std::atomic<bool> g_active{false};

template <class F>
F as(PFN_vkVoidFunction f) {
    return reinterpret_cast<F>(f);
}

// The real vkGetInstanceProcAddr: the one the game was handed (vk_shim_proc), else vulkan-1.dll's own (the core's
// imports are not patched).
PFN_vkGetInstanceProcAddr real_gipa() {
    if (const auto g = g_real_gipa.load()) return g;
    const HMODULE vk = GetModuleHandleW(L"vulkan-1.dll");
    const auto g = vk ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk, "vkGetInstanceProcAddr")) : nullptr;
    if (g) g_real_gipa = g;
    return g;
}

bool find_instance(const void* handle, InstanceRec* out) {
    AcquireSRWLockShared(&g_lock);
    const auto it = g_instances.find(handle);
    const bool found = it != g_instances.end();
    if (found) *out = it->second;
    ReleaseSRWLockShared(&g_lock);
    return found;
}

bool find_device(const void* handle, DeviceRec* out) {
    AcquireSRWLockShared(&g_lock);
    const auto it = g_devices.find(handle);
    const bool found = it != g_devices.end();
    if (found) *out = it->second;
    ReleaseSRWLockShared(&g_lock);
    return found;
}

bool find_queue_device(VkQueue queue, DeviceRec* out) {
    AcquireSRWLockShared(&g_lock);
    bool found = false;
    if (const auto q = g_queues.find(queue); q != g_queues.end()) {
        const auto it = g_devices.find(q->second);
        found = it != g_devices.end();
        if (found) *out = it->second;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

// The instance a physical device came from; one the shim did not see enumerated: the only (else the first) instance.
InstanceRec instance_of(VkPhysicalDevice phys) {
    InstanceRec rec{};
    AcquireSRWLockShared(&g_lock);
    if (const auto p = g_phys.find(phys); p != g_phys.end()) {
        if (const auto it = g_instances.find(p->second); it != g_instances.end()) rec = it->second;
    } else if (!g_instances.empty()) {
        rec = g_instances.begin()->second;
    }
    ReleaseSRWLockShared(&g_lock);
    return rec;
}

bool has_extension(const VkDeviceCreateInfo* ci, const char* name) {
    for (uint32_t i = 0; i < ci->enabledExtensionCount; ++i)
        if (ci->ppEnabledExtensionNames[i] && !std::strcmp(ci->ppEnabledExtensionNames[i], name)) return true;
    return false;
}

PFN_vkVoidFunction VKAPI_CALL shim_GetInstanceProcAddr(VkInstance instance, const char* name);
PFN_vkVoidFunction VKAPI_CALL shim_GetDeviceProcAddr(VkDevice device, const char* name);

VkResult VKAPI_CALL shim_CreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* alloc,
                                        VkInstance* out) {
    const auto gipa = real_gipa();
    const auto create = gipa ? as<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance")) : nullptr;
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult r = create(ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    InstanceRec rec;
    rec.instance = *out;
    if (ci && ci->pApplicationInfo && ci->pApplicationInfo->apiVersion) rec.api = ci->pApplicationInfo->apiVersion;
    rec.destroy = as<PFN_vkDestroyInstance>(gipa(*out, "vkDestroyInstance"));
    rec.enum_phys = as<PFN_vkEnumeratePhysicalDevices>(gipa(*out, "vkEnumeratePhysicalDevices"));
    rec.enum_groups = as<PFN_vkEnumeratePhysicalDeviceGroups>(gipa(*out, "vkEnumeratePhysicalDeviceGroups"));
    rec.enum_groups_khr = as<PFN_vkEnumeratePhysicalDeviceGroups>(gipa(*out, "vkEnumeratePhysicalDeviceGroupsKHR"));
    AcquireSRWLockExclusive(&g_lock);
    g_instances[*out] = rec;
    ReleaseSRWLockExclusive(&g_lock);
    return VK_SUCCESS;
}

void VKAPI_CALL shim_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    InstanceRec rec{};
    const bool known = instance && find_instance(instance, &rec);
    AcquireSRWLockExclusive(&g_lock);
    g_instances.erase(instance);
    for (auto it = g_phys.begin(); it != g_phys.end();) it = it->second == instance ? g_phys.erase(it) : std::next(it);
    ReleaseSRWLockExclusive(&g_lock);
    if (known && rec.destroy) {
        rec.destroy(instance, alloc);
    } else if (const auto gipa = real_gipa()) {
        if (const auto d = as<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"))) d(instance, alloc);
    }
}

void note_phys(VkInstance instance, const VkPhysicalDevice* phys, uint32_t n) {
    AcquireSRWLockExclusive(&g_lock);
    for (uint32_t i = 0; i < n; ++i)
        if (phys[i]) g_phys[phys[i]] = instance;
    ReleaseSRWLockExclusive(&g_lock);
}

VkResult VKAPI_CALL shim_EnumeratePhysicalDevices(VkInstance instance, uint32_t* count, VkPhysicalDevice* out) {
    InstanceRec rec{};
    if (!find_instance(instance, &rec) || !rec.enum_phys) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult r = rec.enum_phys(instance, count, out);
    if (out && count && (r == VK_SUCCESS || r == VK_INCOMPLETE)) note_phys(instance, out, *count);
    return r;
}

VkResult groups(PFN_vkEnumeratePhysicalDeviceGroups real, VkInstance instance, uint32_t* count,
                VkPhysicalDeviceGroupProperties* out) {
    if (!real) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult r = real(instance, count, out);
    if (out && count && (r == VK_SUCCESS || r == VK_INCOMPLETE))
        for (uint32_t g = 0; g < *count; ++g) note_phys(instance, out[g].physicalDevices, out[g].physicalDeviceCount);
    return r;
}
VkResult VKAPI_CALL shim_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* count,
                                                       VkPhysicalDeviceGroupProperties* out) {
    InstanceRec rec{};
    find_instance(instance, &rec);
    return groups(rec.enum_groups, instance, count, out);
}
VkResult VKAPI_CALL shim_EnumeratePhysicalDeviceGroupsKHR(VkInstance instance, uint32_t* count,
                                                          VkPhysicalDeviceGroupProperties* out) {
    InstanceRec rec{};
    find_instance(instance, &rec);
    return groups(rec.enum_groups_khr, instance, count, out);
}

VkResult VKAPI_CALL shim_CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                      const VkAllocationCallbacks* alloc, VkDevice* out) {
    const auto gipa = real_gipa();
    const InstanceRec inst = instance_of(phys);
    const auto create = gipa ? as<PFN_vkCreateDevice>(gipa(inst.instance, "vkCreateDevice")) : nullptr;
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult r = create(phys, ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    const VkDevice dev = *out;
    DeviceRec rec;
    rec.device = dev;
    rec.gdpa = as<PFN_vkGetDeviceProcAddr>(gipa(inst.instance, "vkGetDeviceProcAddr"));
    if (rec.gdpa) {
        PFN_vkGetDeviceProcAddr expected = nullptr;
        g_real_gdpa.compare_exchange_strong(expected, rec.gdpa);
        rec.destroy = as<PFN_vkDestroyDevice>(rec.gdpa(dev, "vkDestroyDevice"));
        rec.get_queue = as<PFN_vkGetDeviceQueue>(rec.gdpa(dev, "vkGetDeviceQueue"));
        rec.get_queue2 = as<PFN_vkGetDeviceQueue2>(rec.gdpa(dev, "vkGetDeviceQueue2"));
        rec.create_swapchain = as<PFN_vkCreateSwapchainKHR>(rec.gdpa(dev, "vkCreateSwapchainKHR"));
        rec.destroy_swapchain = as<PFN_vkDestroySwapchainKHR>(rec.gdpa(dev, "vkDestroySwapchainKHR"));
        rec.present = as<PFN_vkQueuePresentKHR>(rec.gdpa(dev, "vkQueuePresentKHR"));
    }
    if (rec.gdpa && inst.instance && ci && has_extension(ci, VK_KHR_SWAPCHAIN_EXTENSION_NAME) && g_overlay.attach) {
        LayerDevice d;
        d.instance = inst.instance;
        d.api_version = inst.api;
        d.gipa = gipa;
        d.phys = phys;
        d.device = dev;
        d.gdpa = rec.gdpa;
        d.set_loader_data = nullptr;  // the real loader (or winevulkan) stamps the handles it makes
        rec.overlay = g_overlay.attach(d);
    }
    AcquireSRWLockExclusive(&g_lock);
    g_devices[dev] = rec;
    ReleaseSRWLockExclusive(&g_lock);
    return VK_SUCCESS;
}

void VKAPI_CALL shim_DestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
    DeviceRec rec{};
    if (!find_device(device, &rec)) {
        if (const auto gdpa = g_real_gdpa.load())
            if (const auto d = as<PFN_vkDestroyDevice>(gdpa(device, "vkDestroyDevice"))) d(device, alloc);
        return;
    }
    if (rec.overlay && g_overlay.detach) g_overlay.detach(device);
    AcquireSRWLockExclusive(&g_lock);
    g_devices.erase(device);
    for (auto it = g_queues.begin(); it != g_queues.end();) it = it->second == device ? g_queues.erase(it) : std::next(it);
    ReleaseSRWLockExclusive(&g_lock);
    if (rec.destroy) rec.destroy(device, alloc);
}

void note_queue(VkQueue queue, VkDevice device) {
    AcquireSRWLockExclusive(&g_lock);
    g_queues[queue] = device;
    ReleaseSRWLockExclusive(&g_lock);
}

void VKAPI_CALL shim_GetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* queue) {
    DeviceRec rec{};
    if (!find_device(device, &rec) || !rec.get_queue) return;
    rec.get_queue(device, family, index, queue);
    if (!queue || !*queue) return;
    note_queue(*queue, device);
    if (rec.overlay && g_overlay.remember_queue) g_overlay.remember_queue(*queue, family);
}

void VKAPI_CALL shim_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue) {
    DeviceRec rec{};
    if (!find_device(device, &rec) || !rec.get_queue2) return;
    rec.get_queue2(device, info, queue);
    if (!queue || !*queue) return;
    note_queue(*queue, device);
    if (rec.overlay && info && g_overlay.remember_queue) g_overlay.remember_queue(*queue, info->queueFamilyIndex);
}

VkResult VKAPI_CALL shim_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* ci,
                                            const VkAllocationCallbacks* alloc, VkSwapchainKHR* out) {
    DeviceRec rec{};
    if (!find_device(device, &rec) || !rec.create_swapchain) return VK_ERROR_INITIALIZATION_FAILED;
    if (rec.overlay && g_overlay.create_swapchain) return g_overlay.create_swapchain(device, ci, alloc, out);
    return rec.create_swapchain(device, ci, alloc, out);
}

void VKAPI_CALL shim_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* alloc) {
    DeviceRec rec{};
    if (!find_device(device, &rec) || !rec.destroy_swapchain) return;
    if (rec.overlay && g_overlay.destroy_swapchain)
        g_overlay.destroy_swapchain(device, swapchain, alloc);
    else
        rec.destroy_swapchain(device, swapchain, alloc);
}

VkResult VKAPI_CALL shim_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    DeviceRec rec{};
    if (!find_queue_device(queue, &rec) || !rec.present) return VK_ERROR_DEVICE_LOST;
    g_presents.fetch_add(1, std::memory_order_relaxed);
    if (rec.overlay && g_overlay.present) return g_overlay.present(queue, info);
    return rec.present(queue, info);
}

// The functions the shim answers itself; `optional` ones only where the real loader has them.
PFN_vkVoidFunction own_function(const char* name, bool* optional = nullptr) {
    if (optional) *optional = false;
#define EMF_OWN(fn) \
    if (!std::strcmp(name, "vk" #fn)) return reinterpret_cast<PFN_vkVoidFunction>(&shim_##fn)
    EMF_OWN(GetInstanceProcAddr);
    EMF_OWN(CreateInstance);
    EMF_OWN(DestroyInstance);
    EMF_OWN(EnumeratePhysicalDevices);
    EMF_OWN(CreateDevice);
    EMF_OWN(GetDeviceProcAddr);
    EMF_OWN(DestroyDevice);
    EMF_OWN(GetDeviceQueue);
    EMF_OWN(GetDeviceQueue2);
    EMF_OWN(CreateSwapchainKHR);
    EMF_OWN(DestroySwapchainKHR);
    EMF_OWN(QueuePresentKHR);
    if (optional) *optional = true;
    EMF_OWN(EnumeratePhysicalDeviceGroups);
    EMF_OWN(EnumeratePhysicalDeviceGroupsKHR);
#undef EMF_OWN
    if (optional) *optional = false;
    return nullptr;
}

PFN_vkVoidFunction VKAPI_CALL shim_GetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) return nullptr;
    const auto gipa = real_gipa();
    const PFN_vkVoidFunction real = gipa ? gipa(instance, name) : nullptr;
    bool optional = false;
    if (const auto own = own_function(name, &optional)) return optional && !real ? nullptr : own;
    return real;
}

PFN_vkVoidFunction VKAPI_CALL shim_GetDeviceProcAddr(VkDevice device, const char* name) {
    if (!name) return nullptr;
    DeviceRec rec{};
    const auto gdpa = find_device(device, &rec) ? rec.gdpa : g_real_gdpa.load();
    const PFN_vkVoidFunction next = gdpa ? gdpa(device, name) : nullptr;
    if (const auto own = own_function(name)) {
        // a wrapper only for what the device has (no swapchain functions on a device without the extension)
        const bool always = !std::strcmp(name, "vkGetDeviceProcAddr") || !std::strcmp(name, "vkDestroyDevice");
        return always || next ? own : nullptr;
    }
    return next;
}

}  // namespace

void vk_shim_set_overlay(const ShimOverlay& o) { g_overlay = o; }

PFN_vkVoidFunction vk_shim_proc(const char* name, PFN_vkVoidFunction real) {
    if (!name) return real;
    if (!std::strcmp(name, "vkGetInstanceProcAddr")) {
        if (real) g_real_gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(real);
        g_active = true;
        return reinterpret_cast<PFN_vkVoidFunction>(&shim_GetInstanceProcAddr);
    }
    if (const auto own = own_function(name)) {
        if (!real_gipa()) return real;  // nothing to call down to
        g_active = true;
        return own;
    }
    return real;
}

uint64_t vk_shim_presents() { return g_presents.load(std::memory_order_relaxed); }
bool vk_shim_active() { return g_active.load(std::memory_order_relaxed); }

void vk_shim_reset_for_tests() {
    AcquireSRWLockExclusive(&g_lock);
    g_instances.clear();
    g_phys.clear();
    g_devices.clear();
    g_queues.clear();
    ReleaseSRWLockExclusive(&g_lock);
    g_real_gipa = nullptr;
    g_real_gdpa = nullptr;
    g_overlay = default_overlay();
    g_presents = 0;
    g_active = false;
}

}  // namespace emf::core

extern "C" __declspec(dllexport) FARPROC emf_vk_shim_proc(const char* name, FARPROC real) {
    return reinterpret_cast<FARPROC>(emf::core::vk_shim_proc(name, reinterpret_cast<PFN_vkVoidFunction>(real)));
}
