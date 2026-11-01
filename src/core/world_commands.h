#pragma once
// The world settings commands: world.get, world.set, world.reset, world.list, world.export (docs/modding/world_settings.md).
// Like the other built-in commands they are for admins: the console, emf\commands.txt and [chat] admins.
#include "commands.h"

namespace emf::core {
namespace ws {
class Service;
}

// `svc` must stay alive as long as the commands can run. False if one could not be added.
bool add_world_commands(CommandCenter& center, ws::Service& svc);

}  // namespace emf::core
