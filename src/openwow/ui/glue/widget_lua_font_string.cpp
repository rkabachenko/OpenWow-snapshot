#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/game/localization.h"
#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/resources/fonts/text_alpha_gradient.h"
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

int LuaWidget_SetJustifyH(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  uint32_t flags = 0;
  const char* value = (lua_isstring(state, 2) != 0) ? lua_tostring(state, 2) : nullptr;
  if (openwow::ui::JustifyStringToFlags(value, &flags) == 0) {
    return luaL_error(state, "Usage: %s:SetJustifyH(\"justify\")",
                      widget.name.empty() ? "<unnamed>" : widget.name.c_str());
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetJustifyH(widget.name, openwow::ui::JustifyFlagsToString(flags));
  }
  return 0;
}

int LuaWidget_SetJustifyV(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  uint32_t flags = 0;
  const char* value = (lua_isstring(state, 2) != 0) ? lua_tostring(state, 2) : nullptr;
  if (openwow::ui::JustifyStringToFlags(value, &flags) == 0) {
    return luaL_error(state, "Usage: %s:SetJustifyV(\"justify\")",
                      widget.name.empty() ? "<unnamed>" : widget.name.c_str());
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetJustifyV(widget.name, openwow::ui::JustifyFlagsToString(flags));
  }
  return 0;
}

std::string ReadFontObjectStyleArgument(lua_State* state, int arg) {
  if (lua_isstring(state, arg) != 0) {
    const char* value = lua_tostring(state, arg);
    return value != nullptr ? std::string(value) : std::string();
  }
  if (lua_istable(state, arg) != 0) {
    lua_getfield(state, arg, "__ow_name");
    const char* value = lua_tostring(state, -1);
    std::string style = value != nullptr ? std::string(value) : std::string();
    lua_pop(state, 1);
    return style;
  }
  return {};
}

int LuaWidget_SetNormalFontObject(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetButtonFontStyle(name, GlueButtonFontState::kNormal,
                                ReadFontObjectStyleArgument(state, 2));
  }
  return 0;
}

int LuaWidget_SetHighlightFontObject(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetButtonFontStyle(name, GlueButtonFontState::kHighlight,
                                ReadFontObjectStyleArgument(state, 2));
  }
  return 0;
}

int LuaWidget_SetDisabledFontObject(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetButtonFontStyle(name, GlueButtonFontState::kDisabled,
                                ReadFontObjectStyleArgument(state, 2));
  }
  return 0;
}

int LuaWidget_SetFontObject(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  const std::string& name = widget.name;
  std::string style;
  const int argument_type = lua_type(state, 2);
  if (argument_type == LUA_TTABLE) {
    lua_getfield(state, 2, "__ow_name");
    const char* font_name = lua_tostring(state, -1);
    if (font_name == nullptr) {
      lua_pop(state, 1);
      return luaL_error(
          state, "%s:SetFontObject(): Couldn't find 'this' in font object",
          name.empty() ? "<unnamed>" : name.c_str());
    }
    style = font_name;
    lua_pop(state, 1);

    const bool registered =
        openwow::ui::game::frame_api::PushNamedFontObject(state, style.c_str());
    const bool same_object =
        registered && lua_rawequal(state, 2, -1) != 0;
    lua_pop(state, 1);
    if (!same_object) {
      return luaL_error(
          state, "%s:SetFontObject(): Wrong object type, expected font",
          name.empty() ? "<unnamed>" : name.c_str());
    }
  } else if (argument_type == LUA_TSTRING) {
    const char* font_name = lua_tostring(state, 2);
    style = font_name != nullptr ? font_name : "";
    const bool registered =
        openwow::ui::game::frame_api::PushNamedFontObject(state, style.c_str());
    lua_pop(state, 1);
    if (!registered) {
      return luaL_error(
          state, "%s:SetFontObject(): Couldn't find font named %s",
          name.empty() ? "<unnamed>" : name.c_str(), style.c_str());
    }
  } else if (argument_type == LUA_TNONE || argument_type == LUA_TNIL) {
    style.clear();
  } else {
    return luaL_error(
        state, "Usage: %s:SetFontObject(font or \"font\" or nil)",
        name.empty() ? "<unnamed>" : name.c_str());
  }

  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetFontStyle(name, style);
  }
  return 0;
}

