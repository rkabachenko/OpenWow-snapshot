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

int LuaWidget_Show(lua_State* state) {
  lua_pushcfunction(state, LuaShowUIPanel);
  lua_pushvalue(state, 1);
  lua_call(state, 1, 0);
  return 0;
}

int LuaWidget_Hide(lua_State* state) {
  lua_pushcfunction(state, LuaHideUIPanel);
  lua_pushvalue(state, 1);
  lua_call(state, 1, 0);
  return 0;
}

int LuaWidget_IsShown(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  bool shown = false;
  if (!name.empty()) {
    if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
      shown = runtime->IsShown(name);
    }
  }
  if (shown) { lua_pushnumber(state, 1); } else { lua_pushnil(state); }
  return 1;
}

int LuaWidget_GetName(lua_State* state) {
  if (lua_istable(state, 1) != 0) {
    lua_getfield(state, 1, "__ow_public_name");
    if (lua_isstring(state, -1) != 0) {
      size_t length = 0;
      const char* public_name = lua_tolstring(state, -1, &length);
      if (public_name != nullptr && length != 0) {
        return 1;
      }
      lua_pop(state, 1);
      lua_pushnil(state);
      return 1;
    }
    lua_pop(state, 1);
  }
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    lua_pushnil(state);
  } else {
    lua_pushstring(state, name.c_str());
  }
  return 1;
}

int LuaWidget_SetID(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetID(ID)", name.c_str());
  }

  const int id = static_cast<int>(lua_tonumber(state, 2));
  StoreGlueFrameId(state, 1, id);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    runtime->SetId(name, id);
  }
  return 0;
}

int LuaWidget_GetID(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (auto* runtime = GetWidgetRuntime(state);
      runtime != nullptr && !IsUiParentName(name)) {
    lua_pushnumber(state, runtime->GetId(name));
    return 1;
  }
  lua_pushnumber(state, static_cast<lua_Number>(ReadGlueFrameId(state, 1)));
  return 1;
}
int LuaWidget_RegisterEvent(lua_State* state) {
  const auto widget = WidgetNameFromArg(state, 1);
  if (lua_isstring(state, 2) == 0) {
    const char* wname = widget.empty() ? "<unnamed>" : widget.c_str();
    return luaL_error(state, "Usage: %s:RegisterEvent(\"event\")", wname);
  }
  const char* event = lua_tostring(state, 2);
  if (widget.empty() || event == nullptr || *event == '\0') {
    return 0;
  }
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    runtime->RegisterEvent(widget, event);
  }
  return 0;
}

int LuaWidget_UnregisterEvent(lua_State* state) {
  const auto widget = WidgetNameFromArg(state, 1);
  if (lua_isstring(state, 2) == 0) {
    const char* wname = widget.empty() ? "<unnamed>" : widget.c_str();
    return luaL_error(state, "Usage: %s:UnregisterEvent(\"event\")", wname);
  }
  const char* event = lua_tostring(state, 2);
  if (widget.empty() || event == nullptr || *event == '\0') {
    return 0;
  }
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    runtime->UnregisterEvent(widget, event);
  }
  return 0;
}

int LuaWidget_UnregisterAllEvents(lua_State* state) {
  const auto widget = WidgetNameFromArg(state, 1);
  if (widget.empty()) {
    return 0;
  }
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    runtime->UnregisterAllEvents(widget);
  }
  return 0;
}

int LuaWidget_IsEventRegistered(lua_State* state) {
  const auto widget = WidgetNameFromArg(state, 1);
  const char* event = luaL_optstring(state, 2, "");
  if (widget.empty() || event == nullptr || *event == '\0') {
    lua_pushnil(state);
    return 1;
  }
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    if (runtime->IsEventRegistered(widget, event)) {
      lua_pushnumber(state, 1);
    } else {
      lua_pushnil(state);
    }
    return 1;
  }
  lua_pushnil(state);
  return 1;
}

