#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/game/localization.h"
#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/ui_aspect_scales.h"
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

int LuaWidget_Insert(lua_State* state) {
  const auto name = GetCheckedGlueEditBoxWidgetName(state);
  if (lua_isstring(state, 2) == 0) {
    return 0;
  }

  const char* raw_text = lua_tostring(state, 2);
  const std::string inserted_text = raw_text != nullptr ? raw_text : "";
  auto* widget_runtime = GetWidgetRuntime(state);
  const auto insertion =
      widget_runtime->BuildEditBoxInsertion(name, inserted_text);
  if (!insertion.accepted) {
    return 0;
  }

  widget_runtime->SetText(name, insertion.text);
  widget_runtime->SetEditCursorByte(name, insertion.cursor_byte);
  widget_runtime->ClearEditSelection(name);

  if (auto* glue_runtime = GetGlueRuntime(state); glue_runtime != nullptr) {
    (void)glue_runtime->RunWidgetEvent(
        name, "OnChar", name + ".OnChar",
        {MakeLuaString(inserted_text)});
    if (inserted_text.find(' ') != std::string::npos) {
      (void)glue_runtime->RunWidgetEvent(
          name, "OnSpacePressed", name + ".OnSpacePressed", {});
    }
    (void)glue_runtime->RunWidgetEvent(
        name, "OnTextChanged", name + ".OnTextChanged",
        {MakeLuaBool(true)});
  }
  return 0;
}
int LuaWidget_SetMaxBytes(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (lua_gettop(state) != 2 || !lua_isnumber(state, 2)) {
    return luaL_error(state, "Usage: %s:SetMaxBytes(max)", name.c_str());
  }
  const int max_bytes = static_cast<int>(lua_tointeger(state, 2));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetMaxBytes(name, max_bytes);
  }
  return 0;
}

int LuaWidget_SetMaxLetters(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (lua_gettop(state) != 2 || !lua_isnumber(state, 2)) {
    return luaL_error(state, "Usage: %s:SetMaxLetters(max)", name.c_str());
  }
  const int max_letters = static_cast<int>(lua_tointeger(state, 2));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetMaxLetters(name, max_letters);
  }
  return 0;
}

int LuaWidget_GetInputLanguage(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const std::string token = runtime->GetEditInputLanguageToken(name);
    lua_pushlstring(state, token.data(), token.size());
    return 1;
  }

  lua_pushstring(state, "ROMAN");
  return 1;
}

int LuaWidget_GetVerticalScroll(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    lua_pushnumber(state, runtime->GetVerticalScroll(name));
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_SetVerticalScroll(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (!lua_isnumber(state, 2)) {
    const char* frame_name = name.empty() ? "<unnamed>" : name.c_str();
    return luaL_error(state, "Usage: %s:SetVerticalScroll(offset)", frame_name);
  }
  const double offset = lua_tonumber(state, 2);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const double previous = runtime->GetVerticalScroll(name);
    runtime->SetVerticalScroll(name, offset);
    const double current = runtime->GetVerticalScroll(name);
    if (std::fabs(current - previous) >= kSimpleWidgetWriteEpsilon) {
      if (auto* glue_rt = GetGlueRuntime(state); glue_rt != nullptr) {
        (void)glue_rt->RunWidgetEvent(
            name, "OnVerticalScroll", name + ".OnVerticalScroll",
            {MakeLuaNumber(current)});
      }
    }
  }
  return 0;
}

int LuaWidget_GetVerticalScrollRange(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    lua_pushnumber(state, runtime->GetVerticalScrollRange(name));
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_GetHorizontalScroll(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !name.empty()) {
    lua_pushnumber(state, runtime->GetHorizontalScroll(name));
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_SetHorizontalScroll(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (!lua_isnumber(state, 2)) {
    const char* frame_name = name.empty() ? "<unnamed>" : name.c_str();
    return luaL_error(state, "Usage: %s:SetHorizontalScroll(offset)",
                      frame_name);
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const double previous = runtime->GetHorizontalScroll(name);
    runtime->SetHorizontalScroll(name, lua_tonumber(state, 2));
    const double current = runtime->GetHorizontalScroll(name);
    if (std::fabs(current - previous) >= kSimpleWidgetWriteEpsilon) {
      if (auto* glue_rt = GetGlueRuntime(state); glue_rt != nullptr) {
        (void)glue_rt->RunWidgetEvent(
            name, "OnHorizontalScroll", name + ".OnHorizontalScroll",
            {MakeLuaNumber(current)});
      }
    }
  }
  return 0;
}

int LuaWidget_GetHorizontalScrollRange(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !name.empty()) {
    lua_pushnumber(state, runtime->GetHorizontalScrollRange(name));
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}
int LuaWidget_EnableKeyboard(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const bool enable = ScriptReadBoolArgOrDefault(state, 2, true);
  SetGlueFrameBooleanField(state, 1, kGlueKeyboardEnabledField, enable);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    runtime->SetKeyboardEnabled(name, enable);
  }
  return 0;
}

int LuaWidget_SetAutoFocus(lua_State* state) {
  const auto name = GetCheckedGlueEditBoxWidgetName(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetEditAutoFocus(
        name, ScriptReadBoolArgOrDefault(state, 2, true));
  }
  return 0;
}

int LuaWidget_IsAutoFocus(lua_State* state) {
  const auto name = GetCheckedGlueEditBoxWidgetName(state);
  const auto* runtime = GetWidgetRuntime(state);
  lua_pushboolean(state,
                  runtime != nullptr && runtime->IsEditAutoFocus(name) ? 1 : 0);
  return 1;
}

int LuaWidget_SetFocus(lua_State* state) {
  const auto name = GetCheckedGlueEditBoxWidgetName(state);
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    (void)runtime->SetEditBoxFocus(name);
  } else if (auto* widgets = GetWidgetRuntime(state);
             widgets != nullptr && widgets->CanFocusEditBox(name)) {
    widgets->SetFocusedWidget(name);
  }
  return 0;
}

int LuaWidget_ClearFocus(lua_State* state) {
  const auto name = GetCheckedGlueEditBoxWidgetName(state);
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    (void)runtime->ClearEditBoxFocus(name);
  } else if (auto* widgets = GetWidgetRuntime(state);
             widgets != nullptr && widgets->focused_widget() == name) {
    widgets->SetFocusedWidget({});
  }
  return 0;
}

int LuaWidget_HasFocus(lua_State* state) {
  const auto name = GetCheckedGlueEditBoxWidgetName(state);
  const auto* widgets = GetWidgetRuntime(state);
  lua_pushboolean(
      state,
      widgets != nullptr && widgets->focused_widget() == name ? 1 : 0);
  return 1;
}

int LuaWidget_HighlightText(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty() || lua_istable(state, 1) == 0) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const std::string text = runtime->GetText(name);
    const int max_pos = static_cast<int>(text.size());

    if (lua_gettop(state) < 2) {

      runtime->SetEditSelectionBytes(name, 0, max_pos);
      return 0;
    }

    const int start = static_cast<int>(luaL_optinteger(state, 2, 0));
    const int end = static_cast<int>(luaL_optinteger(state, 3, start));
    if (start == 0 && end == 0) {
      runtime->ClearEditSelection(name);
      return 0;
    }
    runtime->SetEditSelectionBytes(name, start, end);
  }
  return 0;
}

}
