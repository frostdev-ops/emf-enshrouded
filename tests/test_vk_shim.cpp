// EMF's in-process Vulkan hook (the overlay's path under Wine / Proton, private research linux_proton_overlay_2026-11-02
// option a): the game's vkGetInstanceProcAddr from vulkan-1.dll goes through EMF's, which wraps instance and device
// creation, the queues, the swapchain and present, keyed by handle value (under Wine every dispatchable handle starts
// with the same word), and hands the overlay the real functions.
#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "core/overlay/vk_shim.h"

using namespace emf::core;

namespace {

// Wine's dispatchable handles all start with the ICD magic (no Khronos loader to put a dispatch table there).
struct FakeHandle {
    uint64_t magic = 0x01CDC0DEull;
    int id = 0;
};
FakeHandle g_inst[2] = {{0x01CDC0DEull, 1}, {0x01CDC0DEull, 2}};
FakeHandle g_phys[2] = {{0x01CDC0DEull, 11}, {0x01CDC0DEull, 12}};
FakeHandle g_dev[3] = {{0x01CDC0DEull, 21}, {0x01CDC0DEull, 22}, {0x01CDC0DEull, 23}};
FakeHandle g_queue[3] = {{0x01CDC0DEull, 31}, {0x01CDC0DEull, 32}, {0x01CDC0DEull, 33}};
int g_next_inst = 0, g_next_dev = 0;
int g_real_presents = 0, g_foo_calls = 0, g_real_dev_destroys = 0;
bool g_dev_has_swapchain_fns = true;

template <class T>
T h(FakeHandle& f) {
    return reinterpret_cast<T>(&f);
}
int id_of(const void* p) { return static_cast<const FakeHandle*>(p)->id; }

void VKAPI_CALL real_foo() { ++g_foo_calls; }
VkResult VKAPI_CALL real_create_instance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* out) {
    *out = h<VkInstance>(g_inst[g_next_inst++ % 2]);
    return VK_SUCCESS;
}
void VKAPI_CALL real_destroy_instance(VkInstance, const VkAllocationCallbacks*) {}
VkResult VKAPI_CALL real_enum_phys(VkInstance inst, uint32_t* count, VkPhysicalDevice* out) {
    if (!out) {
        *count = 1;
        return VK_SUCCESS;
    }
    out[0] = h<VkPhysicalDevice>(g_phys[id_of(inst) == 1 ? 0 : 1]);
    *count = 1;
    return VK_SUCCESS;
}
VkResult VKAPI_CALL real_create_device(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*,
                                       VkDevice* out) {
    *out = h<VkDevice>(g_dev[g_next_dev++ % 3]);
    return VK_SUCCESS;
}
void VKAPI_CALL real_destroy_device(VkDevice, const VkAllocationCallbacks*) { ++g_real_dev_destroys; }
void VKAPI_CALL real_get_queue(VkDevice dev, uint32_t, uint32_t, VkQueue* q) {
    *q = h<VkQueue>(g_queue[id_of(dev) - 21]);
}
VkResult VKAPI_CALL real_present(VkQueue, const VkPresentInfoKHR*) {
    ++g_real_presents;
    return VK_SUCCESS;
}
VkResult VKAPI_CALL real_create_swapchain(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*,
                                          VkSwapchainKHR* out) {
    *out = reinterpret_cast<VkSwapchainKHR>(uintptr_t(0x5C));
    return VK_SUCCESS;
}
void VKAPI_CALL real_destroy_swapchain(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*) {}

PFN_vkVoidFunction VKAPI_CALL real_gdpa(VkDevice, const char* name);
PFN_vkVoidFunction VKAPI_CALL real_gipa(VkInstance, const char* name) {
#define FN(n, f) \
    if (!std::strcmp(name, n)) return reinterpret_cast<PFN_vkVoidFunction>(&f)
    FN("vkCreateInstance", real_create_instance);
    FN("vkDestroyInstance", real_destroy_instance);
    FN("vkEnumeratePhysicalDevices", real_enum_phys);
    FN("vkCreateDevice", real_create_device);
    FN("vkGetDeviceProcAddr", real_gdpa);
    FN("vkFoo", real_foo);
    return nullptr;
}
PFN_vkVoidFunction VKAPI_CALL real_gdpa(VkDevice, const char* name) {
    FN("vkDestroyDevice", real_destroy_device);
    FN("vkGetDeviceQueue", real_get_queue);
    FN("vkFoo", real_foo);
    if (!g_dev_has_swapchain_fns) return nullptr;
    FN("vkQueuePresentKHR", real_present);
    FN("vkCreateSwapchainKHR", real_create_swapchain);
    FN("vkDestroySwapchainKHR", real_destroy_swapchain);
#undef FN
    return nullptr;
}

