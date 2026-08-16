#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/core/frame_region_state.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/foundation/text/ascii.h"

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

constexpr PackedColorFieldNames kTextColorFieldNames{{
    "__ow_text_r",
    "__ow_text_g",
    "__ow_text_b",
    "__ow_text_a",
}};
constexpr PackedColorDefaultValues kTextColorDefaults{{1.0, 1.0, 1.0, 1.0}};
constexpr PackedColorDefaultValues kTextColorSetterDefaults{{0.0, 0.0, 0.0, 1.0}};
double ClampScriptColorComponent(double value) {
  if (value > 1.0) {
    return 1.0;
  }

  if (!(value >= 0.0)) {
    return 0.0;
  }
  return value;
}

double NormalizeScriptColorByte(std::uint8_t value) {
  return static_cast<double>(value) / 255.0;
}

std::uint8_t QuantizeScriptColorByte(double value) {
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(ClampScriptColorComponent(value) * 255.0 + 0.5),
                 0, 255));
}

double GetScriptColorArgumentOrDefault(lua_State *L,
                                       int argument_index,
                                       double default_value) {
  if (lua_isnumber(L, argument_index) == 0) {
    return default_value;
  }

  return lua_tonumber(L, argument_index);
}

double ReadPackedTableColorFieldOrDefault(lua_State *L,
                                          int table_index,
                                          const char *field_name,
                                          double default_value) {
  table_index = lua_absindex(L, table_index);
  lua_getfield(L, table_index, field_name);
  const double value = lua_isnumber(L, -1) != 0
                           ? NormalizeScriptColorByte(
                                 QuantizeScriptColorByte(lua_tonumber(L, -1)))
                           : default_value;
  lua_pop(L, 1);
  return value;
}

void PushPackedColor(lua_State *L,
                     int table_index,
                     const PackedColorFieldNames &field_names,
                     const PackedColorDefaultValues &default_values) {
  table_index = lua_absindex(L, table_index);
  for (std::size_t index = 0; index < field_names.size(); ++index) {
    lua_pushnumber(L, ReadPackedTableColorFieldOrDefault(
                          L, table_index, field_names[index], default_values[index]));
  }
}

void PushPackedTextColor(lua_State *L, int table_index) {
  PushPackedColor(L, table_index, kTextColorFieldNames, kTextColorDefaults);
}

void StorePackedColor(lua_State *L,
                      int table_index,
                      const PackedColorFieldNames &field_names,
                      const PackedColorDefaultValues &default_values) {
  table_index = lua_absindex(L, table_index);
  const PackedColorDefaultValues values{{
      GetScriptColorArgumentOrDefault(L, 2, default_values[0]),
      GetScriptColorArgumentOrDefault(L, 3, default_values[1]),
      GetScriptColorArgumentOrDefault(L, 4, default_values[2]),
      GetScriptColorArgumentOrDefault(L, 5, default_values[3]),
  }};

  for (std::size_t i = 0; i < values.size(); ++i) {
    lua_pushnumber(L, NormalizeScriptColorByte(QuantizeScriptColorByte(values[i])));
    lua_setfield(L, table_index, field_names[i]);
  }
}

void StorePackedTextColor(lua_State *L, int table_index) {
  StorePackedColor(L, table_index, kTextColorFieldNames,
                   kTextColorSetterDefaults);
}

std::uint8_t ReadTextureAlphaByteOrDefault(lua_State *L,
                                            int table_index,
                                            std::uint8_t fallback) {
  table_index = lua_absindex(L, table_index);
  double alpha = openwow::ui::game::NormalizeFrameAlphaByte(fallback);

  lua_getfield(L, table_index, "__ow_vc_a");
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
  }
  lua_pop(L, 1);

  lua_getfield(L, table_index, "__ow_alpha");
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
}

void StoreTextureAlphaByte(lua_State *L, int table_index,
                           const std::uint8_t alpha_byte) {
  table_index = lua_absindex(L, table_index);
  const double normalized_alpha =
      openwow::ui::game::NormalizeFrameAlphaByte(alpha_byte);
  runtime::SetTextureRenderStateNumber(
      L, table_index, runtime::TextureRenderStateField::kVertexColorA,
      normalized_alpha);
  lua_pushnumber(L, normalized_alpha);
  lua_setfield(L, table_index, "__ow_alpha");
}

