#pragma once

struct lua_State;

namespace openwow::ui::game {

class WorldUiLifecycleCommandPort;

void BindWorldUiLifecycleCommands(
    lua_State* state, WorldUiLifecycleCommandPort* commands);
void RequestWorldUiReload(lua_State* state);

}