int LuaWidget_RegisterAllEvents(lua_State* state) {
  const auto widget = WidgetNameFromArg(state, 1);
  if (widget.empty()) {
    return 0;
  }
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    runtime->RegisterAllEvents(widget);
  }
  return 0;
}

int LuaWidget_AllowAttributeChanges(lua_State* state) {
  (void)state;
  return 0;
}

int LuaWidget_CanChangeAttribute(lua_State* state) {
  lua_pushnumber(state, 1);
  return 1;
}

int LuaWidget_GetAttribute(lua_State* state) {
  if (!lua_istable(state, 1)) {
    lua_pushnil(state);
    return 1;
  }

  if (lua_gettop(state) == 4 && lua_isstring(state, 3)) {
    const char* prefix = lua_tostring(state, 2);
    const char* name   = lua_tostring(state, 3);
    const char* suffix = lua_tostring(state, 4);
    if (!prefix) prefix = "";
    if (!name)   name = "";
    if (!suffix) suffix = "";

    char key[256];

    snprintf(key, sizeof(key), "__ow_attr_%s%s%s", prefix, name, suffix);
    lua_getfield(state, 1, key);
    if (!lua_isnil(state, -1)) return 1;
    lua_pop(state, 1);

    snprintf(key, sizeof(key), "__ow_attr_*%s%s", name, suffix);
    lua_getfield(state, 1, key);
    if (!lua_isnil(state, -1)) return 1;
    lua_pop(state, 1);

    snprintf(key, sizeof(key), "__ow_attr_%s%s*", prefix, name);
    lua_getfield(state, 1, key);
    if (!lua_isnil(state, -1)) return 1;
    lua_pop(state, 1);

    snprintf(key, sizeof(key), "__ow_attr_*%s*", name);
    lua_getfield(state, 1, key);
    if (!lua_isnil(state, -1)) return 1;
    lua_pop(state, 1);

    snprintf(key, sizeof(key), "__ow_attr_%s", name);
    lua_getfield(state, 1, key);
    if (!lua_isnil(state, -1)) return 1;
    lua_pop(state, 1);

    lua_pushnil(state);
    return 1;
  }

  if (!lua_isstring(state, 2)) {
    const auto name = WidgetNameFromArg(state, 1);
    return luaL_error(state, "Usage: %s:GetAttribute(\"name\")",
                      name.empty() ? "<unnamed>" : name.c_str());
  }

  const char* attr_name = lua_tostring(state, 2);
  if (!attr_name || *attr_name == '\0') {
    lua_pushnil(state);
    return 1;
  }

  char key[280];
  snprintf(key, sizeof(key), "__ow_attr_%s", attr_name);
  lua_getfield(state, 1, key);
  if (lua_isnil(state, -1)) {
    lua_pop(state, 1);
    lua_pushnil(state);
  }
  return 1;
}