// the overlay, faked: it takes the first device with a swapchain
std::vector<LayerDevice> g_attached;
int g_overlay_presents = 0, g_detaches = 0, g_remembered = 0, g_overlay_swapchains = 0;
bool g_overlay_taken = false;
bool f_attach(const LayerDevice& d) {
    if (g_overlay_taken) return false;
    g_attached.push_back(d);
    g_overlay_taken = true;
    return true;
}
void f_detach(VkDevice) {
    ++g_detaches;
    g_overlay_taken = false;
}
void f_remember(VkQueue, uint32_t) { ++g_remembered; }
VkResult f_present(VkQueue, const VkPresentInfoKHR*) {
    ++g_overlay_presents;
    return VK_SUCCESS;
}
VkResult f_create_swapchain(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR* out) {
    ++g_overlay_swapchains;
    *out = reinterpret_cast<VkSwapchainKHR>(uintptr_t(0x0E));
    return VK_SUCCESS;
}
void f_destroy_swapchain(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*) {}

struct Rig {
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    Rig() {
        vk_shim_reset_for_tests();
        ShimOverlay o;
        o.attach = f_attach;
        o.detach = f_detach;
        o.remember_queue = f_remember;
        o.present = f_present;
        o.create_swapchain = f_create_swapchain;
        o.destroy_swapchain = f_destroy_swapchain;
        vk_shim_set_overlay(o);
        g_attached.clear();
        g_overlay_presents = g_detaches = g_remembered = g_overlay_swapchains = 0;
        g_real_presents = g_foo_calls = g_real_dev_destroys = 0;
        g_next_inst = g_next_dev = 0;
        g_overlay_taken = false;
        g_dev_has_swapchain_fns = true;
        gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            vk_shim_proc("vkGetInstanceProcAddr", reinterpret_cast<PFN_vkVoidFunction>(&real_gipa)));
    }
    ~Rig() { vk_shim_reset_for_tests(); }
    template <class F>
    F fn(VkInstance i, const char* name) {
        return reinterpret_cast<F>(gipa(i, name));
    }
    VkInstance make_instance() {
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        VkInstance inst = VK_NULL_HANDLE;
        REQUIRE(fn<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance")(&ci, nullptr, &inst) == VK_SUCCESS);
        return inst;
    }
    VkPhysicalDevice first_phys(VkInstance inst) {
        uint32_t n = 1;
        VkPhysicalDevice p = VK_NULL_HANDLE;
        REQUIRE(fn<PFN_vkEnumeratePhysicalDevices>(inst, "vkEnumeratePhysicalDevices")(inst, &n, &p) == VK_SUCCESS);
        return p;
    }
    VkDevice make_device(VkInstance inst, VkPhysicalDevice p, bool swapchain = true) {
        const char* ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        ci.enabledExtensionCount = swapchain ? 1 : 0;
        ci.ppEnabledExtensionNames = ext;
        VkDevice d = VK_NULL_HANDLE;
        REQUIRE(fn<PFN_vkCreateDevice>(inst, "vkCreateDevice")(p, &ci, nullptr, &d) == VK_SUCCESS);
        return d;
    }
};

}  // namespace

TEST_CASE("vk shim: EMF's vkGetInstanceProcAddr answers its own functions and passes the rest through") {
    Rig r;
    REQUIRE(r.gipa != nullptr);
    CHECK(reinterpret_cast<void*>(r.gipa) != reinterpret_cast<void*>(&real_gipa));
    CHECK(r.gipa(VK_NULL_HANDLE, "vkCreateInstance") != reinterpret_cast<PFN_vkVoidFunction>(&real_create_instance));
    const VkInstance inst = r.make_instance();
    CHECK(r.gipa(inst, "vkGetInstanceProcAddr") == reinterpret_cast<PFN_vkVoidFunction>(r.gipa));
    CHECK(r.gipa(inst, "vkFoo") == reinterpret_cast<PFN_vkVoidFunction>(&real_foo));
    CHECK(r.gipa(inst, "vkNothing") == nullptr);
    // any other entry point the game asks vulkan-1.dll for directly: the real one
    CHECK(vk_shim_proc("vkFoo", reinterpret_cast<PFN_vkVoidFunction>(&real_foo)) ==
          reinterpret_cast<PFN_vkVoidFunction>(&real_foo));
    CHECK(vk_shim_proc("vkCreateInstance", reinterpret_cast<PFN_vkVoidFunction>(&real_create_instance)) ==
          r.gipa(VK_NULL_HANDLE, "vkCreateInstance"));
}

