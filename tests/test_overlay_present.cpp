#include <doctest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "core/overlay/vk_overlay.h"

using namespace emf::core;

namespace {
struct PresentLog {
    int calls = 0;
    std::vector<VkSemaphore> waits;
} g_pl;
VkResult VKAPI_PTR fake_present(VkQueue, const VkPresentInfoKHR* info) {
    ++g_pl.calls;
    g_pl.waits.assign(info->pWaitSemaphores, info->pWaitSemaphores + info->waitSemaphoreCount);
    return VK_SUCCESS;
}
int g_renders = 0;
bool render_ok(VkQueue, const VkPresentInfoKHR*, VkSemaphore* signal, void*) {
    ++g_renders;
    *signal = reinterpret_cast<VkSemaphore>(uintptr_t(0x1234));
    return true;
}
bool render_faults(VkQueue, const VkPresentInfoKHR*, VkSemaphore*, void*) {
    ++g_renders;
    *static_cast<volatile int*>(nullptr) = 1;
    return true;
}
const VkSemaphore kGameSem = reinterpret_cast<VkSemaphore>(uintptr_t(0x77));
VkPresentInfoKHR game_info() {
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &kGameSem;
    return info;
}
}  // namespace

TEST_CASE("present_through: closed goes straight to the game's present; open renders and presents after our work") {
    std::atomic<bool> disabled{false};
    const VkPresentInfoKHR info = game_info();
    g_pl = {};
    g_renders = 0;
    CHECK(present_through(nullptr, &info, false, &disabled, render_ok, nullptr, fake_present) == VK_SUCCESS);
    CHECK(g_renders == 0);
    CHECK(g_pl.calls == 1);
    CHECK(g_pl.waits == std::vector<VkSemaphore>{kGameSem});
    CHECK(present_through(nullptr, &info, true, &disabled, render_ok, nullptr, fake_present) == VK_SUCCESS);
    CHECK(g_renders == 1);
    CHECK(g_pl.calls == 2);
    CHECK(g_pl.waits == std::vector<VkSemaphore>{reinterpret_cast<VkSemaphore>(uintptr_t(0x1234))});
}

TEST_CASE("present_through: a faulting draw still presents and disables the overlay") {
    std::atomic<bool> disabled{false};
    const VkPresentInfoKHR info = game_info();
    g_pl = {};
    g_renders = 0;
    CHECK(present_through(nullptr, &info, true, &disabled, render_faults, nullptr, fake_present) == VK_SUCCESS);
    CHECK(disabled);
    CHECK(g_pl.calls == 1);
    CHECK(g_pl.waits == std::vector<VkSemaphore>{kGameSem});
    CHECK(present_through(nullptr, &info, true, &disabled, render_faults, nullptr, fake_present) == VK_SUCCESS);
    CHECK(g_renders == 1);  // disabled: not tried again
    CHECK(g_pl.calls == 2);
}

namespace {
#pragma warning(push)
#pragma warning(disable : 4717)  // recursive on all paths: that is the point
int g_depth_sink = 0;
int too_deep(int n) {
    volatile char pad[1024];
    pad[0] = static_cast<char>(n);
    return too_deep(n + 1) + pad[0];
}
#pragma warning(pop)
bool render_overflows(VkQueue, const VkPresentInfoKHR*, VkSemaphore*, void*) {
    ++g_renders;
    g_depth_sink = too_deep(0);
    return true;
}
}  // namespace

TEST_CASE("present_through: a draw that overflows the stack is caught, and so is the next one (the guard page is back)") {
    // in a thread of its own: a second overflow without the guard page put back would end the process
    int presented = 0;
    std::thread t([&] {
        const VkPresentInfoKHR info = game_info();
        for (int i = 0; i < 2; ++i) {
            std::atomic<bool> disabled{false};
            presented += present_through(nullptr, &info, true, &disabled, render_overflows, nullptr, fake_present) ==
                         VK_SUCCESS;
        }
    });
    t.join();
    CHECK(presented == 2);
}
