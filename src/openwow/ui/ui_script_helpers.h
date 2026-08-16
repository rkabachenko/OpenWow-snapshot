
#pragma once

#include <cstdint>

struct lua_State;

namespace openwow::ui {

void ParseFramePointFromLua(uint32_t *pointOut, int argIndex, lua_State *L, float *offsetsOut);

}
