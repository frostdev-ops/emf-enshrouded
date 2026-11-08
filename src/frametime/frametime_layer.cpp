// EMF's frame-time recorder: a small pass-through Vulkan layer, VK_LAYER_EMF_frametime (emf\emf_frametime.dll, manifest
// emf\vk\VkLayer_EMF_frametime.json). It notes the time of every present the game makes and writes
//   emf\logs\frame_times_<start>.csv  one line per frame: seconds since the first frame, the frame's time, the time the
//                                     present call itself took (all in ms but the first)
//   emf\logs\frame_times_<start>.log  a summary of every 10 s and of the whole run so far, and each frame over 100 ms
// Diagnostics only: EMF's loader enables it for the game client when emf\emf_core.ini has [debug] frame_times=1, in
// every way of starting (Play without any mods too), so starts can be compared. It changes nothing in the game.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "frametime/frame_stats.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vulkan.h"

namespace {

// ---- the recorder ----

struct Frame {
    double t_s;        // since the first present
    float frame_ms;    // since the present before
    float present_ms;  // inside the present call (the layers below and the driver)
};

SRWLOCK g_rec_lock = SRWLOCK_INIT;
std::vector<Frame> g_pending;  // filled by present calls, emptied by the writer once a second
LARGE_INTEGER g_freq{}, g_first{}, g_last{};
INIT_ONCE g_writer_once = INIT_ONCE_STATIC_INIT;

double ms_between(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
    return static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart);
}

std::wstring logs_dir() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    return dir.substr(0, dir.find_last_of(L"\\/")) + L"\\emf\\logs";
}

std::string clock_text() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02u:%02u:%02u.%03u", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

DWORD WINAPI writer(void*) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    const std::wstring dir = logs_dir();
    CreateDirectoryW(dir.c_str(), nullptr);
    FILE* csv = _wfsopen((dir + L"\\frame_times_" + stamp + L".csv").c_str(), L"wb", _SH_DENYWR);
    FILE* log = _wfsopen((dir + L"\\frame_times_" + stamp + L".log").c_str(), L"wb", _SH_DENYWR);
    if (!csv || !log) return 0;
    wchar_t mode[64] = L"";
    GetEnvironmentVariableW(L"EMF_FRAME_TIMES_START", mode, 64);
    std::fprintf(log, "%s EMF frame times; this start: %ls\r\n", clock_text().c_str(), mode[0] ? mode : L"(unknown)");
    std::fprintf(csv, "t_s,frame_ms,present_ms\r\n");
    std::vector<float> window, run;
    std::vector<Frame> got;
    for (int second = 1;; ++second) {
        Sleep(1000);
        AcquireSRWLockExclusive(&g_rec_lock);
        got.swap(g_pending);
        ReleaseSRWLockExclusive(&g_rec_lock);
        for (const Frame& f : got) {
            std::fprintf(csv, "%.4f,%.2f,%.2f\r\n", f.t_s, f.frame_ms, f.present_ms);
            window.push_back(f.frame_ms);
            run.push_back(f.frame_ms);
            if (f.frame_ms > 100.0f)
                std::fprintf(log, "%s hitch %.0f ms at t=%.1f s (present call %.1f ms)\r\n", clock_text().c_str(),
                             f.frame_ms, f.t_s, f.present_ms);
        }
        got.clear();
        std::fflush(csv);
        if (second % 10 == 0) {
            using emf::frametime::summarize;
            using emf::frametime::summary_line;
            std::fprintf(log, "%s last 10 s: %s | run: %s\r\n", clock_text().c_str(),
                         summary_line(summarize(window)).c_str(), summary_line(summarize(run)).c_str());
            window.clear();
        }
        std::fflush(log);
    }
}

BOOL CALLBACK start_writer(INIT_ONCE*, void*, void**) {
    QueryPerformanceFrequency(&g_freq);
    if (HANDLE h = CreateThread(nullptr, 0, writer, nullptr, 0, nullptr)) CloseHandle(h);
    return TRUE;
}

// ---- the layer (the same chaining rules as EMF's console layer, src/core/overlay/vk_layer.cpp) ----

void* key(const void* dispatchable) { return *static_cast<void* const*>(dispatchable); }

struct InstanceRec {
    VkInstance instance;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkDestroyInstance destroy;
};

struct DeviceRec {
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkDestroyDevice destroy;
    PFN_vkQueuePresentKHR present;
};

SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<void*, InstanceRec> g_instances;
std::unordered_map<void*, DeviceRec> g_devices;

template <typename Rec>
bool find(std::unordered_map<void*, Rec>& map, const void* handle, Rec* out) {
    if (!handle) return false;
    AcquireSRWLockShared(&g_lock);
    const auto it = map.find(key(handle));
    const bool found = it != map.end();
    if (found) *out = it->second;
    ReleaseSRWLockShared(&g_lock);
    return found;
}

template <typename Info, typename Create>
Info* find_link(const Create* ci, VkStructureType type) {
    for (auto* p = static_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext) {
        if (p->sType != type) continue;
        auto* info = reinterpret_cast<Info*>(const_cast<VkBaseInStructure*>(p));
        if (info->function == VK_LAYER_LINK_INFO) return info;
    }
    return nullptr;
}