int LuaWidget_SetAttribute(lua_State* state) {
  if (!lua_istable(state, 1)) {
    return 0;
  }

  lua_settop(state, 3);

  if (!lua_isstring(state, 2) || lua_type(state, 3) == LUA_TNONE) {
    const auto name = WidgetNameFromArg(state, 1);
    return luaL_error(state, "Usage: %s:SetAttribute(\"name\", value)",
                      name.empty() ? "<unnamed>" : name.c_str());
  }

  const char* attr_name = lua_tostring(state, 2);
  if (!attr_name || *attr_name == '\0') {
    return 0;
  }

  char key[280];
  snprintf(key, sizeof(key), "__ow_attr_%s", attr_name);

  lua_pushvalue(state, 3);
  lua_setfield(state, 1, key);
  return 0;
}
int LuaWidget_SetPoint(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }

  const char* point = luaL_optstring(state, 2, "CENTER");
  const std::string point_s = point ? std::string(point) : std::string("CENTER");

  const int top = lua_gettop(state);
  std::string relative_to;
  std::string relative_point = point_s;
  float x = 0.0f;
  float y = 0.0f;
  bool numeric_parent_overload = false;

  if (top >= 3) {
    if (lua_isnumber(state, 3) != 0) {

      numeric_parent_overload = true;
      x = static_cast<float>(lua_tonumber(state, 3));
      y = static_cast<float>(luaL_optnumber(state, 4, 0.0));
    } else {

      relative_to = WidgetNameFromArg(state, 3);
      if (top >= 4) {
        if (lua_isnumber(state, 4) != 0) {
          if (lua_isstring(state, 3) != 0 && IsFramePointToken(lua_tostring(state, 3))) {

            numeric_parent_overload = true;
            relative_point = relative_to.empty() ? point_s : relative_to;
            relative_to.clear();
            x = static_cast<float>(lua_tonumber(state, 4));
            y = static_cast<float>(luaL_optnumber(state, 5, 0.0));
          } else {

            x = static_cast<float>(lua_tonumber(state, 4));
            y = static_cast<float>(luaL_optnumber(state, 5, 0.0));
          }
        } else {

          const char* rel_point = luaL_optstring(state, 4, point_s.c_str());
          relative_point = rel_point ? std::string(rel_point) : point_s;
          x = static_cast<float>(luaL_optnumber(state, 5, 0.0));
          y = static_cast<float>(luaL_optnumber(state, 6, 0.0));
        }
      }
    }
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    if (numeric_parent_overload && relative_to.empty()) {
      if (const auto widget = runtime->GetWidget(name); widget.has_value()) {
        relative_to = widget->parent;
      }
    }
    relative_to = ResolveScriptParentToken(runtime, name, std::move(relative_to));
    runtime->SetPoint(name,
                      point_s,
                      relative_to,
                      relative_point,
                      x,
                      y);
  }
  return 0;
}

int LuaWidget_SetAllPoints(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    auto relative_to = WidgetNameFromArg(state, 2);
    relative_to = ResolveScriptParentToken(runtime, name, std::move(relative_to));
    runtime->SetAllPoints(name, relative_to);
  }
  return 0;
}

int LuaWidget_ClearAllPoints(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->ClearAllPoints(name);
  }
  return 0;
}

int LuaWidget_SetSize(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const float w = static_cast<float>(luaL_checknumber(state, 2));
  const float h = static_cast<float>(luaL_checknumber(state, 3));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetSize(name, w, h);
  }
  return 0;
}

int LuaWidget_SetWidth(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const float w = static_cast<float>(luaL_checknumber(state, 2));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetWidth(name, w);
  }
  return 0;
}

int LuaWidget_SetHeight(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const float h = static_cast<float>(luaL_checknumber(state, 2));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetHeight(name, h);
  }
  return 0;
}

int LuaWidget_GetWidth(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    if (IsUiParentName(name)) {

      const float rs = runtime->root_scale();
      lua_pushnumber(state, static_cast<double>(runtime->viewport_width()) / rs);
      return 1;
    }
    const bool use_explicit = ScriptReadBoolArgOrDefault(state, 2, false);
    const auto current = runtime->GetWidget(name);
    lua_pushnumber(state, current.has_value()
                              ? ResolveGlueWidgetScriptDimension(
                                    state, name, *current, true, use_explicit)
                              : 0.0f);
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_GetHeight(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    if (IsUiParentName(name)) {
      const float rs = runtime->root_scale();
      lua_pushnumber(state, static_cast<double>(runtime->viewport_height()) / rs);
      return 1;
    }
    const bool use_explicit = ScriptReadBoolArgOrDefault(state, 2, false);
    const auto current = runtime->GetWidget(name);
    lua_pushnumber(state, current.has_value()
                              ? ResolveGlueWidgetScriptDimension(
                                    state, name, *current, false, use_explicit)
                              : 0.0f);
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_GetObjectType(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty() || IsUiParentName(name)) {
    lua_pushstring(state, "Frame");
    return 1;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const auto current = runtime->GetWidget(name);
    if (current.has_value() && !current->kind.empty()) {
      lua_pushstring(state, current->kind.c_str());
      return 1;
    }
  }
  lua_pushstring(state, "Frame");
  return 1;
}

int LuaWidget_GetParent(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty() || IsUiParentName(name)) {
    lua_pushnil(state);
    return 1;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const auto current = runtime->GetWidget(name);
    if (current.has_value() && !current->parent.empty()) {
      if (PushGlueWidgetGlobalTable(state, current->parent)) {
        return 1;
      }
      lua_pushnil(state);
      return 1;
    }
  }
  lua_pushnil(state);
  return 1;
}

