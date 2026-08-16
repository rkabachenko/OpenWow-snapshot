#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/frame_anchor_runtime.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_face.h"
#include "openwow/ui/game/framescript/core/frame_text_expansion.h"
#include "openwow/ui/game/framescript/core/frame_layout_methods.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_method_table_runtime.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/core/frame_region_state.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/widgets/font_string_state_methods.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/animation/animation_lua.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/render/resources/fonts/text_alpha_gradient.h"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game::frame_api {

void CreateFontStringTable(lua_State *L, int parent_idx) {
  const bool has_parent =
      parent_idx != 0 && lua_istable(L, parent_idx) != 0;
  parent_idx = has_parent ? lua_absindex(L, parent_idx) : 0;
  lua_newtable(L);
  int fs = lua_absindex(L, -1);

  lua_pushstring(L, "FontString");
  lua_setfield(L, fs, "__ow_type");
  openwow::ui::game::lua_adapter::AttachScriptObjectIdentity(L, fs);

  lua_pushstring(L, "");
  lua_setfield(L, fs, "__ow_text");

  lua_pushboolean(L, 1);
  lua_setfield(L, fs, "__ow_visible");
  if (has_parent) {
    lua_pushvalue(L, parent_idx);
    lua_setfield(L, fs, "__ow_parent");
  }

  if (TryAttachCachedMethodTableToFreshInstance(
          L, fs, kFontStringMethodTableRegistryKey)) {
    if (has_parent) {
      PrependToRegions(L, parent_idx);
    }
    SyncRegionDrawLayerEnabled(L, fs);
    return;
  }

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFontStringTextSelf(Ls, "SetText");
    const char *t = lua_tostring(Ls, 2);
    PushExpandedSimpleRenderScriptText(Ls, t);
    lua_setfield(Ls, self, "__ow_text");
    NotifyFrameInputMutation(Ls, self, false);
    return 0;
  }, 0);
  lua_setfield(L, fs, "SetText");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_text");
    if (!lua_isstring(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnil(Ls);
      return 1;
    }
    const char *txt = lua_tostring(Ls, -1);
    if (!txt || !*txt) {
      lua_pop(Ls, 1);
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, fs, "GetText");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFontStringTextSelf(Ls, "SetFormattedText");
    const int nargs = lua_gettop(Ls) - 1;
    if (nargs <= 0) {
      return 0;
    }

    const int top = lua_gettop(Ls);
    lua_getglobal(Ls, "string");
    lua_getfield(Ls, -1, "format");
    lua_remove(Ls, -2);
    for (int i = 2; i <= top; ++i) {
      lua_pushvalue(Ls, i);
    }
    if (lua_pcall(Ls, nargs, 1, 0) == 0) {
      const char *formatted = lua_tostring(Ls, -1);
      lua_pop(Ls, 1);
      PushExpandedSimpleRenderScriptText(Ls, formatted);
      lua_setfield(Ls, self, "__ow_text");
      NotifyFrameInputMutation(Ls, self, false);
    } else {
      lua_pop(Ls, 1);
    }
    return 0;
  }, 0);
  lua_setfield(L, fs, "SetFormattedText");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "FontString");
    return SharedSetFontWorker(
        Ls, self, lua_adapter::ScriptObjectDisplayName(Ls, self),
        SetFontFailurePolicy::kFontString);
  }, 0);
  lua_setfield(L, fs, "SetFont");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return PushLuaFontStringGetFontResults(
        Ls, ValidateFrameObjectSelf(Ls, "FontString"));
  }, 0);
  lua_setfield(L, fs, "GetFont");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "FontString");
    StorePackedTextColor(Ls, self);
    SyncSharedFontAlphaFromTextColor(Ls, self);
    return 0;
  }, 0);
  lua_setfield(L, fs, "SetTextColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return GetPackedTextColorForTypedObject(Ls, "FontString");
  }, 0);
  lua_setfield(L, fs, "GetTextColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "FontString");

    const double r = GetScriptColorArgumentOrDefault(Ls, 2, 1.0);
    const double g = GetScriptColorArgumentOrDefault(Ls, 3, 1.0);
    const double b = GetScriptColorArgumentOrDefault(Ls, 4, 1.0);

    lua_pushnumber(Ls, NormalizeScriptColorByte(QuantizeScriptColorByte(r)));
    lua_setfield(Ls, self, "__ow_text_r");
    lua_pushnumber(Ls, NormalizeScriptColorByte(QuantizeScriptColorByte(g)));
    lua_setfield(Ls, self, "__ow_text_g");
    lua_pushnumber(Ls, NormalizeScriptColorByte(QuantizeScriptColorByte(b)));
    lua_setfield(Ls, self, "__ow_text_b");

    if (lua_isnumber(Ls, 5) != 0) {
      const double a = lua_tonumber(Ls, 5);
      lua_pushnumber(Ls, NormalizeScriptColorByte(QuantizeScriptColorByte(a)));
      lua_setfield(Ls, self, "__ow_text_a");
    }

    SyncSharedFontAlphaFromTextColor(Ls, self);
    return 0;
  }, 0);
  lua_setfield(L, fs, "SetVertexColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetTableJustifyField(Ls, "FontString", "__ow_justifyH", "SetJustifyH", true);
  }, 0);
  lua_setfield(L, fs, "SetJustifyH");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetTableJustifyField(Ls, "FontString", "__ow_justifyV", "SetJustifyV", false);
  }, 0);
  lua_setfield(L, fs, "SetJustifyV");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaSetPointInternal(Ls, LuaAnchorTargetValidation::kAllowLayoutOnlyTables);
  }, 0);
  lua_setfield(L, fs, "SetPoint");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaClearAllPointsInternal(Ls, LuaAnchorTargetValidation::kAllowLayoutOnlyTables);
  }, 0);
  lua_setfield(L, fs, "ClearAllPoints");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetTypedLuaScriptRegionShown(Ls, "FontString", true);
  }, 0);
  lua_setfield(L, fs, "Show");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetTypedLuaScriptRegionShown(Ls, "FontString", false);
  }, 0);
  lua_setfield(L, fs, "Hide");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    if (openwow::ui::ReadLuaBooleanFieldOrDefault(Ls, 1, "__ow_visible", true)) {
      lua_pushnumber(Ls, 1);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, fs, "IsShown");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    if (!IsLuaTableEffectivelyVisible(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_pushnumber(Ls, 1);
    return 1;
  }, 0);
  lua_setfield(L, fs, "IsVisible");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "FontString");
    const auto measurement = MeasureLuaFontStringMetrics(Ls, self);
    lua_pushnumber(Ls, measurement.has_value() ? measurement->width : 0.0);
    return 1;
  }, 0);
  lua_setfield(L, fs, "GetStringWidth");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "FontString");
    const auto measurement = MeasureLuaFontStringMetrics(Ls, self);
    lua_pushnumber(Ls, measurement.has_value() ? measurement->height : 0.0);
    return 1;
  }, 0);
  lua_setfield(L, fs, "GetStringHeight");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetLuaRegionDimension(Ls, "SetWidth", "width", "__ow_width");
  }, 0);
  lua_setfield(L, fs, "SetWidth");

  lua_pushcfunction(L, LuaRegion_GetWidth);
  lua_setfield(L, fs, "GetWidth");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetLuaRegionDimension(Ls, "SetHeight", "height", "__ow_height");
  }, 0);
  lua_setfield(L, fs, "SetHeight");

  lua_pushcfunction(L, LuaRegion_GetHeight);
  lua_setfield(L, fs, "GetHeight");

  lua_pushcfunction(L, LuaRegion_GetSize);
  lua_setfield(L, fs, "GetSize");

  lua_pushcfunction(L, SetPackedShadowColorForSharedFontObject);
  lua_setfield(L, fs, "SetShadowColor");

  lua_pushcfunction(L, SetShadowOffsetForSharedFontObject);
  lua_setfield(L, fs, "SetShadowOffset");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateSharedFontObjectSelf(Ls);
    if (lua_isnumber(Ls, 2) == 0) {
      const char* usage_name = lua_adapter::ScriptObjectDisplayName(Ls, self);
      return luaL_error(Ls, "Usage: %s:SetAlpha(alpha)", usage_name);
    }

    const auto alpha_byte =
        openwow::ui::game::QuantizeScriptAlphaByteWrapped(lua_tonumber(Ls, 2));
    StoreSharedFontAlphaByte(Ls, self, alpha_byte);
    return 0;
  }, 0);
  lua_setfield(L, fs, "SetAlpha");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateSharedFontObjectSelf(Ls);
    lua_pushnumber(
        Ls, openwow::ui::game::NormalizeFrameAlphaByte(
                ReadSharedFontAlphaByteOrDefault(Ls, self)));
    return 1;
  }, 0);
  lua_setfield(L, fs, "GetAlpha");

  ApplyFontStringIdentityMethods(L, fs);

  lua_pushcclosure(L, [](lua_State * ) -> int { return 0; }, 0);
  lua_setfield(L, fs, "SetAllPoints");
  lua_pushcclosure(L, [](lua_State *Ls) -> int { return SetLuaRegionSize(Ls); }, 0);
  lua_setfield(L, fs, "SetSize");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "FontString");
    if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0) {
      return luaL_error(Ls, "Usage: %s:SetAlphaGradient(start, length)",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }

    const auto start = static_cast<int>(lua_tonumber(Ls, 2));
    const auto length = static_cast<int>(lua_tonumber(Ls, 3));
    lua_pushinteger(Ls, static_cast<lua_Integer>(start));
    lua_setfield(Ls, self, "__ow_alpha_grad_start");
    lua_pushinteger(Ls, static_cast<lua_Integer>(length));
    lua_setfield(Ls, self, "__ow_alpha_grad_length");

    lua_getfield(Ls, self, "__ow_text");
    const char* text = lua_tostring(Ls, -1);
    const bool active = openwow::render::text::IsTextAlphaGradientActive(
        text != nullptr ? std::string_view{text} : std::string_view{}, start,
        length);
    lua_pop(Ls, 1);
    if (!active) {
      lua_pushnil(Ls);
      return 1;
    }

    lua_pushnumber(Ls, 1);
    return 1;
  }, 0);
  lua_setfield(L, fs, "SetAlphaGradient");

  openwow::ui::anim::ApplyAnimationRegionMethods(L);
  ApplyFontStringStateMethods(L, fs);
  lua_pushvalue(L, fs);
  ApplyLayoutFrameMethods(L);
  lua_pop(L, 1);

  ApplyFontStringIdentityMethods(L, fs);

  ApplyCachedMethodTableAndStripFunctions(L, fs,
                                          kFontStringMethodTableRegistryKey);

  if (has_parent) {
    PrependToRegions(L, parent_idx);
  }
  SyncRegionDrawLayerEnabled(L, fs);
}

}
