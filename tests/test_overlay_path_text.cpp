// The overlay's texts name the way the game's Vulkan came to EMF (the loader's EMF_OVERLAY_PATH): the layer on Windows,
// EMF's Vulkan hook under Wine / Proton or with [overlay] path=hook.
#include <doctest.h>

#include <string>

#include "core/overlay/overlay_path_text.h"

using namespace emf::core;

namespace {
bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
}  // namespace

TEST_CASE("overlay text: the way in by the loader's path") {
    CHECK(overlay_way("") == "EMF's layer");  // an older loader: the layer
    CHECK(overlay_way("layer") == "EMF's layer");
    CHECK(overlay_way("hook") == "EMF's Vulkan hook");
    CHECK(overlay_way("hook-wine") == "EMF's Vulkan hook");
    CHECK(overlay_way("hook-failed") == "EMF's Vulkan hook");
}

TEST_CASE("overlay text: why the console cannot be shown, per path") {
    CHECK(has(overlay_not_ready_text("layer", true, true, true), "off after a failure"));
    const std::string layer = overlay_not_ready_text("layer", false, false, false);
    CHECK(has(layer, "did not come through EMF's layer"));
    CHECK(has(layer, "VkLayer_EMF_console.json"));
    const std::string hook = overlay_not_ready_text("hook", false, false, false);
    CHECK(has(hook, "did not come through EMF's Vulkan hook"));
    CHECK_FALSE(has(hook, "VkLayer_EMF_console.json"));
    CHECK(has(overlay_not_ready_text("hook-wine", false, false, false), "Wine"));
    CHECK(has(overlay_not_ready_text("hook-failed", false, false, false), "GetProcAddress"));
    CHECK(has(overlay_not_ready_text("off", false, false, false), "enabled=0"));
    CHECK(has(overlay_not_ready_text("hook-wine", false, true, false), "no game frames passed through EMF's Vulkan hook"));
    CHECK(has(overlay_not_ready_text("", false, true, false), "no game frames passed through EMF's layer"));
    CHECK(overlay_not_ready_text("hook", false, true, true) == "overlay: the console is ready");
}