int LuaWidget_SetParent(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty() || IsUiParentName(name)) {
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    auto parent = WidgetNameFromArg(state, 2);
    parent = ResolveScriptParentToken(runtime, name, std::move(parent));
    if (auto* glue_runtime = GetGlueRuntime(state); glue_runtime != nullptr) {
      (void)glue_runtime->SetWidgetParentWithVisibilityLifecycle(name, parent);
    } else {
      runtime->SetParent(name, parent);
    }
  }
  return 0;
}

std::optional<GlueWidgetState> GetWidgetOrUiParent(lua_State* state, const std::string& name) {
  if (name.empty()) {
    return std::nullopt;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    if (IsUiParentName(name)) {
      GlueWidgetState pseudo;
      pseudo.name = "UIParent";
      pseudo.kind = "Frame";
      pseudo.x = 0;
      pseudo.y = 0;
      pseudo.width = runtime->viewport_width();
      pseudo.height = runtime->viewport_height();
      pseudo.visible = true;
      return pseudo;
    }
    return runtime->GetWidget(name);
  }
  return std::nullopt;
}

std::optional<GlueWidgetState> GetRectWidgetOrUiParent(lua_State* state,
                                                       const std::string& name) {
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    if (runtime->IsLayoutDirty()) {
      runtime->ResolveLayout(runtime->viewport_width(), runtime->viewport_height());
    }
    if (!IsUiParentName(name) && !runtime->HasResolvedLayout(name)) {
      return std::nullopt;
    }
  }
  return GetWidgetOrUiParent(state, name);
}

struct WidgetScriptRect {
  double left{0.0};
  double bottom{0.0};
  double width{0.0};
  double height{0.0};

  [[nodiscard]] double right() const noexcept { return left + width; }
  [[nodiscard]] double top() const noexcept { return bottom + height; }
  [[nodiscard]] double center_x() const noexcept { return left + (width * 0.5); }
  [[nodiscard]] double center_y() const noexcept { return bottom + (height * 0.5); }
};

WidgetScriptRect BuildWidgetScriptRect(const GlueWidgetState& widget,
                                       const int viewport_height) {
  return {
      .left = static_cast<double>(widget.x),
      .bottom =
          static_cast<double>(viewport_height - (widget.y + widget.height)),
      .width = static_cast<double>(widget.width),
      .height = static_cast<double>(widget.height),
  };
}

float GetOptionalMouseOverInset(lua_State* state, const int argument_index) {
  if (lua_isnumber(state, argument_index) == 0) {
    return 0.0f;
  }

  return static_cast<float>(lua_tonumber(state, argument_index));
}

std::optional<std::pair<float, float>> ResolveGlueCursorPositionPixels(
    lua_State* state) {
  auto* runtime = GetWidgetRuntime(state);
  const int viewport_width =
      runtime != nullptr ? runtime->viewport_width() : 0;
  const int viewport_height =
      runtime != nullptr ? runtime->viewport_height() : 0;

  if (auto* host = GetGlueHost(state); host != nullptr) {
    const auto [cursor_x_ddc, cursor_y_ddc] =
        host->GetCursorPositionDdc(viewport_width, viewport_height);
    return std::pair<float, float>{
        static_cast<float>(cursor_x_ddc),
        static_cast<float>(viewport_height) - static_cast<float>(cursor_y_ddc),
    };
  }

  if (runtime != nullptr) {
    const auto cached_cursor = runtime->cached_cursor_position();
    if (cached_cursor.has_value()) {
      return std::pair<float, float>{
          static_cast<float>(cached_cursor->first),
          static_cast<float>(cached_cursor->second),
      };
    }
  }

  return std::nullopt;
}

