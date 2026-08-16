#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/widgets/button_method_support.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/frame_text_expansion.h"
#include "openwow/ui/game/lua_frame_mutation_policy.h"
#include "openwow/ui/game/lua_mouse_button_context.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/ui_enum_helpers.h"

#include <lua.hpp>

#include <cstring>
#include <string>

namespace openwow::ui::game::frame_api {

static void ApplyButtonStateMethods(lua_State *L) {
  int f = lua_absindex(L, -1);
  lua_pushnumber(L, 1);
  lua_setfield(L, f, "__ow_btn_push_off_x");
  lua_pushnumber(L, -1);
  lua_setfield(L, f, "__ow_btn_push_off_y");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, luaL_optnumber(Ls, 2, 0));
      lua_setfield(Ls, 1, "__ow_btn_push_off_x");
      lua_pushnumber(Ls, luaL_optnumber(Ls, 3, 0));
      lua_setfield(Ls, 1, "__ow_btn_push_off_y");
      RefreshButtonLabelFont(Ls, 1);
    }
    return 0;
  });
  lua_setfield(L, f, "SetPushedTextOffset");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 1);
      lua_pushnumber(Ls, -1);
      return 2;
    }
    lua_getfield(Ls, 1, "__ow_btn_push_off_x");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 1);
    }
    lua_getfield(Ls, 1, "__ow_btn_push_off_y");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, -1);
    }
    return 2;
  });
  lua_setfield(L, f, "GetPushedTextOffset");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, ScriptReadBoolArgOrDefault(Ls, 2, false));
      lua_setfield(Ls, 1, "__ow_btn_motion_disabled");
    }
    return 0;
  });
  lua_setfield(L, f, "SetMotionScriptsWhileDisabled");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_btn_motion_disabled");
    lua_pushboolean(Ls, lua_toboolean(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  });
  lua_setfield(L, f, "GetMotionScriptsWhileDisabled");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_btn_pushed_tex");
    return 1;
  });
  lua_setfield(L, f, "GetPushedTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_btn_disabled_tex");
    return 1;
  });
  lua_setfield(L, f, "GetDisabledTexture");
}

void ApplyButtonMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      return 0;
    }

    SetButtonTextValue(Ls, 1, lua_tostring(Ls, 2));
    return 0;
  }, 0);
  lua_setfield(L, f, "SetText");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Button");
    const int argument_count = lua_gettop(Ls) - 1;
    if (argument_count <= 0) {
      return 0;
    }

    lua_getglobal(Ls, "string");
    lua_getfield(Ls, -1, "format");
    lua_remove(Ls, -2);
    for (int argument = 2; argument <= argument_count + 1; ++argument) {
      lua_pushvalue(Ls, argument);
    }
    lua_call(Ls, argument_count, 1);

    const char *formatted = lua_tostring(Ls, -1);
    std::string bounded = formatted != nullptr ? formatted : "";
    if (bounded.size() > 4095) {
      bounded.resize(4095);
    }
    SetButtonTextValue(Ls, self, bounded.c_str());
    lua_pop(Ls, 1);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetFormattedText");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushstring(Ls, "");
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_btn_fontstr");
    if (lua_istable(Ls, -1) == 0) {
      lua_pop(Ls, 1);
      lua_pushstring(Ls, "");
      return 1;
    }
    lua_getfield(Ls, -1, "__ow_text");
    lua_remove(Ls, -2);
    if (lua_isstring(Ls, -1) == 0) {
      lua_pop(Ls, 1);
      lua_pushstring(Ls, "");
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetText");

  using TextureRole = openwow::ui::framexml::TextureRole;
  InstallNativeTextureSlotMethods(L, f, "NormalTexture",
                                  "__ow_btn_normal_tex",
                                  TextureRole::ButtonNormal, "ARTWORK");
  InstallNativeTextureSlotMethods(L, f, "PushedTexture",
                                  "__ow_btn_pushed_tex",
                                  TextureRole::ButtonPushed, "ARTWORK");
  InstallNativeTextureSlotMethods(L, f, "DisabledTexture",
                                  "__ow_btn_disabled_tex",
                                  TextureRole::ButtonDisabled, "ARTWORK");
  InstallNativeTextureSlotMethods(L, f, "HighlightTexture",
                                  "__ow_btn_highlight_tex",
                                  TextureRole::ButtonHighlight, "HIGHLIGHT");

  InstallButtonFontObjectMethods(
      L, f, "NormalFontObject", "__ow_btn_normal_font",
      "Usage: %s:SetNormalFontObject(\"fontname\" or fontObject)");
  InstallButtonFontObjectMethods(
      L, f, "HighlightFontObject", "__ow_btn_hl_font",
      "Usage: %s:SetHighlightFontObject(\"fontname\")");
  InstallButtonFontObjectMethods(
      L, f, "DisabledFontObject", "__ow_btn_dis_font",
      "Usage: %s:SetDisabledFontObject(\"fontname\")");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Button");
    if (lua_adapter::IsFrameMutationBlocked(Ls, self)) {
      return 0;
    }
    lua_pushboolean(Ls, 1);
    lua_setfield(Ls, self, "__ow_btn_enabled");
    lua_pushstring(Ls, "NORMAL");
    lua_setfield(Ls, self, "__ow_btn_state");
    RefreshButtonLabelFont(Ls, self);
    FireScript(Ls, self, "OnEnable");
    return 0;
  }, 0);
  lua_setfield(L, f, "Enable");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Button");
    if (lua_adapter::IsFrameMutationBlocked(Ls, self)) {
      return 0;
    }
    lua_pushboolean(Ls, 0);
    lua_setfield(Ls, self, "__ow_btn_enabled");
    lua_pushstring(Ls, "DISABLED");
    lua_setfield(Ls, self, "__ow_btn_state");
    RefreshButtonLabelFont(Ls, self);
    FireScript(Ls, self, "OnDisable");
    return 0;
  }, 0);
  lua_setfield(L, f, "Disable");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 1);
      return 1;
    }

    lua_getfield(Ls, 1, "__ow_btn_enabled");
    if (lua_isboolean(Ls, -1) && !lua_toboolean(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnil(Ls);
      return 1;
    }
    lua_pop(Ls, 1);

    lua_getfield(Ls, 1, "__ow_btn_state");
    const char *st = lua_tostring(Ls, -1);
    const bool disabled = st != nullptr && std::strcmp(st, "DISABLED") == 0;
    lua_pop(Ls, 1);
    if (disabled) lua_pushnil(Ls); else lua_pushnumber(Ls, 1);
    return 1;
  }, 0);
  lua_setfield(L, f, "IsEnabled");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;

    const char *btn = lua_isstring(Ls, 2) ? lua_tostring(Ls, 2) : "LeftButton";
    const bool is_down = ScriptReadBoolArgOrDefault(Ls, 3, false);

    lua_getfield(Ls, 1, "__ow_click_in_progress");
    if (lua_toboolean(Ls, -1)) {
      lua_pop(Ls, 1);
      return 0;
    }
    lua_pop(Ls, 1);

    lua_getfield(Ls, 1, "__ow_btn_state");
    const char *st = lua_tostring(Ls, -1);
    const bool disabled = st != nullptr && std::strcmp(st, "DISABLED") == 0;
    lua_pop(Ls, 1);
    if (disabled)
      return 0;

    const lua_adapter::ScopedMouseButtonOverride mouse_button_override(
        Ls, btn != nullptr ? btn : "");

    lua_pushboolean(Ls, 1);
    lua_setfield(Ls, 1, "__ow_click_in_progress");

    lua_pushstring(Ls, btn);
    lua_pushboolean(Ls, is_down);
    FireScript(Ls, 1, "PreClick", 2);

    lua_pushstring(Ls, btn);
    lua_pushboolean(Ls, is_down);
    FireScript(Ls, 1, "OnClick", 2);

    lua_pushstring(Ls, btn);
    lua_pushboolean(Ls, is_down);
    FireScript(Ls, 1, "PostClick", 2);

    lua_pushboolean(Ls, 0);
    lua_setfield(Ls, 1, "__ow_click_in_progress");

    return 0;
  }, 0);
  lua_setfield(L, f, "Click");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      return 0;
    }

    lua_newtable(Ls);
    const int clicks_idx = lua_absindex(Ls, -1);
    lua_Integer out_index = 1;
    for (int arg = 2; arg <= lua_gettop(Ls); ++arg) {
      if (lua_isstring(Ls, arg) == 0) {
        continue;
      }
      lua_pushvalue(Ls, arg);
      lua_seti(Ls, clicks_idx, out_index++);
    }
    lua_setfield(Ls, 1, kRegisteredClicksField);
    return 0;
  }, 0);
  lua_setfield(L, f, "RegisterForClicks");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushstring(Ls, "NORMAL");
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_btn_state");
    if (!lua_isstring(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushstring(Ls, "NORMAL");
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetButtonState");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {

    if (!lua_istable(Ls, 1)) return 0;
    const char *stateStr = lua_isstring(Ls, 2) ? lua_tostring(Ls, 2) : nullptr;
    int slot = 0;
    if (!stateStr ||
        !openwow::ui::StringToButtonTextureSlot(stateStr, &slot)) {
      return luaL_error(Ls,
          "Usage: <button>:SetButtonState(\"state\", lock)");
    }

    lua_pushstring(Ls, openwow::ui::ButtonTextureSlotToString(slot));
    lua_setfield(Ls, 1, "__ow_btn_state");

    const bool locked = ScriptReadBoolArgOrDefault(Ls, 3, false);
    lua_pushboolean(Ls, locked ? 1 : 0);
    lua_setfield(Ls, 1, "__ow_btn_state_locked");
    RefreshButtonLabelFont(Ls, 1);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetButtonState");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 1);
      lua_setfield(Ls, 1, "__ow_btn_hl_locked");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "LockHighlight");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 0);
      lua_setfield(Ls, 1, "__ow_btn_hl_locked");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "UnlockHighlight");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_btn_fontstr");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
      return 1;
    }
    const int fs = lua_absindex(Ls, -1);
    const auto result = MeasureLuaFontStringMetrics(Ls, fs);
    lua_pop(Ls, 1);
    lua_pushnumber(Ls, result.has_value() ? static_cast<double>(result->width) : 0.0);
    return 1;
  }, 0);
  lua_setfield(L, f, "GetTextWidth");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_btn_fontstr");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
      return 1;
    }
    const int fs = lua_absindex(Ls, -1);
    const auto result = MeasureLuaFontStringMetrics(Ls, fs);
    lua_pop(Ls, 1);
    lua_pushnumber(Ls, result.has_value() ? static_cast<double>(result->height) : 0.0);
    return 1;
  }, 0);
  lua_setfield(L, f, "GetTextHeight");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_btn_fontstr");
    if (lua_istable(Ls, -1))
      return 1;
    lua_pop(Ls, 1);

    lua_pushnil(Ls);
    return 1;
  }, 0);
  lua_setfield(L, f, "GetFontString");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1) || lua_istable(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:SetFontString(fontstring)",
                        lua_adapter::ScriptObjectDisplayName(Ls, 1));
    }

    const auto type_name = openwow::ui::ReadLuaStringField(Ls, 2, "__ow_type");
    if (!type_name.has_value() || type_name->empty()) {
      return luaL_error(
          Ls, "%s:SetFontString(): Couldn't find 'this' in fontstring",
          lua_adapter::ScriptObjectDisplayName(Ls, 1));
    }
    if (*type_name != "FontString") {
      return luaL_error(
          Ls, "%s:SetFontString(): Wrong object type, expected fontstring",
          lua_adapter::ScriptObjectDisplayName(Ls, 1));
    }

    BindButtonFontStringRegion(Ls, 1, 2);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetFontString");

  lua_pushboolean(L, 1);
  lua_setfield(L, f, "__ow_btn_enabled");

  SetRegisteredClicks(L, f, {"LeftButtonUp"});
  ApplyButtonStateMethods(L);
}

