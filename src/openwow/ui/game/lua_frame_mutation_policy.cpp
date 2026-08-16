#include "openwow/ui/game/lua_frame_mutation_policy.h"

#include "openwow/ui/game/api/game_lua_api_internal.h"

namespace openwow::ui::game::lua_adapter {

bool IsFrameMutationBlocked(lua_State* lua, const int frame_index) {
  return detail::LuaFrameMutationBlocked(lua, frame_index);
}

}