int LuaWidget_GetFontObject(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  if (!widget.font_style.empty()) {
    if (openwow::ui::game::frame_api::PushNamedFontObject(
            state, widget.font_style.c_str())) {
      return 1;
    }
    lua_pop(state, 1);
  }
  lua_pushnil(state);
  return 1;
}

GlueWidgetState GetCheckedFontStringWidget(lua_State* state) {
  if (lua_istable(state, 1) == 0) {
    luaL_error(state,
               "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }

  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }

  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }

  const auto widget = runtime->GetWidget(name);
  if (!widget.has_value()) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }
  if (!EqualsIgnoreCaseAscii(widget->kind.c_str(), "FontString")) {
    luaL_error(state, "Wrong object type for member function");
  }
  return *widget;
}

static GlueWidgetState GetCheckedFontStringWidgetWithFont(lua_State* state,
                                                          const char* method_name) {
  const auto widget = GetCheckedFontStringWidget(state);
  if (widget.font_style.empty()) {
    luaL_error(state, "%s:%s(): Font not set",
               widget.name.empty() ? "<unnamed>" : widget.name.c_str(),
               method_name);
  }
  return widget;
}

static bool GlueWidgetSupportsSetText(const GlueWidgetState& widget) {
  const auto kind = openwow::text::ToLowerAscii(widget.kind);
  return kind == "fontstring" ||
         kind == "button" ||
         kind == "checkbutton" ||
         kind == "editbox" ||
         kind == "simplehtml" ||
         kind == "messageframe" ||
         kind == "scrollingmessageframe" ||
         kind == "gametooltip";
}

static GlueWidgetState GetCheckedSetTextWidget(lua_State* state) {
  const auto name = GetCheckedGlueWidgetName(state);
  if (IsUiParentName(name)) {
    luaL_error(state, "Wrong object type for member function");
  }

  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }

  const auto widget = runtime->GetWidget(name);
  if (!widget.has_value()) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }
  if (!GlueWidgetSupportsSetText(*widget)) {
    luaL_error(state, "Wrong object type for member function");
  }
  if (EqualsIgnoreCaseAscii(widget->kind.c_str(), "FontString") &&
      widget->font_style.empty()) {
    luaL_error(state, "%s:SetText(): Font not set",
               widget->name.empty() ? "<unnamed>" : widget->name.c_str());
  }
  return *widget;
}

std::string GetUsageWidgetName(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  return name.empty() ? std::string("<unnamed>") : name;
}

static bool FontStringMatchesObjectType(const char* type_name) {
  return EqualsIgnoreCaseAscii(type_name, "FontString") ||
         EqualsIgnoreCaseAscii(type_name, "Region") ||
         EqualsIgnoreCaseAscii(type_name, "Object");
}

int LuaWidget_FontStringGetObjectType(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  lua_pushstring(state, widget.kind.c_str());
  return 1;
}

int LuaWidget_SetText(lua_State* state) {
  const auto widget = GetCheckedSetTextWidget(state);
  const char* text = lua_tostring(state, 2);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const std::string next = ExpandSimpleRenderScriptText(text);
    if (openwow::text::EqualsIgnoreCaseAscii(widget.kind, "EditBox")) {
      if (auto* glue_runtime = GetGlueRuntime(state); glue_runtime != nullptr) {
        (void)glue_runtime->SetEditBoxTextProgrammatically(widget.name, next);
      } else {
        runtime->SetText(widget.name, next);
      }
    } else {
      runtime->SetText(widget.name, next);
    }
  }
  return 0;
}

