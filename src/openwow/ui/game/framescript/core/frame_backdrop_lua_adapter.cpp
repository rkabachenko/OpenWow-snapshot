#include "openwow/ui/game/framescript/core/frame_backdrop_runtime.h"
#include "openwow/ui/framexml/framexml_parser_detail.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/script_boolean.h"
#include "openwow/ui/widgets/simple_frame.h"
#include <lua.hpp>
#include <string>
namespace openwow::ui::game::frame_api {
float ReadLuaBackdropNumberFieldOrDefault(lua_State *L, int table_index, const char *field_name,
                                          const float default_value) {
  table_index = lua_absindex(L, table_index);
  lua_getfield(L, table_index, field_name);
  const float value = lua_isnumber(L, -1) != 0
                          ? static_cast<float>(lua_tonumber(L, -1))
                          : default_value;
  lua_pop(L, 1);
  return value;
}

std::string ReadLuaBackdropStringFieldOrDefault(lua_State *L, int table_index,
                                                const char *field_name) {
  table_index = lua_absindex(L, table_index);
  lua_getfield(L, table_index, field_name);
  const char *value = lua_tostring(L, -1);
  std::string result;
  if (value != nullptr) {
    result = value;
  }
  lua_pop(L, 1);
  return result;
}

bool TryReadLuaBackdropShadow(lua_State *L, int frame_index,
                              openwow::ui::widgets::BackdropInfo *out_backdrop) {
  if (out_backdrop == nullptr) {
    return false;
  }

  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, kLuaBackdropField);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }

  const int backdrop_index = lua_absindex(L, -1);
  out_backdrop->edgeSize =
      openwow::ui::framexml::detail::BackdropCtorDefaultEdgeSizePixels();
  out_backdrop->bgFile =
      ReadLuaBackdropStringFieldOrDefault(L, backdrop_index, "bgFile");
  out_backdrop->edgeFile =
      ReadLuaBackdropStringFieldOrDefault(L, backdrop_index, "edgeFile");

  lua_getfield(L, backdrop_index, "tile");
  out_backdrop->tile = openwow::ui::ScriptReadBoolArgOrDefault(L, -1, false);
  lua_pop(L, 1);

  out_backdrop->tileSize =
      ReadLuaBackdropNumberFieldOrDefault(L, backdrop_index, "tileSize", 0.0f);
  out_backdrop->edgeSize = ReadLuaBackdropNumberFieldOrDefault(
      L, backdrop_index, "edgeSize", out_backdrop->edgeSize);

  lua_getfield(L, backdrop_index, "insets");
  if (lua_istable(L, -1) != 0) {
    const int insets_index = lua_absindex(L, -1);
    out_backdrop->insetLeft =
        ReadLuaBackdropNumberFieldOrDefault(L, insets_index, "left", 0.0f);
    out_backdrop->insetRight =
        ReadLuaBackdropNumberFieldOrDefault(L, insets_index, "right", 0.0f);
    out_backdrop->insetTop =
        ReadLuaBackdropNumberFieldOrDefault(L, insets_index, "top", 0.0f);
    out_backdrop->insetBottom =
        ReadLuaBackdropNumberFieldOrDefault(L, insets_index, "bottom", 0.0f);
  }
  lua_pop(L, 2);
  return true;
}

int EnsureBackdropResultTable(lua_State *L) {
  if (lua_istable(L, 2) != 0) {
    lua_pushvalue(L, 2);
  } else {
    lua_createtable(L, 0, 6);
  }
  return lua_absindex(L, -1);
}

int EnsureBackdropInsetsResultTable(lua_State *L, int backdrop_index) {
  backdrop_index = lua_absindex(L, backdrop_index);
  lua_getfield(L, backdrop_index, "insets");
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    lua_createtable(L, 0, 4);
    lua_pushvalue(L, -1);
    lua_setfield(L, backdrop_index, "insets");
  }
  return lua_absindex(L, -1);
}

void FillBackdropResultTable(lua_State *L, int backdrop_index,
                             const openwow::ui::widgets::BackdropInfo &backdrop) {
  backdrop_index = lua_absindex(L, backdrop_index);

  lua_pushstring(L, backdrop.bgFile.c_str());
  lua_setfield(L, backdrop_index, "bgFile");

  lua_pushstring(L, backdrop.edgeFile.c_str());
  lua_setfield(L, backdrop_index, "edgeFile");

  if (backdrop.tile) {
    lua_pushnumber(L, 1.0);
    lua_setfield(L, backdrop_index, "tile");
  } else {
    lua_pushnil(L);
    lua_setfield(L, backdrop_index, "tile");
  }

  lua_pushnumber(L, backdrop.tileSize);
  lua_setfield(L, backdrop_index, "tileSize");

  lua_pushnumber(L, backdrop.edgeSize);
  lua_setfield(L, backdrop_index, "edgeSize");

  const int insets_index = EnsureBackdropInsetsResultTable(L, backdrop_index);
  lua_pushnumber(L, backdrop.insetLeft);
  lua_setfield(L, insets_index, "left");
  lua_pushnumber(L, backdrop.insetRight);
  lua_setfield(L, insets_index, "right");
  lua_pushnumber(L, backdrop.insetTop);
  lua_setfield(L, insets_index, "top");
  lua_pushnumber(L, backdrop.insetBottom);
  lua_setfield(L, insets_index, "bottom");
  lua_pop(L, 1);
}

void PushLuaBackdropColorFieldOrDefault(lua_State *L, int table_index, const char *field_name,
                                        const float default_value) {
  table_index = lua_absindex(L, table_index);
  lua_getfield(L, table_index, field_name);
  if (lua_isnumber(L, -1) == 0) {
    lua_pop(L, 1);
    lua_pushnumber(L, default_value);
  }
}

}
