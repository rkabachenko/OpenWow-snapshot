#include "openwow/ui/surfaces/game/bindings/world_ui_lifecycle_lua_adapter.h"

#include "openwow/ui/surfaces/game/runtime/world_ui_lifecycle_command.h"

#include <lua.hpp>

namespace openwow::ui::game {
namespace {

constexpr char kLifecycleCommandsRegistryKey[] =
    "openwow.world_ui_lifecycle_commands";

}

void BindWorldUiLifecycleCommands(
    lua_State* state, WorldUiLifecycleCommandPort* commands) {
  if (state == nullptr) {
    return;
  }
  lua_pushlightuserdata(state, commands);
  lua_setfield(state, LUA_REGISTRYINDEX, kLifecycleCommandsRegistryKey);
}

void RequestWorldUiReload(lua_State* state) {
  if (state == nullptr) {
    return;
  }
  lua_getfield(state, LUA_REGISTRYINDEX, kLifecycleCommandsRegistryKey);
  auto* commands = static_cast<WorldUiLifecycleCommandPort*>(
      lua_touserdata(state, -1));
  lua_pop(state, 1);
  if (commands != nullptr) {
    commands->RequestWorldUiReload();
  }
}

}
