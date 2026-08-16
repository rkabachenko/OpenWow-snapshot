#pragma once

struct lua_State;

namespace openwow::ui::game::lua_adapter {

[[nodiscard]] bool IsFrameMutationBlocked(lua_State* lua, int frame_index);

}
