#include "openwow/ui/game/framescript/widgets/texture_state_methods.h"

#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/core/frame_layout_state.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_base_methods.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/lua_c_api_convenience.h"

#include <lua.hpp>

#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

namespace openwow::ui::game::frame_api {
namespace {
int ValidateTextureSelf(lua_State* lua) {
  return ValidateTypedFramescriptSelf(lua, "Texture");
}

void SetTextureBooleanFlag(lua_State* L, const char* field_name,
                           const bool default_value) {
  const int self = ValidateTextureSelf(L);
  lua_pushboolean(L, detail::ScriptReadBoolArgOrDefault(L, 2, default_value));
  lua_setfield(L, self, field_name);
}

void SetTextureRenderStateBooleanFlag(
    lua_State* L, const runtime::TextureRenderStateField field,
    const bool default_value) {
  const int self = ValidateTextureSelf(L);
  runtime::SetTextureRenderStateBoolean(
      L, self, field, detail::ScriptReadBoolArgOrDefault(L, 2, default_value));
}
int PushTextureBooleanFlag(lua_State* L, const char* field_name) {
  const int self = ValidateTextureSelf(L);
  lua_getfield(L, self, field_name);
  const bool value = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  detail::lua_pushwowbool(L, value);
  return 1;
}
}

void ApplyTextureStateMethods(lua_State *L, int table_idx) {
  int tx = lua_absindex(L, table_idx);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {

      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 1);
      lua_pushnumber(Ls, 1);
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 1);
      lua_pushnumber(Ls, 1);
      return 8;
    }

    lua_getfield(Ls, 1, "__ow_tc_ul_x");
    if (lua_isnumber(Ls, -1)) {

      lua_getfield(Ls, 1, "__ow_tc_ul_y");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 0);
      }
      lua_getfield(Ls, 1, "__ow_tc_ll_x");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 0);
      }
      lua_getfield(Ls, 1, "__ow_tc_ll_y");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 1);
      }
      lua_getfield(Ls, 1, "__ow_tc_ur_x");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 1);
      }
      lua_getfield(Ls, 1, "__ow_tc_ur_y");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 0);
      }
      lua_getfield(Ls, 1, "__ow_tc_lr_x");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 1);
      }
      lua_getfield(Ls, 1, "__ow_tc_lr_y");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 1);
      }
      return 8;
    }
    lua_pop(Ls, 1);

    double l = 0;
    double r = 1;
    double t = 0;
    double b = 1;
    lua_getfield(Ls, 1, "__ow_tc_l");
    if (lua_isnumber(Ls, -1))
      l = lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
    lua_getfield(Ls, 1, "__ow_tc_r");
    if (lua_isnumber(Ls, -1))
      r = lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
    lua_getfield(Ls, 1, "__ow_tc_t");
    if (lua_isnumber(Ls, -1))
      t = lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
    lua_getfield(Ls, 1, "__ow_tc_b");
    if (lua_isnumber(Ls, -1))
      b = lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
    lua_pushnumber(Ls, l);
    lua_pushnumber(Ls, t);
    lua_pushnumber(Ls, l);
    lua_pushnumber(Ls, b);
    lua_pushnumber(Ls, r);
    lua_pushnumber(Ls, t);
    lua_pushnumber(Ls, r);
    lua_pushnumber(Ls, b);
    return 8;
  });
  lua_setfield(L, tx, "GetTexCoord");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushstring(Ls, "BLEND");
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_blend");
    if (!lua_isstring(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushstring(Ls, "BLEND");
    }
    return 1;
  });
  lua_setfield(L, tx, "GetBlendMode");

  lua_pushcfunction(L, LuaTexture_SetGradientAlpha);
  lua_setfield(L, tx, "SetGradientAlpha");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    SetTextureRenderStateBooleanFlag(
        Ls, runtime::TextureRenderStateField::kHorizontalTile, true);
    return 0;
  });
  lua_setfield(L, tx, "SetHorizTile");

  lua_pushcfunction(
      L, [](lua_State *Ls) -> int { return PushTextureBooleanFlag(Ls, "__ow_horiz_tile"); });
  lua_setfield(L, tx, "GetHorizTile");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    SetTextureRenderStateBooleanFlag(
        Ls, runtime::TextureRenderStateField::kVerticalTile, true);
    return 0;
  });
  lua_setfield(L, tx, "SetVertTile");

  lua_pushcfunction(
      L, [](lua_State *Ls) -> int { return PushTextureBooleanFlag(Ls, "__ow_vert_tile"); });
  lua_setfield(L, tx, "GetVertTile");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTextureSelf(Ls);
    int draw_layer_id = 0;
    if (lua_isstring(Ls, 2) == 0 || !TryParseDrawLayerName(lua_tostring(Ls, 2), &draw_layer_id)) {
      return luaL_error(Ls, "Usage: %s:SetDrawLayer(\"layer\")",
                        GetObjectNameOrUnnamed(Ls, self_idx).c_str());
    }

    const char *canonical_layer = GetDrawLayerNameById(draw_layer_id);
    lua_pushinteger(Ls, static_cast<lua_Integer>(draw_layer_id));
    lua_setfield(Ls, self_idx, "__ow_draw_layer");
    SyncTrackedRegionDrawLayer(Ls, self_idx, canonical_layer);
    SyncRegionDrawLayerEnabled(Ls, self_idx);
    return 0;
  });
  lua_setfield(L, tx, "SetDrawLayer");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTextureSelf(Ls);
    lua_pushstring(Ls, ReadStoredTextureDrawLayerName(Ls, self_idx));
    return 1;
  });
  lua_setfield(L, tx, "GetDrawLayer");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    SetTextureBooleanFlag(Ls, "__ow_nonblocking", true);
    return 0;
  });
  lua_setfield(L, tx, "SetNonBlocking");

  lua_pushcfunction(
      L, [](lua_State *Ls) -> int { return PushTextureBooleanFlag(Ls, "__ow_nonblocking"); });
  lua_setfield(L, tx, "GetNonBlocking");

  lua_pushcfunction(L, LuaScriptObject_SetParent);
  lua_setfield(L, tx, "SetParent");

  lua_pushcfunction(L, LuaScriptObject_GetParent);
  lua_setfield(L, tx, "GetParent");
}

}
