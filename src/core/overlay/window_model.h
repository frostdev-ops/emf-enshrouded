#pragma once
// Mod windows (API v24): checking a mod's EmfWindowDef and copying it into a WindowRequest. Pure (unit-tested).
#include <string>
#include <vector>

#include "core/window_request.h"

namespace emf::core {

// True and `out` filled when `def` is acceptable; else false and `why` names what is wrong. `for_update`: a
// window_update (cb and user are not needed and not copied). out->mod is left as it was.
bool copy_window_def(const EmfWindowDef* def, bool for_update, WindowRequest* out, std::string* why);

// The window as text lines (the ui.windows command): its id, title and widget count, then each widget.
std::vector<std::string> describe_window(const WindowRequest& w);

}  // namespace emf::core
