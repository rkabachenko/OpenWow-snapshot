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

int LuaWidget_SetBackdrop(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    return 0;
  }

  if (lua_gettop(state) < 2 || lua_isnil(state, 2)) {
    runtime->ClearBackdrop(name);
    return 0;
  }

  if (!lua_istable(state, 2)) {
    return 0;
  }

  openwow::ui::framexml::detail::BackdropSpec spec;

  lua_getfield(state, 2, "bgFile");
  if (lua_isstring(state, -1)) {
    spec.bg_file = lua_tostring(state, -1);
  }
  lua_pop(state, 1);

  lua_getfield(state, 2, "edgeFile");
  if (lua_isstring(state, -1)) {
    spec.edge_file = lua_tostring(state, -1);
  }
  lua_pop(state, 1);

  lua_getfield(state, 2, "tile");
  spec.tile = ScriptReadBoolArgOrDefault(state, -1, false);
  lua_pop(state, 1);

  lua_getfield(state, 2, "tileSize");
  if (lua_isnumber(state, -1)) {
    spec.tile_size = static_cast<float>(lua_tonumber(state, -1));
  }
  lua_pop(state, 1);

  lua_getfield(state, 2, "edgeSize");
  if (lua_isnumber(state, -1)) {
    spec.edge_size = static_cast<float>(lua_tonumber(state, -1));
  }
  lua_pop(state, 1);

  lua_getfield(state, 2, "insets");
  if (lua_istable(state, -1)) {
    lua_getfield(state, -1, "left");
    if (lua_isnumber(state, -1)) spec.inset_left = static_cast<float>(lua_tonumber(state, -1));
    lua_pop(state, 1);

    lua_getfield(state, -1, "right");
    if (lua_isnumber(state, -1)) spec.inset_right = static_cast<float>(lua_tonumber(state, -1));
    lua_pop(state, 1);

    lua_getfield(state, -1, "top");
    if (lua_isnumber(state, -1)) spec.inset_top = static_cast<float>(lua_tonumber(state, -1));
    lua_pop(state, 1);

    lua_getfield(state, -1, "bottom");
    if (lua_isnumber(state, -1)) spec.inset_bottom = static_cast<float>(lua_tonumber(state, -1));
    lua_pop(state, 1);
  }
  lua_pop(state, 1);

  if (spec.bg_file.empty() && spec.edge_file.empty()) {
    runtime->ClearBackdrop(name);
    return 0;
  }

  runtime->SetBackdrop(name, spec);
  return 0;
}

int LuaWidget_SetBackdropColor(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const auto color = ParseScriptBackdropColorArgs(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetBackdropColor(name, color.red, color.green, color.blue,
                              color.alpha);
  }
  return 0;
}

int LuaWidget_SetBackdropBorderColor(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const auto color = ParseScriptBackdropColorArgs(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetBackdropBorderColor(name, color.red, color.green,
                                    color.blue, color.alpha);
  }
  return 0;
}

int LuaWidget_SetTexture(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    lua_pushnil(state);
    return 1;
  }
  if (lua_isnumber(state, 2)) {

    if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {

      runtime->SetTexture(name, std::string());
    }
    lua_pushnumber(state, 1);
    return 1;
  }
  const char* file = lua_isstring(state, 2) ? lua_tostring(state, 2) : nullptr;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetTexture(name, file ? std::string(file) : std::string());
  }
  lua_pushnumber(state, 1);
  return 1;
}

