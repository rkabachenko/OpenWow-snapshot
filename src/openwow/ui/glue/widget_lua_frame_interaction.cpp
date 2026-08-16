#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/game/localization.h"
#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/ui_coordinate_space.h"
#include "openwow/ui/font_layout.h"
#include "openwow/ui/font_string_layout.h"
#include "openwow/ui/frame_script_type_info.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/framexml/framexml_value_utils.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/glue/editbox_text_layout.h"
#include "openwow/ui/glue/glue_font_metrics.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/foundation/text/utf8.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "openwow/ui/glue/widget_lua_adapter_support.h"
#include "openwow/ui/glue/widget_lua_bindings.h"

namespace openwow::ui::glue::detail {

int LuaWidget_Lower(lua_State* state) {
  static_cast<void>(GetCheckedGlueFrameWidgetName(state));

  return 0;
}

int LuaWidget_SetToplevel(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const bool toplevel = ScriptReadBoolArgOrDefault(state, 2, true);
  SetGlueFrameBooleanField(state, 1, kGlueToplevelField, toplevel);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    runtime->SetToplevel(name, toplevel);
  }
  return 0;
}

int LuaWidget_SetMovable(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const bool movable = ScriptReadBoolArgOrDefault(state, 2, true);
  SetGlueFrameBooleanField(state, 1, kGlueMovableField, movable);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    runtime->SetMovable(name, movable);
  }
  return 0;
}

int LuaWidget_SetResizable(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const bool resizable = ScriptReadBoolArgOrDefault(state, 2, true);
  SetGlueFrameBooleanField(state, 1, kGlueResizableField, resizable);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    runtime->SetResizable(name, resizable);
  }
  return 0;
}

int LuaWidget_SetClampedToScreen(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const bool clamped = ScriptReadBoolArgOrDefault(state, 2, true);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetClampedToScreen(name, clamped);
  }
  lua_pushboolean(state, clamped ? 1 : 0);
  lua_setfield(state, 1, "__ow_clamped");
  return 0;
}

int LuaWidget_EnableMouse(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const bool enabled = ScriptReadBoolArgOrDefault(state, 2, true);
  SetGlueFrameBooleanField(state, 1, kGlueMouseEnabledField, enabled);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    runtime->SetMouseEnabled(name, enabled);
  }
  return 0;
}

int LuaWidget_EnableMouseWheel(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const bool enabled = ScriptReadBoolArgOrDefault(state, 2, true);
  SetGlueFrameBooleanField(state, 1, kGlueMouseWheelEnabledField, enabled);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    runtime->SetMouseWheelEnabled(name, enabled);
  }
  return 0;
}

int LuaWidget_IsMouseEnabled(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  bool enabled = GetGlueFrameBooleanField(state, 1, kGlueMouseEnabledField);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    enabled = runtime->IsMouseEnabled(name);
  }
  lua_pushwowbool(state, enabled);
  return 1;
}

int LuaWidget_IsMovable(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  bool movable = GetGlueFrameBooleanField(state, 1, kGlueMovableField);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    movable = runtime->IsMovable(name);
  }
  lua_pushwowbool(state, movable);
  return 1;
}

int LuaWidget_IsMouseWheelEnabled(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  bool enabled = GetGlueFrameBooleanField(state, 1, kGlueMouseWheelEnabledField);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    enabled = runtime->IsMouseWheelEnabled(name);
  }
  lua_pushwowbool(state, enabled);
  return 1;
}

int LuaWidget_IsKeyboardEnabled(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  bool enabled = GetGlueFrameBooleanField(state, 1, kGlueKeyboardEnabledField);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    enabled = runtime->IsKeyboardEnabled(name);
  }
  lua_pushwowbool(state, enabled);
  return 1;
}

int LuaWidget_EnableJoystick(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const bool enabled = ScriptReadBoolArgOrDefault(state, 2, true);
  SetGlueFrameBooleanField(state, 1, kGlueJoystickEnabledField, enabled);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    runtime->SetJoystickEnabled(name, enabled);
  }
  return 0;
}

int LuaWidget_IsJoystickEnabled(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  bool enabled = GetGlueFrameBooleanField(state, 1, kGlueJoystickEnabledField);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    enabled = runtime->IsJoystickEnabled(name);
  }
  lua_pushwowbool(state, enabled);
  return 1;
}

int LuaWidget_RegisterForDrag(lua_State* state) {
  const std::uint32_t mask = ParseRegisteredMouseButtonMask(state, 2);
  lua_pushinteger(state, static_cast<lua_Integer>(mask));
  lua_setfield(state, 1, kGlueRegisteredDragButtonMaskField);
  return 0;
}