bool EqualsIgnoreCaseAscii(const char* lhs, const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return lhs == rhs;
  }
  while (*lhs != '\0' && *rhs != '\0') {
    const auto left = static_cast<unsigned char>(*lhs);
    const auto right = static_cast<unsigned char>(*rhs);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
    ++lhs;
    ++rhs;
  }
  return *lhs == *rhs;
}

std::uint32_t ParseRegisteredMouseButtonMask(lua_State* state, const int first_argument) {
  std::uint32_t mask = 0;
  for (int argument = first_argument; lua_isstring(state, argument) != 0; ++argument) {
    mask |= openwow::ui::widgets::MouseButtonFlag(lua_tostring(state, argument));
  }
  return mask;
}

int LuaWidget_GetRect(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const auto widget = GetRectWidgetOrUiParent(state, name);
  if (!widget.has_value()) {
    return 0;

  }
  int viewport_h = 0;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    viewport_h = runtime->viewport_height();
  }
  const WidgetScriptRect rect = BuildWidgetScriptRect(*widget, viewport_h);
  lua_pushnumber(state, rect.left);
  lua_pushnumber(state, rect.bottom);
  lua_pushnumber(state, rect.width);
  lua_pushnumber(state, rect.height);
  return 4;
}

int LuaWidget_GetLeft(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const auto widget = GetRectWidgetOrUiParent(state, name);
  if (!widget.has_value()) {
    lua_pushnil(state);
    return 1;
  }
  const WidgetScriptRect rect = BuildWidgetScriptRect(*widget, 0);
  lua_pushnumber(state, rect.left);
  return 1;
}

int LuaWidget_GetRight(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const auto widget = GetRectWidgetOrUiParent(state, name);
  if (!widget.has_value()) {
    lua_pushnil(state);
    return 1;
  }
  const WidgetScriptRect rect = BuildWidgetScriptRect(*widget, 0);
  lua_pushnumber(state, rect.right());
  return 1;
}

int LuaWidget_GetTop(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const auto widget = GetRectWidgetOrUiParent(state, name);
  if (!widget.has_value()) {
    lua_pushnil(state);
    return 1;
  }
  int viewport_h = 0;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    viewport_h = runtime->viewport_height();
  }
  const WidgetScriptRect rect = BuildWidgetScriptRect(*widget, viewport_h);
  lua_pushnumber(state, rect.top());
  return 1;
}

int LuaWidget_GetBottom(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const auto widget = GetRectWidgetOrUiParent(state, name);
  if (!widget.has_value()) {
    lua_pushnil(state);
    return 1;
  }
  int viewport_h = 0;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    viewport_h = runtime->viewport_height();
  }
  const WidgetScriptRect rect = BuildWidgetScriptRect(*widget, viewport_h);
  lua_pushnumber(state, rect.bottom);
  return 1;
}

int LuaWidget_GetCenter(lua_State* state) {

  const auto name = WidgetNameFromArg(state, 1);
  const auto widget = GetRectWidgetOrUiParent(state, name);
  if (!widget.has_value()) {
    lua_pushnil(state);
    lua_pushnil(state);
    return 2;
  }
  int viewport_h = 0;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    viewport_h = runtime->viewport_height();
  }
  const WidgetScriptRect rect = BuildWidgetScriptRect(*widget, viewport_h);
  lua_pushnumber(state, rect.center_x());
  lua_pushnumber(state, rect.center_y());
  return 2;
}

int LuaWidget_GetNumPoints(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  int count = 0;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty() && !IsUiParentName(name)) {
    count = runtime->GetNumPoints(name);
  }
  lua_pushnumber(state, count);
  return 1;
}

