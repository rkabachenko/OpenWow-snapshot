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
#include "openwow/ui/glue/widget_lua_adapter_support.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/foundation/text/utf8.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "openwow/ui/glue/widget_lua_adapter_support.h"
#include "openwow/ui/glue/widget_lua_bindings.h"

namespace openwow::ui::glue::detail {

int LuaWidget_RegisterForClicks(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  std::uint64_t mask = 0;
  for (int i = 2; lua_isstring(state, i) != 0; ++i) {
    std::string token = lua_tostring(state, i);
    const std::string lower = openwow::text::ToLowerAscii(token);
    bool is_down = false;
    std::string button_name;
    if (lower == "anydown") {
      mask |= 0xffffffffULL << 32u;
      continue;
    }
    if (lower == "anyup") {
      mask |= 0xffffffffULL;
      continue;
    }
    if (lower.size() > 4 && lower.ends_with("down")) {
      is_down = true;
      button_name = token.substr(0, token.size() - 4);
    } else if (lower.size() > 2 && lower.ends_with("up")) {
      button_name = token.substr(0, token.size() - 2);
    } else {
      continue;
    }
    const std::uint32_t button_flag =
        openwow::ui::widgets::MouseButtonFlag(button_name.c_str());
    if (button_flag != 0u) {
      mask |= is_down ? (static_cast<std::uint64_t>(button_flag) << 32u)
                      : static_cast<std::uint64_t>(button_flag);
    }
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->RegisterForClicks(name, mask);
  }
  return 0;
}

int LuaWidget_Click(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const char* button = luaL_optstring(state, 2, "LeftButton");
  const bool down = ScriptReadBoolArgOrDefault(state, 3, false);
  auto* widget_runtime = GetWidgetRuntime(state);
  if (widget_runtime != nullptr) {
    const auto widget = widget_runtime->GetWidget(name);
    if (!widget.has_value() || !widget->enabled ||
        openwow::text::EqualsIgnoreCaseAscii(widget_runtime->GetButtonState(name),
                                             "DISABLED")) {
      return 0;
    }
  }

  lua_getfield(state, 1, "__ow_click_in_progress");
  const bool click_in_progress = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  if (click_in_progress) {
    return 0;
  }
  lua_pushboolean(state, 1);
  lua_setfield(state, 1, "__ow_click_in_progress");

  if (widget_runtime != nullptr && !down) {
    const auto widget = widget_runtime->GetWidget(name);
    if (widget.has_value() &&
        openwow::text::EqualsIgnoreCaseAscii(widget->kind, "CheckButton")) {
      widget_runtime->SetChecked(name, !widget_runtime->Checked(name));
    }
  }

  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    GlueLuaValue arg_button;
    arg_button.kind = GlueLuaValue::Kind::kString;
    arg_button.string_value = button ? std::string(button) : std::string("LeftButton");
    GlueLuaValue arg_down;
    arg_down.kind = GlueLuaValue::Kind::kBoolean;
    arg_down.bool_value = down;
    const std::vector<GlueLuaValue> args{arg_button, arg_down};
    (void)runtime->RunWidgetEvent(name, "PreClick", name + ".PreClick", args);
    (void)runtime->RunWidgetEvent(name, "OnClick", name + ".Click", args);
    (void)runtime->RunWidgetEvent(name, "PostClick", name + ".PostClick", args);
  }
  lua_pushboolean(state, 0);
  lua_setfield(state, 1, "__ow_click_in_progress");
  return 0;
}
static std::string GetCheckedGlueButtonWidgetName(lua_State* state) {
  const auto name = GetCheckedGlueWidgetName(state);
  auto* runtime = GetWidgetRuntime(state);
  const auto owner = runtime != nullptr ? runtime->GetWidget(name)
                                        : std::optional<GlueWidgetState>{};
  if (!owner.has_value() ||
      (!openwow::text::EqualsIgnoreCaseAscii(owner->kind, "Button") &&
       !openwow::text::EqualsIgnoreCaseAscii(owner->kind, "CheckButton"))) {
    luaL_error(state, "Wrong object type for member function");
  }
  return name;
}