void ApplyCheckButtonMethods(lua_State *L) {
  ApplyButtonMethods(L);
  int f = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_checked");
    const bool checked = lua_toboolean(Ls, -1) != 0;
    lua_pop(Ls, 1);
    if (checked) lua_pushnumber(Ls, 1); else lua_pushnil(Ls);
    return 1;
  }, 0);
  lua_setfield(L, f, "GetChecked");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {

      lua_pushboolean(Ls, ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_checked");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetChecked");

  using TextureRole = openwow::ui::framexml::TextureRole;
  InstallNativeTextureSlotMethods(L, f, "CheckedTexture",
                                  "__ow_checked_tex",
                                  TextureRole::CheckButtonChecked, "OVERLAY");
  InstallNativeTextureSlotMethods(
      L, f, "DisabledCheckedTexture", "__ow_disabled_checked_tex",
      TextureRole::CheckButtonDisabledChecked, "OVERLAY");
}

bool CallTextureSetPath(lua_State *L, const int texture_index,
                               const int path_index) {
  const int texture = lua_absindex(L, texture_index);
  const int path = lua_absindex(L, path_index);
  lua_getfield(L, texture, "SetTexture");
  if (lua_isfunction(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }
  lua_pushvalue(L, texture);
  lua_pushvalue(L, path);
  lua_call(L, 2, 1);
  const bool loaded = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return loaded;
}

static void ApplyNativeTextureBlendMode(lua_State *L, const int texture_index,
                                        const int blend_index) {
  if (blend_index == 0 || lua_isstring(L, blend_index) == 0) {
    return;
  }
  const int texture = lua_absindex(L, texture_index);
  const int blend = lua_absindex(L, blend_index);
  lua_getfield(L, texture, "SetBlendMode");
  if (lua_isfunction(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }
  lua_pushvalue(L, texture);
  lua_pushvalue(L, blend);
  lua_call(L, 2, 0);
}

static int LuaSetNativeTextureSlot(lua_State *L) {
  using TextureRole = openwow::ui::framexml::TextureRole;
  const char *slot = lua_tostring(L, lua_upvalueindex(1));
  const auto role = static_cast<TextureRole>(
      lua_tointeger(L, lua_upvalueindex(2)));
  const char *method = lua_tostring(L, lua_upvalueindex(3));
  const char *draw_layer = lua_tostring(L, lua_upvalueindex(4));
  if (lua_istable(L, 1) == 0 || slot == nullptr) {
    return 0;
  }

  if (lua_isnoneornil(L, 2)) {
    lua_getfield(L, 1, slot);
    if (lua_istable(L, -1) != 0) {
      ReleaseTextureOwnership(L, -1);
    }
    lua_pop(L, 1);
    lua_pushnil(L);
    lua_setfield(L, 1, slot);
    return 0;
  }

  if (lua_istable(L, 2) != 0) {
    if (!lua_adapter::IsScriptObjectKindOf(
            L, 2, openwow::ui::widgets::ScriptObjectType::Texture)) {
      return luaL_error(L, "%s:%s(): Wrong object type, expected texture",
                        lua_adapter::ScriptObjectDisplayName(L, 1), method);
    }
    lua_getfield(L, 1, slot);
    if (lua_istable(L, -1) != 0 && lua_rawequal(L, -1, 2) == 0) {
      ReleaseTextureOwnership(L, -1);
    }
    lua_pop(L, 1);
    BindTextureOwnership(L, 2, 1, role);
    if (draw_layer != nullptr && draw_layer[0] != '\0') {
      lua_pushstring(L, draw_layer);
      lua_setfield(L, 2, "__ow_draw_layer");
      SyncRegionDrawLayerEnabled(L, 2);
    }
    ApplyNativeTextureBlendMode(L, 2, lua_gettop(L) >= 3 ? 3 : 0);
    lua_pushvalue(L, 2);
    lua_setfield(L, 1, slot);
    return 0;
  }

  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:%s(texture or \"texture\" or nil)",
                      lua_adapter::ScriptObjectDisplayName(L, 1), method);
  }

  lua_getfield(L, 1, slot);
  if (lua_istable(L, -1) != 0) {
    const int texture = lua_absindex(L, -1);
    if (CallTextureSetPath(L, texture, 2)) {
      ApplyNativeTextureBlendMode(L, texture,
                                  lua_gettop(L) >= 3 ? 3 : 0);
    }
    lua_pop(L, 1);
    return 0;
  }
  lua_pop(L, 1);

  CreateTextureTable(L, 1);
  const int texture = lua_absindex(L, -1);
  lua_pushboolean(L, 1);
  lua_setfield(L, texture, "__ow_setAllPoints");
  if (draw_layer != nullptr && draw_layer[0] != '\0') {
    lua_pushstring(L, draw_layer);
    lua_setfield(L, texture, "__ow_draw_layer");
  }
  if (!CallTextureSetPath(L, texture, 2)) {
    ReparentScriptObjectTable(L, texture, 0);
    lua_pop(L, 1);
    return 0;
  }
  ApplyNativeTextureBlendMode(L, texture, lua_gettop(L) >= 3 ? 3 : 0);
  TrackRuntimeRegion(L, 1, texture, "Texture", nullptr, draw_layer, role,
                     true);
  lua_pushvalue(L, texture);
  lua_setfield(L, 1, slot);
  lua_pop(L, 1);
  return 0;
}

static int LuaGetNativeTextureSlot(lua_State *L) {
  const char *slot = lua_tostring(L, lua_upvalueindex(1));
  if (lua_istable(L, 1) != 0 && slot != nullptr) {
    lua_getfield(L, 1, slot);
    return 1;
  }
  lua_pushnil(L);
  return 1;
}

void InstallNativeTextureSlotMethods(
    lua_State *L, const int owner_index, const char *method_suffix,
    const char *slot, const openwow::ui::framexml::TextureRole role,
    const char *draw_layer) {
  const int owner = lua_absindex(L, owner_index);
  const std::string setter = std::string("Set") + method_suffix;
  const std::string getter = std::string("Get") + method_suffix;
  lua_pushstring(L, slot);
  lua_pushinteger(L, static_cast<lua_Integer>(role));
  lua_pushstring(L, setter.c_str());
  lua_pushstring(L, draw_layer);
  lua_pushcclosure(L, LuaSetNativeTextureSlot, 4);
  lua_setfield(L, owner, setter.c_str());

  lua_pushstring(L, slot);
  lua_pushcclosure(L, LuaGetNativeTextureSlot, 1);
  lua_setfield(L, owner, getter.c_str());
}

void SetButtonTextValue(lua_State *L, const int self_index, const char *text) {
  const int self = lua_absindex(L, self_index);
  lua_getfield(L, self, "__ow_btn_fontstr");
  const bool has_font_string = lua_istable(L, -1) != 0;
  if (!has_font_string && (text == nullptr || text[0] == '\0')) {
    lua_pop(L, 1);
    return;
  }

  if (!has_font_string) {
    lua_pop(L, 1);
    CreateFontStringTable(L, self);
    BindButtonFontStringRegion(L, self, -1);

    TrackRuntimeRegion(L, self, -1, "FontString", nullptr, "ARTWORK");
  }

  const int font_string = lua_absindex(L, -1);
  PushExpandedSimpleRenderScriptText(L, text);
  lua_setfield(L, font_string, "__ow_text");
  NotifyFrameInputMutation(L, font_string, false);
  lua_pop(L, 1);
  RefreshButtonLabelFont(L, self);
}

void RefreshButtonLabelFont(lua_State *L, const int button_index) {
  const int button = lua_absindex(L, button_index);
  lua_getfield(L, button, "__ow_btn_fontstr");
  if (lua_istable(L, -1) != 0) {
    (void)ResolveButtonLabelAnchorPoint(L, button, -1);
    const int font_string = lua_absindex(L, -1);
    lua_getfield(L, button, "__ow_btn_state");
    const char *state = lua_tostring(L, -1);
    const bool pushed = state != nullptr && std::strcmp(state, "PUSHED") == 0;
    lua_pop(L, 1);
    const auto read_offset = [&](const char *field, const lua_Number fallback) {
      lua_getfield(L, button, field);
      const lua_Number result =
          lua_isnumber(L, -1) != 0 ? lua_tonumber(L, -1) : fallback;
      lua_pop(L, 1);
      return result;
    };
    lua_pushnumber(L, pushed ? read_offset("__ow_btn_push_off_x", 1.0) : 0.0);
    lua_setfield(L, font_string, "__ow_layout_offset_x");
    lua_pushnumber(L, pushed ? read_offset("__ow_btn_push_off_y", -1.0) : 0.0);
    lua_setfield(L, font_string, "__ow_layout_offset_y");
    NotifyFrameInputMutation(L, -1, false);
  }
  lua_pop(L, 1);
}

static int LuaSetButtonFontObject(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, "Button");
  const char *slot = lua_tostring(L, lua_upvalueindex(1));
  const char *usage = lua_tostring(L, lua_upvalueindex(2));
  bool valid = false;

  if (lua_isstring(L, 2) != 0) {
    valid = PushNamedFontObject(L, lua_tostring(L, 2));
  } else if (lua_istable(L, 2) != 0 &&
             lua_adapter::HasScriptObjectIdentity(L, 2) &&
             lua_adapter::HasCanonicalScriptObjectType(
                 L, 2, openwow::ui::widgets::ScriptObjectType::Font)) {
    lua_pushvalue(L, 2);
    valid = true;
  }

  if (!valid) {
    if (lua_gettop(L) > 2) {
      lua_pop(L, 1);
    }

    return luaL_error(L, usage, lua_adapter::ScriptObjectDisplayName(L, self));
  }

  lua_setfield(L, self, slot);
  RefreshButtonLabelFont(L, self);
  return 0;
}

static int LuaGetButtonFontObject(lua_State *L) {
  const int self = ValidateFrameObjectSelf(L, "Button");
  lua_getfield(L, self, lua_tostring(L, lua_upvalueindex(1)));
  return 1;
}

void InstallButtonFontObjectMethods(lua_State *L, const int button_index,
                                           const char *name,
                                           const char *slot,
                                           const char *setter_usage) {
  const int button = lua_absindex(L, button_index);
  const std::string setter = std::string("Set") + name;
  const std::string getter = std::string("Get") + name;
  lua_pushstring(L, slot);
  lua_pushstring(L, setter_usage);
  lua_pushcclosure(L, LuaSetButtonFontObject, 2);
  lua_setfield(L, button, setter.c_str());
  lua_pushstring(L, slot);
  lua_pushcclosure(L, LuaGetButtonFontObject, 1);
  lua_setfield(L, button, getter.c_str());
}

}