int LuaWidget_GetPoint(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const int index = static_cast<int>(luaL_optinteger(state, 2, 1));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty() && !IsUiParentName(name)) {
    const auto anchor = runtime->GetPoint(name, index);
    if (!anchor.has_value()) {
      lua_pushnil(state);
      return 1;
    }
    lua_pushstring(state, anchor->point.c_str());
    if ((anchor->flags & 0x100u) != 0u) {
      lua_pushnumber(state, anchor->x);
      lua_pushnumber(state, anchor->y);
      return 3;
    }

    if (!anchor->relative_to.empty() && !IsUiParentName(anchor->relative_to)) {
      lua_getglobal(state, anchor->relative_to.c_str());
      if (lua_isnil(state, -1) != 0) {
        lua_pop(state, 1);
        lua_pushnil(state);
      }
    } else {
      lua_pushnil(state);
    }
    lua_pushstring(state, anchor->relative_point.c_str());
    lua_pushnumber(state, anchor->x);
    lua_pushnumber(state, anchor->y);
    return 5;
  }
  lua_pushnil(state);
  return 1;
}

int LuaWidget_SetFrameLevel(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetFrameLevel(level)", name.c_str());
  }

  const int level = static_cast<int>(lua_tonumber(state, 2));
  if (level < 0) {
    return luaL_error(state, "%s:SetFrameLevel(): Passed negative frame level: %d",
                      name.c_str(), level);
  }

  if (IsUiParentName(name)) {
    lua_pushinteger(state, static_cast<lua_Integer>(level));
    lua_setfield(state, 1, "__ow_frame_level");
    return 0;
  }

  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetFrameLevel(name, level);
  }
  return 0;
}

int LuaWidget_GetFrameLevel(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (IsUiParentName(name)) {
    lua_getfield(state, 1, "__ow_frame_level");
    if (lua_isnumber(state, -1) == 0) {
      lua_pop(state, 1);
      lua_pushnumber(state, 0);
    }
    return 1;
  }

  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    lua_pushnumber(state, runtime->GetFrameLevel(name));
    return 1;
  }
  lua_pushnumber(state, 0);
  return 1;
}

int LuaWidget_Raise(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->Raise(name);
  }
  return 0;
}

int LuaWidget_GetScale(lua_State* state) {

  const auto name = GetCheckedGlueFrameWidgetName(state);
  auto* runtime = GetWidgetRuntime(state);
  lua_pushnumber(state, runtime->GetScale(name));
  return 1;
}

int LuaWidget_GetEffectiveScale(lua_State* state) {

  const auto name = GetCheckedGlueFrameWidgetName(state);
  auto* runtime = GetWidgetRuntime(state);
  lua_pushnumber(state, runtime->GetEffectiveScale(name));
  return 1;
}

int LuaWidget_SetScale(lua_State* state) {

  const auto name = GetCheckedGlueFrameWidgetName(state);
  const auto usage_name = GetUsageWidgetName(state);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetScale(scale)", usage_name.c_str());
  }

  const float scale = static_cast<float>(lua_tonumber(state, 2));
  if (scale <= 0.0f) {
    return luaL_error(state, "%s:SetScale(): Scale must be > 0", usage_name.c_str());
  }

  auto* runtime = GetWidgetRuntime(state);
  runtime->SetScale(name, scale);
  return 0;
}
int LuaWidget_SetFrameStrata(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (lua_isstring(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetFrameStrata(level)", name.c_str());
  }
  const char* raw_strata = lua_tostring(state, 2);
  int strata_value = 0;
  if (openwow::ui::StringToScriptFrameStrata(raw_strata, &strata_value) == 0) {
    return luaL_error(state, "%s:SetFrameStrata(): Unknown frame strata: %s",
                      name.c_str(), raw_strata != nullptr ? raw_strata : "");
  }
  const char* canonical_strata = openwow::ui::ScriptFrameStrataToString(strata_value);
  if (IsUiParentName(name)) {
    lua_pushstring(state, canonical_strata);
    lua_setfield(state, 1, "__ow_frame_strata");
    return 0;
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetFrameStrata(name, canonical_strata);
  }
  return 0;
}

int LuaWidget_IsVisible(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  bool visible = false;
  if (!name.empty()) {
    if (IsUiParentName(name)) {
      visible = true;
    } else if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
      visible = runtime->IsVisible(name);
    }
  }
  if (visible) { lua_pushnumber(state, 1); } else { lua_pushnil(state); }
  return 1;
}