std::uint8_t ReadSharedFontAlphaByteOrDefault(lua_State *L,
                                              int table_index,
                                              std::uint8_t fallback) {
  table_index = lua_absindex(L, table_index);
  double alpha = openwow::ui::game::NormalizeFrameAlphaByte(fallback);

  lua_getfield(L, table_index, "__ow_text_a");
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
  }
  lua_pop(L, 1);

  lua_getfield(L, table_index, "__ow_alpha");
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
}

void StoreSharedFontAlphaByte(lua_State *L, int table_index,
                              const std::uint8_t alpha_byte) {
  table_index = lua_absindex(L, table_index);
  const double normalized_alpha =
      openwow::ui::game::NormalizeFrameAlphaByte(alpha_byte);
  lua_pushnumber(L, normalized_alpha);
  lua_setfield(L, table_index, "__ow_alpha");
  lua_pushnumber(L, normalized_alpha);
  lua_setfield(L, table_index, "__ow_text_a");
}

void SyncSharedFontAlphaFromTextColor(lua_State *L, int table_index) {
  StoreSharedFontAlphaByte(
      L, table_index,
      openwow::ui::game::QuantizeFrameAlphaByteTruncated(
          ReadPackedTableColorFieldOrDefault(L, table_index, "__ow_text_a", 1.0)));
}

int SetTypedLuaScriptRegionShown(lua_State *L, const char *expected_type,
                                const bool shown) {
  const int self_index = ValidateFrameObjectSelf(L, expected_type);
  if (openwow::ui::game::detail::SetLuaScriptRegionShown(
          L, self_index, shown)) {
    NotifyFrameInputMutation(L, self_index, true);
  }
  return 0;
}

int ValidateFrameSelf(lua_State *L) {
  return ValidateFrameObjectSelf(L, "Frame");
}

int ValidateSharedFontObjectSelf(lua_State *L) {
  using openwow::ui::widgets::ScriptObjectType;

  const int self_index = ValidateFrameScriptSelf(L);
  const auto type = openwow::ui::game::detail::GetLuaCanonicalScriptObjectType(L, self_index);
  if (type != ScriptObjectType::Font && type != ScriptObjectType::FontString) {
    luaL_error(L, "Wrong object type for member function");
  }

  return self_index;
}

int PushSharedFontObjectName(lua_State* L) {
  const int self_index = ValidateSharedFontObjectSelf(L);
  lua_getfield(L, self_index, "__ow_name");
  const char* name = lua_tostring(L, -1);
  if (name != nullptr && *name != '\0') {
    return 1;
  }

  lua_pop(L, 1);
  lua_pushnil(L);
  return 1;
}

int ValidateFrameLikeObjectSelf(lua_State* L) {
  const int self_index = ValidateFrameScriptSelf(L);
  if (!IsFrameLikeScriptObjectType(GetLuaScriptObjectType(L, self_index))) {
    luaL_error(L, "Wrong object type for member function");
  }

  return self_index;
}

int SetPackedTextColorForTypedObject(lua_State *L, const char *expected_type) {
  const int self_index = ValidateFrameObjectSelf(L, expected_type);
  StorePackedTextColor(L, self_index);
  return 0;
}

int GetPackedTextColorForTypedObject(lua_State *L, const char *expected_type) {
  const int self_index = ValidateFrameObjectSelf(L, expected_type);
  PushPackedTextColor(L, self_index);
  return 4;
}

void SetIndentedWordWrapForTypedObject(lua_State *L,
                                       const char *expected_type,
                                       const char *field_name) {
  const int self_index = ValidateFrameObjectSelf(L, expected_type);
  lua_pushboolean(L, detail::ScriptReadBoolArgOrDefault(L, 2, true));
  lua_setfield(L, self_index, field_name);
}