int LuaWidget_GetFontString(lua_State* state) {
  const auto name = GetCheckedGlueButtonWidgetName(state);
  auto* runtime = GetWidgetRuntime(state);
  const std::string region = runtime->TextRegionForWidget(name);
  if (region.empty() || !PushGlueWidgetGlobalTable(state, region)) {
    lua_pushnil(state);
  }
  return 1;
}

static int PushGlueButtonTexture(
    lua_State* state,
    std::initializer_list<const char*> runtime_key_suffixes) {
  const auto name = GetCheckedGlueButtonWidgetName(state);
  auto* runtime = GetWidgetRuntime(state);
  const std::string region =
      FindFirstExistingWidgetName(runtime, name, runtime_key_suffixes);
  if (region.empty() || !PushGlueWidgetGlobalTable(state, region)) {
    lua_pushnil(state);
  }
  return 1;
}

int LuaWidget_GetNormalTexture(lua_State* state) {
  return PushGlueButtonTexture(
      state, {"NormalTexture", "Normal", "UpTexture"});
}

int LuaWidget_GetHighlightTexture(lua_State* state) {
  return PushGlueButtonTexture(state, {"HighlightTexture", "Highlight"});
}

int LuaWidget_GetPushedTexture(lua_State* state) {
  return PushGlueButtonTexture(
      state, {"PushedTexture", "Pushed", "DownTexture"});
}

int LuaWidget_GetDisabledTexture(lua_State* state) {
  return PushGlueButtonTexture(state, {"DisabledTexture", "Disabled"});
}

int LuaWidget_SetNormalTexture(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const char* file = luaL_optstring(state, 2, "");
  auto* runtime = GetWidgetRuntime(state);
  const std::string target =
      FindFirstExistingWidgetName(runtime, name, {"NormalTexture", "Normal", "UpTexture"});
  if (!target.empty() && runtime != nullptr) {
    runtime->SetTexture(target, file ? std::string(file) : std::string());
  }
  return 0;
}

int LuaWidget_SetPushedTexture(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const char* file = luaL_optstring(state, 2, "");
  auto* runtime = GetWidgetRuntime(state);
  const std::string target =
      FindFirstExistingWidgetName(runtime, name, {"PushedTexture", "Pushed", "DownTexture"});
  if (!target.empty() && runtime != nullptr) {
    runtime->SetTexture(target, file ? std::string(file) : std::string());
  }
  return 0;
}

int LuaWidget_SetHighlightTexture(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const char* file = luaL_optstring(state, 2, "");
  auto* runtime = GetWidgetRuntime(state);
  const std::string target =
      FindFirstExistingWidgetName(runtime, name, {"HighlightTexture", "Highlight"});
  if (!target.empty() && runtime != nullptr) {
    runtime->SetTexture(target, file ? std::string(file) : std::string());
  }
  return 0;
}

int LuaWidget_SetEnabled(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const bool enabled = lua_toboolean(state, 2) != 0;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetEnabled(name, enabled);
  }
  return 0;
}

int LuaWidget_Enable(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetEnabled(name, true);
  }
  return 0;
}

int LuaWidget_Disable(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetEnabled(name, false);
  }
  return 0;
}

int LuaWidget_IsEnabled(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  bool enabled = false;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    enabled = runtime->IsEnabled(name);
  }
  lua_pushboolean(state, enabled ? 1 : 0);
  return 1;
}

int LuaWidget_GetMinMaxValues(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    if (runtime->IsStatusBar(name)) {
      const auto status_bar = runtime->GetStatusBarValueSnapshot(name);
      lua_pushnumber(state, status_bar.minimum);
      lua_pushnumber(state, status_bar.maximum);
      return 2;
    }
    const auto [min_v, max_v] = runtime->GetMinMaxValues(name);
    lua_pushnumber(state, min_v);
    lua_pushnumber(state, max_v);
    return 2;
  }
  lua_pushnumber(state, 0);
  lua_pushnumber(state, 0);
  return 2;
}