int LuaWidget_SetAlpha(lua_State* state) {
  const auto name = GetCheckedGlueWidgetName(state);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetAlpha(alpha 0 to 1)",
                      name.c_str());
  }
  float alpha = static_cast<float>(lua_tonumber(state, 2));
  if (alpha < 0.0f) alpha = 0.0f;
  else if (alpha >= 1.0f) alpha = 1.0f;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetAlpha(name, alpha);
  }
  return 0;
}

int LuaWidget_GetAlpha(lua_State* state) {
  const auto name = GetCheckedGlueWidgetName(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const auto current = runtime->GetWidget(name);
    lua_pushnumber(state, current.has_value() ? current->alpha : 1.0);
    return 1;
  }
  lua_pushnumber(state, 1.0);
  return 1;
}

int LuaWidget_GetBoundsRect(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const auto widget = GetWidgetOrUiParent(state, name);
  if (!widget.has_value()) {
    return 0;
  }
  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    return 0;
  }

  const auto bounds = ResolveGlueWidgetBoundsPixels(state, name, *widget);
  if (!bounds.has_value()) {
    return 0;
  }

  const float left = ConvertGlueWidgetPixelsToScriptDimension(
      *runtime, name, bounds->left);
  const float bottom = ConvertGlueWidgetPixelsToScriptDimension(
      *runtime, name,
      static_cast<float>(runtime->viewport_height()) - bounds->bottom);
  const float width = ConvertGlueWidgetPixelsToScriptDimension(
      *runtime, name, bounds->right - bounds->left);
  const float height = ConvertGlueWidgetPixelsToScriptDimension(
      *runtime, name, bounds->bottom - bounds->top);
  lua_pushnumber(state, left);
  lua_pushnumber(state, bottom);
  lua_pushnumber(state, width);
  lua_pushnumber(state, height);
  return 4;
}

int LuaWidget_SetScript(lua_State* state) {
  const auto widget_name = GetCheckedGlueFrameWidgetName(state);
  if (!lua_isstring(state, 2)
      || (lua_type(state, 3) != LUA_TFUNCTION && lua_type(state, 3) != LUA_TNIL)) {
    return luaL_error(state, "Usage: %s:SetScript(\"type\", function)",
                      widget_name.c_str());
  }

  const auto* script_info = ResolveGlueFrameScriptTypeInfo(state, 1, 2);
  if (script_info == nullptr) {
    const char* script_name = lua_tostring(state, 2);
    return luaL_error(state, "%s doesn't have a \"%s\" script",
                      widget_name.c_str(),
                      script_name != nullptr ? script_name : "");
  }

  lua_getfield(state, 1, "__ow_scripts");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, 1, "__ow_scripts");
  }

  if (lua_isnil(state, 3) != 0) {
    lua_pushnil(state);
    lua_setfield(state, -2, script_info->canonical_name);
  } else {
    lua_pushvalue(state, 3);
    lua_setfield(state, -2, script_info->canonical_name);
  }
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    runtime->InvalidateWidgetScriptCache(widget_name, script_info->canonical_name);
  }
  if (openwow::text::EqualsIgnoreCaseAscii(script_info->canonical_name,
                                           "OnMouseWheel")) {
    if (auto* widgets = GetWidgetRuntime(state); widgets != nullptr) {
      widgets->SetMouseWheelEnabled(widget_name, lua_isnil(state, 3) == 0);
    }
  }

  lua_pop(state, 1);
  return 0;
}

}