int GetIndentedWordWrapForTypedObject(lua_State *L,
                                      const char *expected_type,
                                      const char *field_name) {
  const int self_index = ValidateFrameObjectSelf(L, expected_type);
  lua_getfield(L, self_index, field_name);
  const bool enabled = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (enabled) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

const char *TypedFontInstanceReceiver(lua_State *L) {
  const char *type_name = lua_tostring(L, lua_upvalueindex(1));
  return type_name != nullptr ? type_name : "Frame";
}

int LuaTypedFontInstanceSetFontObject(lua_State *L) {
  const char *type_name = TypedFontInstanceReceiver(L);
  const int self = ValidateFrameObjectSelf(L, type_name);
  const char* object_name = lua_adapter::ScriptObjectDisplayName(L, self);
  const int argument_type = lua_type(L, 2);

  if (argument_type == LUA_TTABLE) {
    if (!openwow::ui::game::lua_adapter::HasScriptObjectIdentity(L, 2)) {
      return luaL_error(L,
                        "%s:SetFontObject(): Couldn't find 'this' in font object",
                        object_name);
    }
    if (!openwow::ui::game::lua_adapter::HasCanonicalScriptObjectType(
            L, 2, openwow::ui::widgets::ScriptObjectType::Font)) {
      return luaL_error(L,
                        "%s:SetFontObject(): Wrong object type, expected font",
                        object_name);
    }
    if (WouldIntroduceFontObjectBindingCycle(L, self, 2)) {
      return luaL_error(L,
                        "%s:SetFontObject(): Can't create a font object loop",
                        object_name);
    }
    SetBoundFontObject(L, self, 2);
    CopyNamedFontObjectStyle(L, self, 2);
    return 0;
  }

  if (argument_type == LUA_TSTRING) {
    const char *font_name = lua_tostring(L, 2);
    if (PushNamedFontObject(L, font_name)) {
      SetBoundFontObject(L, self, -1);
      CopyNamedFontObjectStyle(L, self, -1);
      lua_pop(L, 1);
      return 0;
    }
    lua_pop(L, 1);
    return luaL_error(L, "%s:SetFontObject(): Couldn't find font named %s",
                      object_name, font_name);
  }

  if (argument_type != LUA_TNONE && argument_type != LUA_TNIL) {
    return luaL_error(L, "Usage: %s:SetFontObject(font or \"font\" or nil)",
                      object_name);
  }

  ClearBoundFontObject(L, self);
  return 0;
}

int LuaTypedFontInstanceGetFontObject(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, TypedFontInstanceReceiver(L));
  lua_getfield(L, self, "__ow_font_object");
  return 1;
}

int LuaTypedFontInstanceSetShadowColor(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, TypedFontInstanceReceiver(L));
  StorePackedColor(L, self, kShadowColorFieldNames, kShadowColorDefaults);
  return 0;
}

int LuaTypedFontInstanceGetShadowColor(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, TypedFontInstanceReceiver(L));
  PushPackedColor(L, self, kShadowColorFieldNames, kShadowColorDefaults);
  return 4;
}

int LuaTypedFontInstanceSetShadowOffset(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, TypedFontInstanceReceiver(L));
  if (lua_isnumber(L, 2) == 0 || lua_isnumber(L, 3) == 0) {
    return luaL_error(L, "Usage: %s:SetShadowOffset(x, y)",
                      lua_adapter::ScriptObjectDisplayName(L, self));
  }
  StoreShadowOffsetForObject(L, self, static_cast<float>(lua_tonumber(L, 2)),
                             static_cast<float>(lua_tonumber(L, 3)));
  return 0;
}

int LuaTypedFontInstanceGetShadowOffset(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, TypedFontInstanceReceiver(L));
  PushShadowOffsetComponentForObject(L, self, "__ow_shadow_x");
  PushShadowOffsetComponentForObject(L, self, "__ow_shadow_y");
  return 2;
}

int LuaTypedFontInstanceSetJustify(lua_State *L) {
  const char *type_name = TypedFontInstanceReceiver(L);
  const char *field_name = lua_tostring(L, lua_upvalueindex(2));
  const char *method_name = lua_tostring(L, lua_upvalueindex(3));
  return SetTableJustifyField(L, type_name, field_name, method_name,
                              std::strcmp(field_name, "__ow_justifyH") == 0);
}

