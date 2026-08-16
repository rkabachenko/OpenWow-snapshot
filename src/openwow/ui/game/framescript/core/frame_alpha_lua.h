#pragma once

#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/lua_c_api_convenience.h"

#include <lua.hpp>

#include <cstdint>

namespace openwow::ui::game {

inline std::uint8_t ReadLuaFrameAlphaByteOrDefault(lua_State *L, int frame_index,
                                                   std::uint8_t fallback = 0xFF) {
  frame_index = lua_absindex(L, frame_index);
  double alpha = NormalizeFrameAlphaByte(fallback);
  lua_getfield(L, frame_index, "__ow_alpha");
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return QuantizeFrameAlphaByteTruncated(alpha);
}

inline std::uint8_t ComputeLuaFrameParentInheritedAlpha(lua_State *L, int frame_index) {
  constexpr int kMaxParentDepth = 64;

  frame_index = lua_absindex(L, frame_index);
  std::uint8_t inherited_alpha = 0xFF;
  lua_getfield(L, frame_index, "__ow_parent");
  for (int depth = 0; depth < kMaxParentDepth && lua_istable(L, -1) != 0; ++depth) {
    inherited_alpha = MultiplyFrameAlphaBytes(inherited_alpha,
                                              ReadLuaFrameAlphaByteOrDefault(L, -1));
    lua_getfield(L, -1, "__ow_parent");
    lua_remove(L, -2);
  }
  lua_pop(L, 1);
  return inherited_alpha;
}

}
