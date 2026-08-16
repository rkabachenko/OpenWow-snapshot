#include "openwow/ui/game/framescript/widgets/font_string_state_methods.h"

#include "openwow/ui/game/framescript/widgets/edit_box_methods.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/game/framescript/core/frame_font_binding.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/core/frame_layout_state.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_base_methods.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/ui_aspect_scales.h"

#include <lua.hpp>

#include <string>

#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

namespace openwow::ui::game::frame_api {
namespace {
constexpr const char* kFontStringWordWrapField = "__ow_wordwrap";
constexpr const char* kFontStringNonSpaceWrapField = "__ow_nonspacewrap";
constexpr float kMinPositiveTextHeightPixels = 0.00000011920929F;
}

using detail::ScriptReadBoolArgOrDefault;
using detail::lua_pushwowbool;

void ApplyFontStringStateMethods(lua_State *L, int table_idx) {
  int fs = lua_absindex(L, table_idx);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return PushTableJustify(Ls, "__ow_justifyH", "CENTER", true);
  });
  lua_setfield(L, fs, "GetJustifyH");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return PushTableJustify(Ls, "__ow_justifyV", "MIDDLE", false);
  });
  lua_setfield(L, fs, "GetJustifyV");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    const std::string object_name = GetObjectNameOrUnnamed(Ls, self_idx);
    const int argument_type = lua_type(Ls, 2);

    if (argument_type == LUA_TTABLE) {
      if (!openwow::ui::game::lua_adapter::HasScriptObjectIdentity(Ls, 2)) {
        return luaL_error(Ls, "%s:SetFontObject(): Couldn't find 'this' in font object",
                          object_name.c_str());
      }

      if (!openwow::ui::game::lua_adapter::HasCanonicalScriptObjectType(
              Ls, 2, openwow::ui::widgets::ScriptObjectType::Font)) {
        return luaL_error(Ls, "%s:SetFontObject(): Wrong object type, expected font",
                          object_name.c_str());
      }

      SetBoundFontObject(Ls, self_idx, 2);
      CopyNamedFontObjectStyle(Ls, self_idx, 2);
      NotifyFrameInputMutation(Ls, self_idx, false);
      return 0;
    }

    if (argument_type == LUA_TSTRING) {
      const char *font_name = lua_tostring(Ls, 2);
      if (!PushNamedFontObject(Ls, font_name)) {
        return luaL_error(Ls, "%s:SetFontObject(): Couldn't find font named %s",
                          object_name.c_str(), font_name);
      }

      SetBoundFontObject(Ls, self_idx, -1);
      CopyNamedFontObjectStyle(Ls, self_idx, -1);
      lua_pop(Ls, 1);
      NotifyFrameInputMutation(Ls, self_idx, false);
      return 0;
    }

    if (argument_type != LUA_TNONE && argument_type != LUA_TNIL) {
      return luaL_error(Ls, "Usage: %s:SetFontObject(font or \"font\" or nil)",
                        object_name.c_str());
    }

    ClearBoundFontObject(Ls, self_idx);
    NotifyFrameInputMutation(Ls, self_idx, false);
    return 0;
  });
  lua_setfield(L, fs, "SetFontObject");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    lua_getfield(Ls, self_idx, "__ow_font_object");
    return 1;
  });
  lua_setfield(L, fs, "GetFontObject");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    SetFontStringWrapState(Ls, self_idx, kFontStringWordWrapField,
                           ScriptReadBoolArgOrDefault(Ls, 2, true));
    return 0;
  });
  lua_setfield(L, fs, "SetWordWrap");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    lua_pushwowbool(Ls, GetFontStringWrapState(Ls, self_idx, kFontStringWordWrapField, true));
    return 1;
  });
  lua_setfield(L, fs, "CanWordWrap");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    SetFontStringWrapState(Ls, self_idx, kFontStringNonSpaceWrapField,
                           ScriptReadBoolArgOrDefault(Ls, 2, true));
    return 0;
  });
  lua_setfield(L, fs, "SetNonSpaceWrap");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    lua_pushwowbool(Ls, GetFontStringWrapState(Ls, self_idx, kFontStringNonSpaceWrapField, false));
    return 1;
  });
  lua_setfield(L, fs, "CanNonSpaceWrap");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    const std::string object_name = GetObjectNameOrUnnamed(Ls, self_idx);
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:SetTextHeight(pixelHeight)", object_name.c_str());
    }

    const float height_pixels = static_cast<float>(lua_tonumber(Ls, 2));
    if (!(height_pixels > kMinPositiveTextHeightPixels)) {
      return luaL_error(Ls, "%s:SetTextHeight(): invalid texHeight: %f, height must be > 0",
                        object_name.c_str(), height_pixels);
    }

    lua_pushnumber(Ls, openwow::ui::PixelUiHorizontalCoordinateToStored(height_pixels));
    lua_setfield(Ls, self_idx, "__ow_text_height");
    NotifyFrameInputMutation(Ls, self_idx, false);
    return 0;
  });
  lua_setfield(L, fs, "SetTextHeight");

  lua_pushcfunction(L, GetShadowOffsetForSharedFontObject);
  lua_setfield(L, fs, "GetShadowOffset");

  lua_pushcfunction(L, GetPackedShadowColorForSharedFontObject);
  lua_setfield(L, fs, "GetShadowColor");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:SetSpacing(spacing)",
                        GetObjectNameOrUnnamed(Ls, self_idx).c_str());
    }

    const float spacing_pixels = static_cast<float>(lua_tonumber(Ls, 2));
    lua_pushnumber(Ls, openwow::ui::PixelUiHorizontalCoordinateToStored(spacing_pixels));
    lua_setfield(Ls, self_idx, "__ow_spacing");
    NotifyFrameInputMutation(Ls, self_idx, false);
    return 0;
  });
  lua_setfield(L, fs, "SetSpacing");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    lua_getfield(Ls, self_idx, "__ow_spacing");
    if (lua_isnumber(Ls, -1) == 0) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
      return 1;
    }

    const float stored_spacing = static_cast<float>(lua_tonumber(Ls, -1));
    lua_pop(Ls, 1);
    lua_pushnumber(Ls, openwow::ui::StoredUiHorizontalCoordinateToPixels(stored_spacing));
    return 1;
  });
  lua_setfield(L, fs, "GetSpacing");

  lua_pushcfunction(L, SetFontStringIndentedWordWrap);
  lua_setfield(L, fs, "GetIndentedWordWrap");
  lua_pushcfunction(L, GetFontStringIndentedWordWrap);
  lua_setfield(L, fs, "SetIndentedWordWrap");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    int draw_layer_id = 0;
    if (lua_isstring(Ls, 2) == 0 || !TryParseDrawLayerName(lua_tostring(Ls, 2), &draw_layer_id)) {
      return luaL_error(Ls, "Usage: %s:SetDrawLayer(\"layer\")",
                        GetObjectNameOrUnnamed(Ls, self_idx).c_str());
    }

    const char *canonical_layer = GetDrawLayerNameById(draw_layer_id);
    lua_pushstring(Ls, canonical_layer);
    lua_setfield(Ls, self_idx, "__ow_draw_layer");
    SyncTrackedRegionDrawLayer(Ls, self_idx, canonical_layer);
    SyncRegionDrawLayerEnabled(Ls, self_idx);
    return 0;
  });
  lua_setfield(L, fs, "SetDrawLayer");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "FontString");
    lua_pushstring(Ls, ReadRegionDrawLayerName(Ls, self_idx));
    return 1;
  });
  lua_setfield(L, fs, "GetDrawLayer");

  lua_pushcfunction(L, LuaScriptObject_SetParent);
  lua_setfield(L, fs, "SetParent");

  lua_pushcfunction(L, LuaScriptObject_GetParent);
  lua_setfield(L, fs, "GetParent");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    lua_pushnumber(Ls, 8191.0);
    return 1;
  });
  lua_setfield(L, fs, "GetFieldSize");
}

}