int LuaWidget_StartSizing(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  bool resizable = GetGlueFrameBooleanField(state, 1, kGlueResizableField);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    resizable = runtime->IsResizable(name);
  }

  if (!resizable) {
    return luaL_error(state, "Frame %s is not resizable", name.c_str());
  }

  int move_sizing_mode = 8;
  if (lua_isstring(state, 2) != 0) {
    const char* point_name = lua_tostring(state, 2);
    int parsed_mode = move_sizing_mode;
    if (point_name != nullptr && openwow::ui::StringToFramePoint(point_name, &parsed_mode) != 0) {
      move_sizing_mode = parsed_mode;
    }
  }

  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    if (runtime->BeginWidgetSizing(name, move_sizing_mode)) {
      SetGlueFrameBooleanField(state, 1, kGlueUserPlacedField, true);
      if (auto* widget_runtime = GetWidgetRuntime(state);
          widget_runtime != nullptr && !IsUiParentName(name)) {
        widget_runtime->SetUserPlaced(name, true);
      }
    }
  }
  return 0;
}

int LuaWidget_StopMovingOrSizing(lua_State* state) {
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    (void)runtime->StopWidgetMoveSizing(GetCheckedGlueFrameWidgetName(state));
  }
  return 0;
}

int LuaWidget_SetUserPlaced(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  bool movable = GetGlueFrameBooleanField(state, 1, kGlueMovableField);
  bool resizable = GetGlueFrameBooleanField(state, 1, kGlueResizableField);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    movable = runtime->IsMovable(name);
    resizable = runtime->IsResizable(name);
  }

  if (!movable && !resizable) {
    return luaL_error(state, "Frame %s is not movable or resizable", name.c_str());
  }

  const bool user_placed = ScriptReadBoolArgOrDefault(state, 2, true);
  SetGlueFrameBooleanField(state, 1, kGlueUserPlacedField, user_placed);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    runtime->SetUserPlaced(name, user_placed);
  }
  return 0;
}

int LuaWidget_IsUserPlaced(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  bool user_placed = GetGlueFrameBooleanField(state, 1, kGlueUserPlacedField);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    user_placed = runtime->IsUserPlaced(name);
  }
  lua_pushwowbool(state, user_placed);
  return 1;
}

int LuaWidget_IsClampedToScreen(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  bool clamped = false;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    clamped = runtime->IsClampedToScreen(name);
  }
  if (clamped) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaWidget_SetDepth(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (lua_isnumber(state, 2) == 0) {
    const char* wname = name.empty() ? "<unnamed>" : name.c_str();
    return luaL_error(state, "Usage: %s:SetDepth(additiveDepth)", wname);
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetDepth(name, static_cast<float>(lua_tonumber(state, 2)));
  }
  return 0;
}

int LuaWidget_GetDepth(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  auto* runtime = GetWidgetRuntime(state);
  if (runtime != nullptr && runtime->IsIgnoringDepth(name)) {
    return 0;
  }
  const float depth = runtime != nullptr ? runtime->GetDepth(name) : 0.0F;
  lua_pushnumber(state, static_cast<lua_Number>(depth));
  return 1;
}

int LuaWidget_GetEffectiveDepth(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  auto* runtime = GetWidgetRuntime(state);
  if (runtime != nullptr && runtime->IsIgnoringDepth(name)) {
    return 0;
  }
  const double effective_depth =
      runtime != nullptr ? runtime->GetEffectiveDepth(name) : 0.0;
  lua_pushnumber(state, effective_depth);
  return 1;
}

int LuaWidget_IgnoreDepth(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (lua_type(state, 2) != LUA_TBOOLEAN) {
    const char* wname = name.empty() ? "<unnamed>" : name.c_str();
    return luaL_error(state, "Usage: %s:IgnoreDepth(ignore)", wname);
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetIgnoreDepth(name, lua_toboolean(state, 2) != 0);
  }
  return 0;
}

