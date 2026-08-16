#pragma once

struct lua_State;

#include "openwow/ui/runtime/lua/lua_binding.h"

namespace openwow::ui::game::frame_api {

void SyncTooltipRegisteredLinesFromSystem(lua_State* lua, int tooltip_index);

void RefreshCursorAnchoredTooltipFrames(lua_State* lua);
[[nodiscard]] bool TooltipRetainedPresentationIsCurrent(
    lua_State* lua, int tooltip_index);

template <lua_CFunction Setter>
int TooltipContentSetterWithSync(lua_State* Ls) {
  const int results = Setter(Ls);
  const int results_base = lua_gettop(Ls) - results;

  SyncTooltipRegisteredLinesFromSystem(Ls, 1);

  lua_settop(Ls, results_base + results);
  return results;
}

template <lua_CFunction Setter>
void RegisterTooltipContentSetter(lua_State* L, int frame_index,
                                  const char* method_name) {
  lua_pushcfunction(L, &TooltipContentSetterWithSync<Setter>);
  lua_setfield(L, frame_index, method_name);
}

}