int LuaWidget_SetFormattedText(lua_State* state) {
  const auto widget = GetCheckedFontStringWidgetWithFont(state, "SetFormattedText");

  const int top = lua_gettop(state);
  const char* fmt = luaL_optstring(state, 2, "");
  if (fmt == nullptr) {
    return 0;
  }

  lua_getglobal(state, "string");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return 0;
  }
  lua_getfield(state, -1, "format");
  lua_remove(state, -2);
  if (lua_isfunction(state, -1) == 0) {
    lua_pop(state, 1);
    return 0;
  }

  lua_pushstring(state, fmt);
  for (int i = 3; i <= top; ++i) {
    lua_pushvalue(state, i);
  }
  const int nargs = (top >= 2) ? 1 + (top - 2) : 1;
  if (lua_pcall(state, nargs, 1, 0) != 0) {
    lua_pop(state, 1);
    return 0;
  }

  const char* out = lua_tostring(state, -1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && out != nullptr) {
    runtime->SetText(widget.name, ExpandSimpleRenderScriptText(out));
  }
  lua_pop(state, 1);
  return 0;
}
int LuaWidget_GetTextColor(lua_State* state) {

  const auto widget = GetCheckedFontStringWidget(state);
  lua_pushnumber(state, NormalizePackedColorComponent(widget.color_r));
  lua_pushnumber(state, NormalizePackedColorComponent(widget.color_g));
  lua_pushnumber(state, NormalizePackedColorComponent(widget.color_b));
  lua_pushnumber(state, NormalizePackedColorComponent(widget.color_a));
  return 4;
}

int LuaWidget_SetTextColor(lua_State* state) {

  const auto widget = GetCheckedFontStringWidget(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetVertexColor(
        widget.name,
        NormalizePackedColorComponent(GetScriptColorArgumentOrDefault(state, 2, 0.0F)),
        NormalizePackedColorComponent(GetScriptColorArgumentOrDefault(state, 3, 0.0F)),
        NormalizePackedColorComponent(GetScriptColorArgumentOrDefault(state, 4, 0.0F)),
        NormalizePackedColorComponent(GetScriptColorArgumentOrDefault(state, 5, 1.0F)));
  }
  return 0;
}

int LuaWidget_GetText(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const auto text = runtime->GetText(name);
    if (text.empty()) {
      const auto widget = runtime->GetWidget(name);

      if (widget.has_value() &&
          openwow::text::EqualsIgnoreCaseAscii(widget->kind, "EditBox")) {
        lua_pushliteral(state, "");
      } else {
        lua_pushnil(state);
      }
    } else {
      lua_pushstring(state, text.c_str());
    }
    return 1;
  }
  lua_pushnil(state);
  return 1;
}

int LuaWidget_GetTextWidth(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value() && widget->width > 0) {
      lua_pushnumber(state, widget->width);
      return 1;
    }
    const auto text = runtime->GetText(name);
    lua_pushnumber(state, static_cast<double>(text.size()) * 8.0);
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_GetStringWidth(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* widget_runtime = GetWidgetRuntime(state);
      widget_runtime != nullptr && !name.empty()) {
    const auto widget = widget_runtime->GetWidget(name);
    if (widget.has_value()) {
      if (const auto measurement =
              MeasureGlueFontString(GetGlueRuntime(state), widget_runtime, *widget);
          measurement.has_value()) {
        lua_pushnumber(state, measurement->width);
        return 1;
      }
    }
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_IsObjectType(lua_State* state) {
  (void)GetCheckedFontStringWidget(state);
  if (lua_isstring(state, 2) == 0) {
    const auto usage_name = GetUsageWidgetName(state);
    return luaL_error(state, "Usage: %s:IsObjectType(\"TYPE\")", usage_name.c_str());
  }
  const char* typeName = lua_tostring(state, 2);
  if (FontStringMatchesObjectType(typeName)) {
    lua_pushnumber(state, 1);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaWidget_GetFont(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value()) {
      const auto resolved_style = ResolveGlueFontStringStyle(
          GetGlueRuntime(state) != nullptr
              ? GetGlueRuntime(state)->font_registry()
              : nullptr,
          *widget);
      if (resolved_style.has_bound_font &&
          !resolved_style.font.font_file.empty()) {
        lua_pushstring(state, resolved_style.font.font_file.c_str());
      } else {
        lua_pushnil(state);
      }

      if (widget->text_height_stored > 0.0f) {
        lua_pushnumber(state, openwow::ui::StoredUiHorizontalCoordinateToPixels(
                                  widget->text_height_stored));
      } else {
        lua_pushnumber(
            state,
            resolved_style.has_bound_font
                ? static_cast<lua_Number>(resolved_style.font.height_px)
                : 0.0);
      }
      lua_pushstring(state, "");
      return 3;
    }
  }
  lua_pushnil(state);
  lua_pushnumber(state, 0.0);
  lua_pushstring(state, "");
  return 3;
}

int LuaWidget_GetDrawLayer(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value() && !widget->draw_layer.empty()) {
      lua_pushstring(state, widget->draw_layer.c_str());
      return 1;
    }
  }
  lua_pushstring(state, "ARTWORK");
  return 1;
}