int LuaWidget_IsIgnoringDepth(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  auto* runtime = GetWidgetRuntime(state);
  const bool ignoring = runtime != nullptr ? runtime->IsIgnoringDepth(name) : false;
  if (ignoring) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaWidget_GetFrameStrata(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  std::string stored_strata;
  if (IsUiParentName(name)) {
    lua_getfield(state, 1, "__ow_frame_strata");
    const char* stored = lua_tostring(state, -1);
    if (stored != nullptr) {
      stored_strata = stored;
    }
    lua_pop(state, 1);
  } else if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value() && !widget->frame_strata.empty()) {
      stored_strata = widget->frame_strata;
    }
  }
  if (stored_strata.empty()) {
    lua_pushstring(state, "MEDIUM");
    return 1;
  }

  int strata_value = 0;
  if (openwow::ui::StringToScriptFrameStrata(stored_strata.c_str(), &strata_value) == 0) {
    lua_pushstring(state, "UNKNOWN");
    return 1;
  }

  lua_pushstring(state, openwow::ui::ScriptFrameStrataToString(strata_value));
  return 1;
}

int LuaWidget_HasScript(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (lua_isstring(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:HasScript(\"type\")", name.c_str());
  }

  if (ResolveGlueFrameScriptTypeInfo(state, 1, 2) != nullptr) {
    lua_pushnumber(state, 1);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaWidget_GetScript(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (lua_isstring(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:GetScript(\"type\")", name.c_str());
  }

  const auto* script_info = ResolveGlueFrameScriptTypeInfo(state, 1, 2);
  if (script_info == nullptr) {
    const char* script_name = lua_tostring(state, 2);
    return luaL_error(state, "%s doesn't have a \"%s\" script",
                      name.c_str(),
                      script_name != nullptr ? script_name : "");
  }

  lua_getfield(state, 1, "__ow_scripts");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    lua_pushnil(state);
    return 1;
  }

  lua_getfield(state, -1, script_info->canonical_name);
  lua_remove(state, -2);
  return 1;
}

int LuaWidget_HookScript(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (!lua_isstring(state, 2) || lua_type(state, 3) != LUA_TFUNCTION) {
    return luaL_error(state, "Usage: %s:HookScript(\"type\", function)",
                      name.c_str());
  }

  const auto* script_info = ResolveGlueFrameScriptTypeInfo(state, 1, 2);
  if (script_info == nullptr) {
    const char* script_name = lua_tostring(state, 2);
    return luaL_error(state, "%s doesn't have a \"%s\" script",
                      name.c_str(),
                      script_name != nullptr ? script_name : "");
  }

  lua_getfield(state, 1, "__ow_scripts");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, 1, "__ow_scripts");
  }

  lua_getfield(state, -1, script_info->canonical_name);
  if (lua_isfunction(state, -1) != 0) {
    lua_pushvalue(state, 3);
    openwow::ui::PushLuaCallOriginalThenHookClosure<
        openwow::ui::LuaPlainProtectedCall>(
        state, openwow::ui::kGlueLuaErrorHandlerRegistryKey);
    lua_setfield(state, -2, script_info->canonical_name);
  } else {
    lua_pop(state, 1);
    lua_pushvalue(state, 3);
    lua_setfield(state, -2, script_info->canonical_name);
  }

  lua_pop(state, 1);
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    runtime->InvalidateWidgetScriptCache(name, script_info->canonical_name);
  }
  return 0;
}

int LuaWidget_GetSize(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    if (IsUiParentName(name)) {
      const float rs = runtime->root_scale();
      lua_pushnumber(state, static_cast<double>(runtime->viewport_width()) / rs);
      lua_pushnumber(state, static_cast<double>(runtime->viewport_height()) / rs);
      return 2;
    }
    const bool use_explicit = ScriptReadBoolArgOrDefault(state, 2, false);
    const auto widget = runtime->GetWidget(name);
    lua_pushnumber(state, widget.has_value()
                              ? ResolveGlueWidgetScriptDimension(
                                    state, name, *widget, true, use_explicit)
                              : 0.0f);
    lua_pushnumber(state, widget.has_value()
                              ? ResolveGlueWidgetScriptDimension(
                                    state, name, *widget, false, use_explicit)
                              : 0.0f);
    return 2;
  }
  lua_pushnumber(state, 0);
  lua_pushnumber(state, 0);
  return 2;
}

int LuaWidget_GetEffectiveAlpha(lua_State* state) {
  const auto name = GetCheckedGlueWidgetName(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    lua_pushnumber(state, runtime->EffectiveAlpha(name));
    return 1;
  }
  lua_pushnumber(state, 1.0);
  return 1;
}

int LuaWidget_StopAnimating(lua_State* state) {
  static_cast<void>(GetCheckedGlueRegionWidgetName(state));
  openwow::ui::anim::StopRegionAnimationGroups(state, 1);
  return 0;
}