int LuaTypedFontInstanceGetJustify(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, TypedFontInstanceReceiver(L));
  const char *field_name = lua_tostring(L, lua_upvalueindex(2));
  const char *default_value = lua_tostring(L, lua_upvalueindex(3));
  const bool horizontal = std::strcmp(field_name, "__ow_justifyH") == 0;

  lua_getfield(L, self, field_name);
  const char *stored = lua_tostring(L, -1);
  if (stored == nullptr || stored[0] == '\0') {
    lua_pop(L, 1);
    stored = default_value;
  }

  std::uint32_t flags = 0;
  const int parsed = horizontal
                         ? openwow::ui::StringToHorizontalJustify(stored, &flags)
                         : openwow::ui::StringToVerticalJustify(stored, &flags);
  lua_pushstring(L, horizontal
                        ? openwow::ui::HorizontalJustifyFlagsToString(parsed ? flags : 0)
                        : openwow::ui::VerticalJustifyFlagsToString(parsed ? flags : 0));
  return 1;
}

int LuaTypedFontInstanceSetSpacing(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, TypedFontInstanceReceiver(L));
  if (lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:SetSpacing(spacing)",
                      lua_adapter::ScriptObjectDisplayName(L, self));
  }
  lua_pushnumber(L, openwow::ui::PixelUiHorizontalCoordinateToStored(
                        static_cast<float>(lua_tonumber(L, 2))));
  lua_setfield(L, self, "__ow_spacing");
  return 0;
}

int LuaTypedFontInstanceGetSpacing(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, TypedFontInstanceReceiver(L));
  lua_getfield(L, self, "__ow_spacing");
  if (lua_isnumber(L, -1) == 0) {
    lua_pop(L, 1);
    lua_pushnumber(L, 0.0);
    return 1;
  }
  const float stored = static_cast<float>(lua_tonumber(L, -1));
  lua_pop(L, 1);
  lua_pushnumber(L, openwow::ui::StoredUiHorizontalCoordinateToPixels(stored));
  return 1;
}

void InstallTypedFontMethod(lua_State *L, const int table_index,
                            const char *type_name, const char *method_name,
                            lua_CFunction function) {
  lua_pushstring(L, type_name);
  lua_pushcclosure(L, function, 1);
  lua_setfield(L, table_index, method_name);
}

void InstallTypedFontJustifyPair(lua_State *L, const int table_index,
                                 const char *type_name, const bool horizontal,
                                 const char *default_value) {
  const char *field_name = horizontal ? "__ow_justifyH" : "__ow_justifyV";
  const char *setter_name = horizontal ? "SetJustifyH" : "SetJustifyV";
  const char *getter_name = horizontal ? "GetJustifyH" : "GetJustifyV";

  lua_pushstring(L, type_name);
  lua_pushstring(L, field_name);
  lua_pushstring(L, setter_name);
  lua_pushcclosure(L, LuaTypedFontInstanceSetJustify, 3);
  lua_setfield(L, table_index, setter_name);

  lua_pushstring(L, type_name);
  lua_pushstring(L, field_name);
  lua_pushstring(L, default_value);
  lua_pushcclosure(L, LuaTypedFontInstanceGetJustify, 3);
  lua_setfield(L, table_index, getter_name);
}

void InstallTypedFontObjectPair(lua_State *L, const int table_index,
                                const char *type_name) {
  InstallTypedFontMethod(L, table_index, type_name, "SetFontObject",
                         LuaTypedFontInstanceSetFontObject);
  InstallTypedFontMethod(L, table_index, type_name, "GetFontObject",
                         LuaTypedFontInstanceGetFontObject);
}

void InstallTypedFontShadowMethods(lua_State *L, const int table_index,
                                   const char *type_name) {
  InstallTypedFontMethod(L, table_index, type_name, "SetShadowColor",
                         LuaTypedFontInstanceSetShadowColor);
  InstallTypedFontMethod(L, table_index, type_name, "GetShadowColor",
                         LuaTypedFontInstanceGetShadowColor);
  InstallTypedFontMethod(L, table_index, type_name, "SetShadowOffset",
                         LuaTypedFontInstanceSetShadowOffset);
  InstallTypedFontMethod(L, table_index, type_name, "GetShadowOffset",
                         LuaTypedFontInstanceGetShadowOffset);
}

void InstallTypedFontSpacingPair(lua_State *L, const int table_index,
                                 const char *type_name) {
  InstallTypedFontMethod(L, table_index, type_name, "SetSpacing",
                         LuaTypedFontInstanceSetSpacing);
  InstallTypedFontMethod(L, table_index, type_name, "GetSpacing",
                         LuaTypedFontInstanceGetSpacing);
}

}