int LuaWidget_SetDrawLayer(lua_State* ) {

  return 0;
}

int LuaWidget_GetShadowColor(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  lua_pushnumber(state, NormalizePackedColorComponent(widget.shadow_r));
  lua_pushnumber(state, NormalizePackedColorComponent(widget.shadow_g));
  lua_pushnumber(state, NormalizePackedColorComponent(widget.shadow_b));
  lua_pushnumber(state, NormalizePackedColorComponent(widget.shadow_a));
  return 4;
}

int LuaWidget_SetShadowColor(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetShadowColor(
        widget.name,
        NormalizePackedColorComponent(GetScriptColorArgumentOrDefault(state, 2, 0.0F)),
        NormalizePackedColorComponent(GetScriptColorArgumentOrDefault(state, 3, 0.0F)),
        NormalizePackedColorComponent(GetScriptColorArgumentOrDefault(state, 4, 0.0F)),
        NormalizePackedColorComponent(GetScriptColorArgumentOrDefault(state, 5, 1.0F)));
  }
  return 0;
}

int LuaWidget_GetShadowOffset(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  lua_pushnumber(state, openwow::ui::StoredUiHorizontalCoordinateToPixels(widget.shadow_x));
  lua_pushnumber(state, openwow::ui::StoredUiHorizontalCoordinateToPixels(widget.shadow_y));
  return 2;
}

int LuaWidget_SetShadowOffset(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  if (lua_isnumber(state, 2) == 0 || lua_isnumber(state, 3) == 0) {
    return luaL_error(state, "Usage: %s:SetShadowOffset(x, y)",
                      widget.name.empty() ? "<unnamed>" : widget.name.c_str());
  }

  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetShadowOffset(
        widget.name,
        openwow::ui::PixelUiHorizontalCoordinateToStored(
            static_cast<float>(lua_tonumber(state, 2))),
        openwow::ui::PixelUiHorizontalCoordinateToStored(
            static_cast<float>(lua_tonumber(state, 3))));
  }
  return 0;
}

int LuaWidget_GetSpacing(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  lua_pushnumber(
      state,
      openwow::ui::StoredUiHorizontalCoordinateToPixels(widget.text_spacing_stored));
  return 1;
}

int LuaWidget_SetSpacing(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetSpacing(spacing)",
                      GetUsageWidgetName(state).c_str());
  }

  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetTextSpacing(
        widget.name,
        openwow::ui::PixelUiHorizontalCoordinateToStored(
            static_cast<float>(lua_tonumber(state, 2))));
  }
  return 0;
}

