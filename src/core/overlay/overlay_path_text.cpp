#include "overlay_path_text.h"

#include <windows.h>

namespace emf::core {

namespace {
bool is_hook(std::string_view path) { return path.rfind("hook", 0) == 0; }
}  // namespace

std::string overlay_way(std::string_view path) { return is_hook(path) ? "EMF's Vulkan hook" : "EMF's layer"; }

std::string overlay_not_ready_text(std::string_view path, bool disabled, bool device_ready, bool frames_recent) {
    if (disabled) return "overlay: the console is off after a failure (see the lines above)";
    if (path == "off") return "overlay: the console is off ([overlay] enabled=0 in emf\\emf_core.ini)";
    if (!device_ready) {
        if (path == "hook-failed")
            return "overlay: the console cannot be shown: EMF's Vulkan hook could not be set (the game's GetProcAddress "
                   "import was not found; see emf\\emf_loader.log)";
        if (path == "hook-wine")
            return "overlay: the console cannot be shown: the game's Vulkan device did not come through EMF's Vulkan hook "
                   "(Wine / Proton: please report it with emf\\emf_core.log and emf\\emf_loader.log)";
        if (is_hook(path))
            return "overlay: the console cannot be shown: the game's Vulkan device did not come through EMF's Vulkan hook "
                   "([overlay] path=hook)";
        return "overlay: the console cannot be shown: the game's Vulkan device did not come through EMF's layer (is "
               "emf\\vk\\VkLayer_EMF_console.json there, and was the game started with EMF installed?)";
    }
    if (!frames_recent)
        return "overlay: the console cannot be shown: no game frames passed through " + overlay_way(path) +
               " in the last 2 s";
    return "overlay: the console is ready";
}

const std::string& current_overlay_path() {
    static const std::string path = [] {
        char buf[32] = {};
        const DWORD n = GetEnvironmentVariableA("EMF_OVERLAY_PATH", buf, sizeof buf);
        return n && n < sizeof buf ? std::string(buf, n) : std::string();
    }();
    return path;
}

}  // namespace emf::core