int LuaWidget_SetMinMaxValues(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    if (runtime->IsStatusBar(name)) {
      if (lua_isnumber(state, 2) == 0 || lua_isnumber(state, 3) == 0) {
        return luaL_error(state, "Usage: %s:SetMinMaxValues(min, max)",
                          name.c_str());
      }
      const float minimum = static_cast<float>(lua_tonumber(state, 2));
      const float maximum = static_cast<float>(lua_tonumber(state, 3));
      switch (openwow::ui::widgets::ValidateStatusBarRange(minimum, maximum)) {
        case openwow::ui::widgets::StatusBarRangeError::EndpointOutOfRange:
          return luaL_error(state, "Min or Max out of range");
        case openwow::ui::widgets::StatusBarRangeError::SpanTooLarge:
          return luaL_error(state, "Min and Max too far apart");
        case openwow::ui::widgets::StatusBarRangeError::None:
          break;
      }
      const auto change =
          runtime->SetStatusBarRange(name, minimum, maximum);
      if (!change.range_changed) {
        return 0;
      }
      auto* glue_runtime = GetGlueRuntime(state);
      if (glue_runtime != nullptr) {
        (void)glue_runtime->RunWidgetEvent(
            name, "OnMinMaxChanged", name + ".OnMinMaxChanged",
            {MakeLuaNumber(minimum), MakeLuaNumber(maximum)});
      }
      if (change.reapply_value) {
        const float current_value =
            runtime->GetStatusBarValueSnapshot(name).value;
        if (runtime->SetStatusBarValue(name, current_value) &&
            glue_runtime != nullptr) {
          (void)glue_runtime->RunWidgetEvent(
              name, "OnValueChanged", name + ".OnValueChanged",
              {MakeLuaNumber(
                  runtime->GetStatusBarValueSnapshot(name).value)});
        }
      }
      return 0;
    }

    const double min_v = luaL_optnumber(state, 2, 0.0);
    const double max_v = luaL_optnumber(state, 3, 0.0);
    const bool range_was_set = runtime->HasSliderRange(name);
    const auto [old_minimum, old_maximum] = runtime->GetMinMaxValues(name);
    const bool range_changed =
        !range_was_set ||
        std::fabs(old_minimum - min_v) >= kSimpleWidgetWriteEpsilon ||
        std::fabs(old_maximum - max_v) >= kSimpleWidgetWriteEpsilon;
    if (!range_changed) {
      return 0;
    }

    const bool value_was_set = runtime->HasSliderValue(name);
    const double previous_value = runtime->GetValue(name);

    runtime->SetMinMaxValues(name, min_v, max_v,
                             false);
    auto* glue_rt = GetGlueRuntime(state);
    if (glue_rt != nullptr) {
      (void)glue_rt->RunWidgetEvent(
          name, "OnMinMaxChanged", name + ".OnMinMaxChanged",
          {MakeLuaNumber(min_v), MakeLuaNumber(max_v)});
    }
    if (value_was_set) {
      runtime->SetValue(name, previous_value);
      const double next_value = runtime->GetValue(name);
      if (glue_rt != nullptr &&
          std::fabs(next_value - previous_value) >=
              kSimpleWidgetWriteEpsilon) {
        (void)glue_rt->RunWidgetEvent(
            name, "OnValueChanged", name + ".OnValueChanged",
            {MakeLuaNumber(next_value)});
      }
    }
  }
  return 0;
}

int LuaWidget_GetValue(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    if (runtime->IsStatusBar(name)) {
      lua_pushnumber(state, runtime->GetStatusBarValueSnapshot(name).value);
      return 1;
    }
    lua_pushnumber(state, runtime->GetValue(name));
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_SetValue(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    if (runtime->IsStatusBar(name)) {
      if (lua_gettop(state) != 2 || lua_isnumber(state, 2) == 0) {
        return luaL_error(state, "Usage: %s:SetValue(value)", name.c_str());
      }
      if (runtime->SetStatusBarValue(
              name, static_cast<float>(lua_tonumber(state, 2)))) {
        const float value = runtime->GetStatusBarValueSnapshot(name).value;
        if (auto* glue_runtime = GetGlueRuntime(state);
            glue_runtime != nullptr) {
          (void)glue_runtime->RunWidgetEvent(
              name, "OnValueChanged", name + ".OnValueChanged",
              {MakeLuaNumber(value)});
        }
      }
      return 0;
    }

    const double value = luaL_optnumber(state, 2, 0.0);
    const double old_value = runtime->GetValue(name);
    runtime->SetValue(name, value);
    const double new_value = runtime->GetValue(name);

    if (new_value != old_value) {
      if (auto* glue_rt = GetGlueRuntime(state); glue_rt != nullptr) {
        (void)glue_rt->RunWidgetEvent(name, "OnValueChanged",
                                      name + ".OnValueChanged",
                                      {MakeLuaNumber(new_value)});
      }
    }
  }
  return 0;
}

