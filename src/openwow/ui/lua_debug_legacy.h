#pragma once

extern "C" {
#include <lua.hpp>
}

#include <string>

namespace openwow::ui {

int PushLegacyDebugStack(lua_State* state);

std::string BuildLegacyDebugLocalsString(lua_State* state, int level);

}