int LuaWidget_SetTextHeight(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetTextHeight(pixelHeight)",
                      GetUsageWidgetName(state).c_str());
  }

  const float height = static_cast<float>(lua_tonumber(state, 2));
  if (!(height > kMinPositiveTextHeightPixels)) {
    const std::string widget_name =
        widget.name.empty() ? std::string("<unnamed>") : widget.name;
    return luaL_error(
        state,
        "%s:SetTextHeight(): invalid texHeight: %f, height must be > 0",
        widget_name.c_str(), height);
  }

  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetTextHeightStored(
        widget.name, openwow::ui::PixelUiHorizontalCoordinateToStored(height));
  }
  return 0;
}

int LuaWidget_GetStringHeight(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* widget_runtime = GetWidgetRuntime(state);
      widget_runtime != nullptr && !name.empty()) {
    const auto widget = widget_runtime->GetWidget(name);
    if (widget.has_value()) {
      if (const auto measurement =
              MeasureGlueFontString(GetGlueRuntime(state), widget_runtime, *widget);
          measurement.has_value()) {
        lua_pushnumber(state, measurement->height);
        return 1;
      }
    }
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_SetAlphaGradient(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (lua_isnumber(state, 2) == 0 || lua_isnumber(state, 3) == 0) {
    return luaL_error(state, "Usage: %s:SetAlphaGradient(start, length)",
                      name.c_str());
  }
  const int start = static_cast<int>(lua_tonumber(state, 2));
  const int length = static_cast<int>(lua_tonumber(state, 3));
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !name.empty()) {
    if (const auto widget = runtime->GetWidget(name); widget.has_value() &&
        !openwow::render::text::IsTextAlphaGradientActive(
            widget->text, start, length)) {
      lua_pushnil(state);
      return 1;
    }
  }
  lua_pushnumber(state, 1);
  return 1;
}

int LuaWidget_CanWordWrap(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value() && !widget->word_wrap) {
      lua_pushnil(state);
      return 1;
    }
  }
  lua_pushnumber(state, 1);
  return 1;
}

int LuaWidget_SetWordWrap(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const bool enable = ScriptReadBoolArgOrDefault(state, 2, true);
    runtime->SetWordWrap(name, enable);
  }
  return 0;
}

int LuaWidget_CanNonSpaceWrap(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value() && widget->non_space_wrap) {
      lua_pushnumber(state, 1);
      return 1;
    }
  }
  lua_pushnil(state);
  return 1;
}

int LuaWidget_SetNonSpaceWrap(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const bool enable = ScriptReadBoolArgOrDefault(state, 2, true);
    runtime->SetNonSpaceWrap(name, enable);
  }
  return 0;
}

int LuaWidget_GetIndentedWordWrap(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value() && widget->indented_word_wrap) {
      lua_pushnumber(state, 1);
      return 1;
    }
  }
  lua_pushnil(state);
  return 1;
}

int LuaWidget_SetIndentedWordWrap(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const bool enable = ScriptReadBoolArgOrDefault(state, 2, true);
    runtime->SetIndentedWordWrap(name, enable);
  }
  return 0;
}

int LuaWidget_GetJustifyH(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  if (widget.justify_h.empty()) {
    lua_pushstring(state, "CENTER");
    return 1;
  }
  uint32_t flags = 0;
  const char* result =
      openwow::ui::StringToHorizontalJustify(widget.justify_h.c_str(), &flags) != 0
          ? openwow::ui::HorizontalJustifyFlagsToString(flags)
          : openwow::ui::HorizontalJustifyFlagsToString(0);
  lua_pushstring(state, result);
  return 1;
}

int LuaWidget_GetJustifyV(lua_State* state) {
  const auto widget = GetCheckedFontStringWidget(state);
  if (widget.justify_v.empty()) {
    lua_pushstring(state, "MIDDLE");
    return 1;
  }
  uint32_t flags = 0;
  const char* result =
      openwow::ui::StringToVerticalJustify(widget.justify_v.c_str(), &flags) != 0
          ? openwow::ui::VerticalJustifyFlagsToString(flags)
          : openwow::ui::VerticalJustifyFlagsToString(0);
  lua_pushstring(state, result);
  return 1;
}

}
