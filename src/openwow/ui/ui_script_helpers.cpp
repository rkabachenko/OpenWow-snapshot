
#include "openwow/ui/ui_script_helpers.h"

#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/ui_enum_helpers.h"

extern "C" {
#include <lua.hpp>
}

static const char *lua_tostring_3(lua_State *L, int idx) {
  return lua_tolstring(L, idx, nullptr);
}

namespace openwow::ui {

static float LuaToFloat(lua_State *L, int idx) {
  return static_cast<float>(lua_tonumber(L, idx));
}

void ParseFramePointFromLua(uint32_t *pointOut, int argIndex, lua_State *L, float *offsetsOut) {
  const char *pointName = lua_tostring_3(L, argIndex);
  int nextArg = argIndex + 1;

  int parsed_point = 4;
  if (!StringToFramePoint(pointName, &parsed_point)) {
    *pointOut = 4;
  } else {
    *pointOut = static_cast<uint32_t>(parsed_point);
  }

  float rawX = LuaToFloat(L, nextArg);
  offsetsOut[0] = openwow::ui::anim::PixelAnimationOffsetToStored(rawX);

  float rawY = LuaToFloat(L, nextArg + 1);
  offsetsOut[1] = openwow::ui::anim::PixelAnimationOffsetToStored(rawY);
}

}
