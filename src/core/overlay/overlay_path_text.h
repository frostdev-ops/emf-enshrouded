#pragma once
// The overlay's texts name how the game's Vulkan came to EMF: the loader sets EMF_OVERLAY_PATH for this start (layer,
// hook, hook-wine, hook-failed, off; empty from an older loader, which always used the layer).
#include <string>
#include <string_view>

namespace emf::core {

// "EMF's layer" or "EMF's Vulkan hook".
std::string overlay_way(std::string_view path);
// Why the console cannot be shown (or that it is ready), for the log and the console key.
std::string overlay_not_ready_text(std::string_view path, bool disabled, bool device_ready, bool frames_recent);
// The loader's EMF_OVERLAY_PATH, read once ("" without it).
const std::string& current_overlay_path();

}  // namespace emf::core