VkResult VKAPI_CALL ft_CreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* alloc,
                                      VkInstance* out) {
    auto* link = find_link<VkLayerInstanceCreateInfo>(ci, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO);
    if (!link || !link->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    const PFN_vkGetInstanceProcAddr next_gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto next_create = reinterpret_cast<PFN_vkCreateInstance>(next_gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!next_create) return VK_ERROR_INITIALIZATION_FAILED;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    const VkResult r = next_create(ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    InstanceRec rec{*out, next_gipa, reinterpret_cast<PFN_vkDestroyInstance>(next_gipa(*out, "vkDestroyInstance"))};
    AcquireSRWLockExclusive(&g_lock);
    g_instances[key(*out)] = rec;
    ReleaseSRWLockExclusive(&g_lock);
    return VK_SUCCESS;
}

void VKAPI_CALL ft_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    InstanceRec rec{};
    if (!find(g_instances, instance, &rec)) return;
    AcquireSRWLockExclusive(&g_lock);
    g_instances.erase(key(instance));
    ReleaseSRWLockExclusive(&g_lock);
    if (rec.destroy) rec.destroy(instance, alloc);
}

VkResult VKAPI_CALL ft_CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                    const VkAllocationCallbacks* alloc, VkDevice* out) {
    auto* link = find_link<VkLayerDeviceCreateInfo>(ci, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO);
    if (!link || !link->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    const PFN_vkGetInstanceProcAddr next_gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const PFN_vkGetDeviceProcAddr next_gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    InstanceRec inst{};
    const bool known = find(g_instances, phys, &inst);  // a physical device carries its instance's key
    auto next_create =
        reinterpret_cast<PFN_vkCreateDevice>(next_gipa(known ? inst.instance : VK_NULL_HANDLE, "vkCreateDevice"));
    if (!next_create) return VK_ERROR_INITIALIZATION_FAILED;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    const VkResult r = next_create(phys, ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    DeviceRec rec{next_gdpa, reinterpret_cast<PFN_vkDestroyDevice>(next_gdpa(*out, "vkDestroyDevice")),
                  reinterpret_cast<PFN_vkQueuePresentKHR>(next_gdpa(*out, "vkQueuePresentKHR"))};
    AcquireSRWLockExclusive(&g_lock);
    g_devices[key(*out)] = rec;
    ReleaseSRWLockExclusive(&g_lock);
    return VK_SUCCESS;
}

void VKAPI_CALL ft_DestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
    DeviceRec rec{};
    if (!find(g_devices, device, &rec)) return;
    AcquireSRWLockExclusive(&g_lock);
    g_devices.erase(key(device));
    ReleaseSRWLockExclusive(&g_lock);
    if (rec.destroy) rec.destroy(device, alloc);
}

VkResult VKAPI_CALL ft_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    DeviceRec rec{};  // a queue carries its device's key
    if (!find(g_devices, queue, &rec) || !rec.present) return VK_ERROR_DEVICE_LOST;
    InitOnceExecuteOnce(&g_writer_once, start_writer, nullptr, nullptr);
    LARGE_INTEGER before, after;
    QueryPerformanceCounter(&before);
    const VkResult r = rec.present(queue, info);
    QueryPerformanceCounter(&after);
    AcquireSRWLockExclusive(&g_rec_lock);
    if (g_first.QuadPart == 0) {
        g_first = before;
    } else {
        g_pending.push_back({ms_between(g_first, before) / 1000.0, static_cast<float>(ms_between(g_last, before)),
                             static_cast<float>(ms_between(before, after))});
    }
    g_last = before;
    ReleaseSRWLockExclusive(&g_rec_lock);
    return r;
}

PFN_vkVoidFunction own_function(const char* name);

PFN_vkVoidFunction VKAPI_CALL ft_GetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) return nullptr;
    if (const auto own = own_function(name)) return own;
    InstanceRec rec{};
    if (!find(g_instances, instance, &rec)) return nullptr;
    return rec.gipa(instance, name);
}

PFN_vkVoidFunction VKAPI_CALL ft_GetDeviceProcAddr(VkDevice device, const char* name) {
    if (!name) return nullptr;
    DeviceRec rec{};
    if (!find(g_devices, device, &rec)) return nullptr;
    const PFN_vkVoidFunction next = rec.gdpa(device, name);
    if (const auto own = own_function(name)) {
        const bool always = !std::strcmp(name, "vkGetDeviceProcAddr") || !std::strcmp(name, "vkDestroyDevice");
        return always || next ? own : nullptr;  // no present wrapper on a device without the swapchain extension
    }
    return next;
}

PFN_vkVoidFunction own_function(const char* name) {
#define EMF_FT_OWN(fn) \
    if (!std::strcmp(name, "vk" #fn)) return reinterpret_cast<PFN_vkVoidFunction>(&ft_##fn)
    EMF_FT_OWN(GetInstanceProcAddr);
    EMF_FT_OWN(CreateInstance);
    EMF_FT_OWN(DestroyInstance);
    EMF_FT_OWN(CreateDevice);
    EMF_FT_OWN(GetDeviceProcAddr);
    EMF_FT_OWN(DestroyDevice);
    EMF_FT_OWN(QueuePresentKHR);
#undef EMF_FT_OWN
    return nullptr;
}

}  // namespace

extern "C" __declspec(dllexport) VkResult VKAPI_CALL emf_ft_negotiate(VkNegotiateLayerInterface* version) {
    if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || version->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    version->loaderLayerInterfaceVersion = 2;
    version->pfnGetInstanceProcAddr = ft_GetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = ft_GetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
