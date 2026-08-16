#include "openwow/ui/game/debug/lua_ui_debug_adapter.h"

#include "openwow/ui/game/lua_effective_visibility.h"
#include "openwow/ui/lua_table_field.h"

#include <lua.hpp>

#include <string>

namespace openwow::ui::game::debug {
namespace {

std::string LuaFrameKey(lua_State* const lua, const int index) {
  if (lua_istable(lua, index) == 0) return {};
  return openwow::ui::ReadLuaStringField(lua, index, "__ow_frame_key")
      .value_or(openwow::ui::ReadLuaStringField(lua, index, "__ow_name")
                    .value_or(""));
}

bool LuaBoolean(lua_State* const lua, const int index, const char* const field,
                const bool fallback) {
  lua_getfield(lua, lua_absindex(lua, index), field);
  const bool value = lua_isboolean(lua, -1) != 0
                         ? lua_toboolean(lua, -1) != 0
                         : fallback;
  lua_pop(lua, 1);
  return value;
}

}

std::optional<UiDebugLuaState> ReadLuaUiDebugState(
    lua_State* const lua, const int registry_reference) {
  if (lua == nullptr || registry_reference < 0) return std::nullopt;

  const int top = lua_gettop(lua);
  lua_rawgeti(lua, LUA_REGISTRYINDEX, registry_reference);
  if (lua_istable(lua, -1) == 0) {
    lua_settop(lua, top);
    return std::nullopt;
  }

  UiDebugLuaState state;
  state.name =
      openwow::ui::ReadLuaStringField(lua, -1, "__ow_name").value_or("");
  state.kind =
      openwow::ui::ReadLuaStringField(lua, -1, "__ow_type").value_or("");
  lua_getfield(lua, -1, "__ow_parent");
  state.parent_key = LuaFrameKey(lua, -1);
  lua_pop(lua, 1);
  state.texture =
      openwow::ui::ReadLuaStringField(lua, -1, "__ow_texture").value_or("");
  state.locally_visible = LuaBoolean(lua, -1, "__ow_visible", true) &&
                            LuaBoolean(lua, -1, "__ow_draw_layer_enabled", true);
  state.effectively_visible =
      detail::IsLuaWidgetEffectivelyVisibleIterative(lua, -1);
  lua_settop(lua, top);
  return state;
}

}