int LuaWidget_IsDragging(lua_State* state) {
  bool dragging = false;
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    dragging = runtime->IsWidgetMoveSizingActive(GetCheckedGlueFrameWidgetName(state));
  }
  lua_pushboolean(state, dragging ? 1 : 0);
  return 1;
}

int LuaWidget_IsMouseOver(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const auto widget = GetRectWidgetOrUiParent(state, name);
  const auto cursor = ResolveGlueCursorPositionPixels(state);

  if (!widget.has_value() || !cursor.has_value()) {
    lua_pushboolean(state, 0);
    return 1;
  }

  auto* const runtime = GetWidgetRuntime(state);
  const auto scale = openwow::ui::ResolveDevicePixelsPerUiUnit(
      runtime != nullptr ? static_cast<float>(runtime->viewport_height())
                         : 0.0F,
      runtime != nullptr ? runtime->GetEffectiveScale(name) : 1.0F);
  const bool is_mouse_over = openwow::ui::IsCursorInsideHitRect(
      openwow::ui::DevicePixelEdgeRect{
          .left = static_cast<float>(widget->x),
          .top = static_cast<float>(widget->y),
          .right = static_cast<float>(widget->x + widget->width),
          .bottom = static_cast<float>(widget->y + widget->height),
      },
      openwow::ui::DevicePixelPoint{cursor->first, cursor->second},
      openwow::ui::UiUnitHitInsets{
          .top = GetOptionalMouseOverInset(state, 2),
          .bottom = GetOptionalMouseOverInset(state, 3),
          .left = GetOptionalMouseOverInset(state, 4),
          .right = GetOptionalMouseOverInset(state, 5),
      },
      scale);

  lua_pushboolean(state, is_mouse_over ? 1 : 0);
  return 1;
}

int LuaWidget_GetAnimationGroups(lua_State* state) {
  return openwow::ui::anim::PushRegionAnimationGroups(state, 1);
}

int LuaWidget_CreateAnimationGroup(lua_State* state) {
  static_cast<void>(GetCheckedGlueRegionWidgetName(state));
  const char* name = luaL_optstring(state, 2, "");
  openwow::ui::anim::CreateAnimationGroupOnRegion(state, 1, name);
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    runtime->InvalidatePerFrameWidgetCache();
  }
  return 1;
}

static int PushOrCreateGlueTitleRegion(lua_State* state,
                                       int frame_index,
                                       const std::string& frame_name) {
  frame_index = lua_absindex(state, frame_index);

  lua_getfield(state, frame_index, kGlueTitleRegionField);
  if (lua_istable(state, -1) != 0) {
    return 1;
  }
  lua_pop(state, 1);

  const std::string title_region_name = frame_name + kGlueTitleRegionNameSuffix;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr &&
      !runtime->GetWidget(title_region_name).has_value()) {
    GlueWidgetState title_region{
        .name = title_region_name,
        .kind = "Region",
        .parent = frame_name,
        .frame_strata = "",
        .frame_level = 0,
        .alpha = 1.0F,
        .visible = true,
    };
    if (const auto parent_widget = runtime->GetWidget(frame_name); parent_widget.has_value()) {
      title_region.frame_strata = parent_widget->frame_strata;
      title_region.frame_level = parent_widget->frame_level;
    }
    runtime->RegisterWidget(title_region);
  }

  EnsureWidgetMethodTable(state);
  lua_newtable(state);
  lua_pushlstring(state, title_region_name.c_str(), title_region_name.size());
  lua_setfield(state, -2, "__ow_name");
  lua_pushliteral(state, "");
  lua_setfield(state, -2, "__ow_public_name");
  lua_pushstring(state, "Region");
  lua_setfield(state, -2, "__ow_type");
  lua_getfield(state, LUA_REGISTRYINDEX, "openwow.widget_methods");
  lua_setmetatable(state, -2);
  BindWidgetObjectTypeMethods(state, "Region");

  lua_pushvalue(state, -1);
  lua_setfield(state, frame_index, kGlueTitleRegionField);
  return 1;
}

int LuaWidget_GetTitleRegion(lua_State* state) {
  (void)GetCheckedGlueFrameWidgetName(state);
  lua_getfield(state, 1, kGlueTitleRegionField);
  if (lua_istable(state, -1) != 0) {
    return 1;
  }
  lua_pop(state, 1);
  lua_pushnil(state);
  return 1;
}

int LuaWidget_CreateTitleRegion(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  return PushOrCreateGlueTitleRegion(state, 1, name);
}

}