TEST_CASE("vk shim: a device built from vkGetDeviceProcAddr tables reaches the overlay with the real functions") {
    Rig r;
    const VkInstance inst = r.make_instance();
    const VkPhysicalDevice phys = r.first_phys(inst);
    const VkDevice dev = r.make_device(inst, phys);
    REQUIRE(g_attached.size() == 1);
    const LayerDevice& d = g_attached[0];
    CHECK(d.instance == inst);
    CHECK(d.phys == phys);
    CHECK(d.device == dev);
    CHECK(d.gipa == &real_gipa);
    CHECK(d.gdpa == &real_gdpa);
    CHECK(d.set_loader_data == nullptr);
    const auto gdpa = r.fn<PFN_vkGetDeviceProcAddr>(inst, "vkGetDeviceProcAddr");
    VkQueue q = VK_NULL_HANDLE;
    reinterpret_cast<PFN_vkGetDeviceQueue>(gdpa(dev, "vkGetDeviceQueue"))(dev, 0, 0, &q);
    CHECK(id_of(q) == 31);
    CHECK(g_remembered == 1);
    VkSwapchainKHR sc = VK_NULL_HANDLE;
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    REQUIRE(reinterpret_cast<PFN_vkCreateSwapchainKHR>(gdpa(dev, "vkCreateSwapchainKHR"))(dev, &sci, nullptr, &sc) ==
            VK_SUCCESS);
    CHECK(g_overlay_swapchains == 1);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    CHECK(reinterpret_cast<PFN_vkQueuePresentKHR>(gdpa(dev, "vkQueuePresentKHR"))(q, &pi) == VK_SUCCESS);
    CHECK(g_overlay_presents == 1);
    CHECK(g_real_presents == 0);
    CHECK(gdpa(dev, "vkFoo") == reinterpret_cast<PFN_vkVoidFunction>(&real_foo));
}

TEST_CASE("vk shim: handles that all start with the same word are told apart (Wine)") {
    Rig r;
    const VkInstance a = r.make_instance();
    const VkInstance b = r.make_instance();
    const VkDevice da = r.make_device(a, r.first_phys(a));  // takes the overlay
    const VkDevice db = r.make_device(b, r.first_phys(b));  // a second device: the overlay refuses it
    REQUIRE(g_attached.size() == 1);
    CHECK(g_attached[0].instance == a);
    const auto gdpa = r.fn<PFN_vkGetDeviceProcAddr>(b, "vkGetDeviceProcAddr");
    VkQueue qa = VK_NULL_HANDLE, qb = VK_NULL_HANDLE;
    auto get_queue = reinterpret_cast<PFN_vkGetDeviceQueue>(gdpa(db, "vkGetDeviceQueue"));
    get_queue(da, 0, 0, &qa);
    get_queue(db, 0, 0, &qb);
    auto present = reinterpret_cast<PFN_vkQueuePresentKHR>(gdpa(db, "vkQueuePresentKHR"));
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present(qb, &pi);  // b's queue: the real present
    CHECK(g_real_presents == 1);
    CHECK(g_overlay_presents == 0);
    present(qa, &pi);  // a's queue: the overlay
    CHECK(g_overlay_presents == 1);
}

TEST_CASE("vk shim: destroying the overlay's device detaches it; a later device takes over") {
    Rig r;
    const VkInstance inst = r.make_instance();
    const VkPhysicalDevice phys = r.first_phys(inst);
    const VkDevice d1 = r.make_device(inst, phys);
    const auto gdpa = r.fn<PFN_vkGetDeviceProcAddr>(inst, "vkGetDeviceProcAddr");
    reinterpret_cast<PFN_vkDestroyDevice>(gdpa(d1, "vkDestroyDevice"))(d1, nullptr);
    CHECK(g_detaches == 1);
    CHECK(g_real_dev_destroys == 1);
    r.make_device(inst, phys);
    CHECK(g_attached.size() == 2);
}

TEST_CASE("vk shim: no swapchain functions on a device without them; a device without the extension is not attached") {
    Rig r;
    const VkInstance inst = r.make_instance();
    g_dev_has_swapchain_fns = false;
    const VkDevice dev = r.make_device(inst, r.first_phys(inst), false);
    CHECK(g_attached.empty());
    const auto gdpa = r.fn<PFN_vkGetDeviceProcAddr>(inst, "vkGetDeviceProcAddr");
    CHECK(gdpa(dev, "vkCreateSwapchainKHR") == nullptr);
    CHECK(gdpa(dev, "vkQueuePresentKHR") == nullptr);
    CHECK(gdpa(dev, "vkDestroyDevice") != nullptr);
}

TEST_CASE("vk shim: a physical device the shim did not see falls back to the only instance") {
    Rig r;
    const VkInstance inst = r.make_instance();
    FakeHandle unseen{0x01CDC0DEull, 99};
    r.make_device(inst, reinterpret_cast<VkPhysicalDevice>(&unseen));
    REQUIRE(g_attached.size() == 1);
    CHECK(g_attached[0].instance == inst);
}