int LuaWidget_GetValueStep(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    lua_pushnumber(state, runtime->GetValueStep(name));
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_SetValueStep(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const double step = luaL_optnumber(state, 2, 0.0);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const bool value_was_set = runtime->HasSliderValue(name);
    const double previous_value = runtime->GetValue(name);
    runtime->SetValueStep(name, step);
    const double next_value = runtime->GetValue(name);
    if (value_was_set &&
        std::fabs(next_value - previous_value) >=
            kSimpleWidgetWriteEpsilon) {
      if (auto* glue_rt = GetGlueRuntime(state); glue_rt != nullptr) {
        (void)glue_rt->RunWidgetEvent(
            name, "OnValueChanged", name + ".OnValueChanged",
            {MakeLuaNumber(next_value)});
      }
    }
  }
  return 0;
}
int LuaWidget_LockHighlight(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->LockHighlight(name, true);
  }
  return 0;
}

int LuaWidget_UnlockHighlight(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->LockHighlight(name, false);
  }
  return 0;
}

int LuaWidget_GetButtonState(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    lua_pushstring(state, runtime->GetButtonState(name).c_str());
    return 1;
  }
  lua_pushstring(state, "NORMAL");
  return 1;
}

int LuaWidget_SetButtonState(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const char* new_state = luaL_optstring(state, 2, "");
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetButtonState(name, new_state ? std::string(new_state) : std::string());
  }
  return 0;
}

int LuaWidget_SetDesaturated(lua_State* state) {

  const auto name = WidgetNameFromArg(state, 1);
  const bool desaturated = ScriptReadBoolArgOrDefault(state, 2, true);
  const bool supported = TextureStateSupported(desaturated);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetDesaturated(name, supported && desaturated);
  }
  if (supported) {
    lua_pushnumber(state, 1);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaWidget_SetDisabledTextColor(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetDisabledTextColor(
        name,
        {.r = NormalizePackedColorComponent(
             GetScriptColorArgumentOrDefault(state, 2, 0.0F)),
         .g = NormalizePackedColorComponent(
             GetScriptColorArgumentOrDefault(state, 3, 0.0F)),
         .b = NormalizePackedColorComponent(
             GetScriptColorArgumentOrDefault(state, 4, 0.0F)),
         .a = NormalizePackedColorComponent(
             GetScriptColorArgumentOrDefault(state, 5, 1.0F))});
  }
  return 0;
}

int LuaWidget_AddLine(lua_State* state) {

  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const char* text = luaL_optstring(state, 2, "");
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && text != nullptr) {
    const std::string existing = runtime->GetText(name);
    if (existing.empty()) {
      runtime->SetText(name, std::string(text));
    } else {
      runtime->SetText(name, existing + "\n" + std::string(text));
    }
  }
  return 0;
}

int LuaWidget_Clear(lua_State* state) {

  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetText(name, "");
  }
  return 0;
}
int LuaWidget_SetChecked(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty() || lua_istable(state, 1) == 0) {
    return 0;
  }

  const bool checked = ScriptReadBoolArgOrDefault(state, 2, true);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetChecked(name, checked);
  }
  lua_pushboolean(state, checked ? 1 : 0);
  lua_setfield(state, 1, "__ow_checked");
  return 0;
}

int LuaWidget_GetChecked(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (lua_istable(state, 1) == 0) {
    lua_pushboolean(state, 0);
    return 1;
  }
  if (!name.empty()) {
    if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
      const bool checked = runtime->Checked(name);
      lua_pushboolean(state, checked ? 1 : 0);
      lua_setfield(state, 1, "__ow_checked");
      lua_pushboolean(state, checked ? 1 : 0);
      return 1;
    }
  }
  lua_getfield(state, 1, "__ow_checked");
  const bool checked = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  lua_pushboolean(state, checked ? 1 : 0);
  return 1;
}

}
