#pragma once

#include <string_view>

struct lua_State;

namespace openwow::ui::game::detail {

[[nodiscard]] bool PushNamedFrameLikeObject(
    lua_State* state, std::string_view frame_name);

}