int LuaWidget_GetTexture(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    lua_pushnil(state);
    return 1;
  }

  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto widget = runtime->GetWidget(name);
  if (!widget.has_value() || widget->texture_file.empty()) {
    lua_pushnil(state);
    return 1;
  }

  lua_pushlstring(state, widget->texture_file.c_str(), widget->texture_file.size());
  return 1;
}
int LuaWidget_SetTexCoord(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    return 0;
  }
  constexpr float kTexCoordMin = -10000.0f;
  constexpr float kTexCoordMax =  10000.0f;
  auto validate = [&](float v) -> bool {
    return v >= kTexCoordMin && v <= kTexCoordMax;
  };
  const int nargs = lua_gettop(state);
  if (nargs == 9) {

    const float ulx = static_cast<float>(luaL_checknumber(state, 2));
    const float uly = static_cast<float>(luaL_checknumber(state, 3));
    const float llx = static_cast<float>(luaL_checknumber(state, 4));
    const float lly = static_cast<float>(luaL_checknumber(state, 5));
    const float urx = static_cast<float>(luaL_checknumber(state, 6));
    const float ury = static_cast<float>(luaL_checknumber(state, 7));
    const float lrx = static_cast<float>(luaL_checknumber(state, 8));
    const float lry = static_cast<float>(luaL_checknumber(state, 9));
    if (!validate(ulx) || !validate(uly) || !validate(llx) || !validate(lly) ||
        !validate(urx) || !validate(ury) || !validate(lrx) || !validate(lry)) {
      return luaL_error(state, "TexCoord out of range");
    }
    openwow::ui::framexml::UiTextureCoordQuad tex_coords;
    tex_coords.upper_left = {ulx, uly};
    tex_coords.lower_left = {llx, lly};
    tex_coords.upper_right = {urx, ury};
    tex_coords.lower_right = {lrx, lry};
    runtime->SetTexCoordQuad(name, tex_coords);
  } else if (nargs == 5) {

    const float left = static_cast<float>(luaL_checknumber(state, 2));
    const float right = static_cast<float>(luaL_checknumber(state, 3));
    const float top = static_cast<float>(luaL_checknumber(state, 4));
    const float bottom = static_cast<float>(luaL_checknumber(state, 5));
    if (!validate(left) || !validate(right) || !validate(top) || !validate(bottom)) {
      return luaL_error(state, "TexCoord out of range");
    }
    runtime->SetTexCoord(name, left, right, top, bottom);
  } else {
    return luaL_error(
        state,
        "Usage: %s:SetTexCoord(minX, maxX, minY, maxY) or SetTexCoord(ULx, ULy, LLx, LLy, URx, URy, LRx, LRy)",
        name.empty() ? "<unnamed>" : name.c_str());
  }
  return 0;
}

int LuaWidget_SetVertexColor(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return 0;
  }
  const float r = static_cast<float>(luaL_checknumber(state, 2));
  const float g = static_cast<float>(luaL_checknumber(state, 3));
  const float b = static_cast<float>(luaL_checknumber(state, 4));
  float a = 1.0F;
  if (lua_gettop(state) >= 5) {
    a = static_cast<float>(luaL_checknumber(state, 5));
  }
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetVertexColor(name, r, g, b, a);
  }
  return 0;
}

int LuaWidget_GetBackdrop(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr && !name.empty()) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value() && widget->backdrop.has_value()) {
      const auto& bd = *widget->backdrop;
      const int result_index = EnsureBackdropOutputTable(state);
      FillBackdropOutputTable(state, result_index, bd);
      return 1;
    }
  }
  return 0;
}

int LuaWidget_GetBackdropColor(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value() && widget->backdrop.has_value()) {
      lua_pushnumber(state, widget->backdrop->bg_color_r);
      lua_pushnumber(state, widget->backdrop->bg_color_g);
      lua_pushnumber(state, widget->backdrop->bg_color_b);
      lua_pushnumber(state, widget->backdrop->bg_color_a);
      return 4;
    }
  }
  return 0;
}

int LuaWidget_GetBackdropBorderColor(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    const auto widget = runtime->GetWidget(name);
    if (widget.has_value() && widget->backdrop.has_value()) {
      lua_pushnumber(state, widget->backdrop->border_color_r);
      lua_pushnumber(state, widget->backdrop->border_color_g);
      lua_pushnumber(state, widget->backdrop->border_color_b);
      lua_pushnumber(state, widget->backdrop->border_color_a);
      return 4;
    }
  }
  return 0;
}

}
