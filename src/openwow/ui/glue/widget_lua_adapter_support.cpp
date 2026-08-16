#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/game/localization.h"
#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/ui/texture_natural_size.h"
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
#include "openwow/ui/glue/widget_lua_adapter_support.h"
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

namespace openwow::ui::glue::detail {

int LuaShowUIPanel(lua_State* state);
int LuaHideUIPanel(lua_State* state);

const char* const kGlueMovableField = "__ow_movable";
const char* const kGlueResizableField = "__ow_resizable";
const char* const kGlueToplevelField = "__ow_toplevel";
const char* const kGlueUserPlacedField = "__ow_user_placed";
const char* const kGlueMouseWheelEnabledField = "__ow_mousewheel_enabled";
const char* const kGlueMouseEnabledField = "__ow_mouse_enabled";
const char* const kGlueKeyboardEnabledField = "__ow_keyboard_enabled";
const char* const kGlueJoystickEnabledField = "__ow_joystick_enabled";
const char* const kGlueRegisteredDragButtonMaskField = "__ow_registered_drag_button_mask";
const char* const kGlueTitleRegionField = "__ow_title_region";
const char* const kGlueTitleRegionNameSuffix = ".$TitleRegion";
const char* const kWidgetMethodsRegistryKey = "openwow.widget_methods";
const char* const kWidgetTablesRegistryKey = "openwow.widget_tables";
const float kMinPositiveTextHeightPixels = 0.00000011920929f;
const double kSimpleWidgetWriteEpsilon = 0.00000023841858;
const std::uint32_t kModelSequenceCount = 0x1FAu;

bool GlueWidgetBoundsPixels::valid() const {
  return right > left && bottom > top;
}

void GlueWidgetBoundsPixels::Include(const float x, const float y,
                                     const float width,
                                     const float height) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
      !std::isfinite(height) || width <= 0.0F || height <= 0.0F) {
    return;
  }
  left = std::min(left, x);
  top = std::min(top, y);
  right = std::max(right, x + width);
  bottom = std::max(bottom, y + height);
}

void EnsureWidgetTableRegistry(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kWidgetTablesRegistryKey);
  if (lua_istable(state, -1) != 0) {
    lua_pop(state, 1);
    return;
  }
  lua_pop(state, 1);
  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, kWidgetTablesRegistryKey);
}

void StoreWidgetTableByRuntimeKey(lua_State* state,
                                  const std::string& runtime_key,
                                  int table_index) {
  if (runtime_key.empty()) {
    return;
  }
  table_index = lua_absindex(state, table_index);
  EnsureWidgetTableRegistry(state);
  lua_getfield(state, LUA_REGISTRYINDEX, kWidgetTablesRegistryKey);

  lua_pushvalue(state, table_index);
  lua_rawget(state, -2);
  size_t stored_key_length = 0;
  const char* const stored_key =
      lua_tolstring(state, -1, &stored_key_length);
  if (stored_key != nullptr &&
      std::string_view(stored_key, stored_key_length) == runtime_key) {
    lua_pop(state, 2);
    return;
  }
  lua_pop(state, 1);

  openwow::ui::game::detail::AttachLuaScriptObjectThis(state, table_index);
  lua_getfield(state, -1, runtime_key.c_str());
  if (lua_istable(state, -1) != 0 &&
      lua_rawequal(state, -1, table_index) == 0) {
    lua_rawgeti(state, -1, 0);
    if (lua_type(state, -1) == LUA_TLIGHTUSERDATA) {
      lua_pushnil(state);
      lua_rawset(state, -4);
    } else {
      lua_pop(state, 1);
    }
    lua_pushvalue(state, -1);
    lua_pushnil(state);
    lua_rawset(state, -4);
  }
  lua_pop(state, 1);
  lua_pushvalue(state, table_index);
  lua_setfield(state, -2, runtime_key.c_str());
  lua_pushvalue(state, table_index);
  lua_pushlstring(state, runtime_key.data(), runtime_key.size());
  lua_rawset(state, -3);
  lua_rawgeti(state, table_index, 0);
  if (lua_type(state, -1) == LUA_TLIGHTUSERDATA) {
    lua_pushlstring(state, runtime_key.data(), runtime_key.size());
    lua_rawset(state, -3);
  } else {
    lua_pop(state, 1);
  }
  lua_pop(state, 1);
}

bool PushStoredWidgetTableByRuntimeKey(lua_State* state,
                                       const std::string& runtime_key) {
  if (runtime_key.empty()) {
    return false;
  }
  lua_getfield(state, LUA_REGISTRYINDEX, kWidgetTablesRegistryKey);
  if (lua_istable(state, -1) != 0) {
    lua_getfield(state, -1, runtime_key.c_str());
    if (lua_istable(state, -1) != 0) {
      lua_remove(state, -2);
      return true;
    }
    lua_pop(state, 1);
  }
  lua_pop(state, 1);

  return false;
}

std::string ReadStoredWidgetRuntimeKey(lua_State* state, int table_index) {
  if (state == nullptr || lua_istable(state, table_index) == 0) {
    return {};
  }
  table_index = lua_absindex(state, table_index);
  lua_getfield(state, LUA_REGISTRYINDEX, kWidgetTablesRegistryKey);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return {};
  }
  lua_rawgeti(state, table_index, 0);
  if (lua_type(state, -1) != LUA_TLIGHTUSERDATA) {
    lua_pop(state, 2);
    return {};
  }
  lua_rawget(state, -2);
  std::string runtime_key;
  if (lua_isstring(state, -1) != 0) {
    size_t length = 0;
    const char* value = lua_tolstring(state, -1, &length);
    if (value != nullptr) {
      runtime_key.assign(value, length);
    }
  }
  lua_pop(state, 2);
  return runtime_key;
}

bool PushWidgetTableByRuntimeKey(lua_State* state, const std::string& runtime_key) {
  if (PushStoredWidgetTableByRuntimeKey(state, runtime_key)) {
    return true;
  }

  lua_getglobal(state, runtime_key.c_str());
  if (lua_istable(state, -1) != 0) {
    StoreWidgetTableByRuntimeKey(state, runtime_key, -1);
    return true;
  }
  lua_pop(state, 1);
  return false;
}

void FinalizePublishedWidgetGlobalImpl(lua_State* state, const std::string& name) {
  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    return;
  }

  const auto* frame = runtime->GetLayoutFrameDefinition(name);
  if (frame == nullptr) {
    return;
  }

  const int top = lua_gettop(state);
  if (!PushWidgetTableByRuntimeKey(state, name)) {
    lua_settop(state, top);
    return;
  }
  const int frame_index = lua_absindex(state, -1);

  int parent_index = 0;
  if (!frame->parent.empty()) {
    if (PushWidgetTableByRuntimeKey(state, frame->parent)) {
      parent_index = lua_absindex(state, -1);
    }
  }

  openwow::ui::anim::ApplyFrameXmlLoadBehavior(state, frame_index, parent_index, *frame);
  lua_settop(state, top);
}

std::string ExpandSimpleRenderScriptText(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return {};
  }

  return openwow::game::ExpandLocalizedTextTags(
      text, openwow::game::Localization::Get().GetLocale());
}

std::string ResolveScriptParentToken(GlueWidgetRuntime* runtime,
                                     const std::string& owner_name,
                                     std::string value) {
  if (runtime == nullptr || owner_name.empty() || value.find("$parent") == std::string::npos) {
    return value;
  }

  const auto owner = runtime->GetWidget(owner_name);
  if (!owner.has_value() || owner->parent.empty()) {
    return value;
  }

  std::string::size_type pos = 0;
  while ((pos = value.find("$parent", pos)) != std::string::npos) {
    value.replace(pos, 7, owner->parent);
    pos += owner->parent.size();
  }
  return value;
}

bool IsFramePointToken(const char* value) {
  int point = 0;
  return openwow::ui::StringToFramePoint(value, &point) != 0;
}

float NormalizePackedColorComponent(float value) {
  if (std::isnan(value)) {
    value = 1.0F;
  } else if (value < 0.0F) {
    value = 0.0F;
  } else if (value >= 1.0F) {
    value = 1.0F;
  }

  const int quantized =
      std::clamp(static_cast<int>(value * 255.0F + 0.5F), 0, 255);
  return static_cast<float>(quantized) / 255.0F;
}

float GetScriptColorArgumentOrDefault(lua_State* state,
                                      int argument_index,
                                      float default_value) {
  if (lua_isnumber(state, argument_index) == 0) {
    return default_value;
  }

  return static_cast<float>(lua_tonumber(state, argument_index));
}

ScriptBackdropColorArgs ParseScriptBackdropColorArgs(lua_State* state) {
  ScriptBackdropColorArgs color;
  color.red = static_cast<float>(lua_tonumber(state, 2));
  color.green = static_cast<float>(lua_tonumber(state, 3));
  color.blue = static_cast<float>(lua_tonumber(state, 4));
  if (lua_isnumber(state, 5) != 0) {
    color.alpha = static_cast<float>(lua_tonumber(state, 5));
  }
  return color;
}

std::uint32_t ClampLuaNumberToClientU32(lua_State* state, int index) {
  const double value = lua_tonumber(state, index);
  if (!std::isfinite(value) || value <= 0.0) {
    return 0;
  }
  if (value >= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(value);
}

int RuntimeModelIndexFromClientU32(const std::uint32_t value) {
  return static_cast<int>(
      std::min<std::uint32_t>(value, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
}

int EnsureBackdropOutputTable(lua_State* state) {
  if (lua_istable(state, 2) != 0) {
    lua_pushvalue(state, 2);
  } else {
    lua_createtable(state, 0, 6);
  }
  return lua_absindex(state, -1);
}

int EnsureBackdropInsetsOutputTable(lua_State* state, const int backdrop_index) {
  const int absolute_backdrop_index = lua_absindex(state, backdrop_index);
  lua_getfield(state, absolute_backdrop_index, "insets");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    lua_createtable(state, 0, 4);
    lua_pushvalue(state, -1);
    lua_setfield(state, absolute_backdrop_index, "insets");
  }
  return lua_absindex(state, -1);
}

void FillBackdropOutputTable(lua_State* state,
                             const int backdrop_index,
                             const openwow::ui::framexml::detail::BackdropSpec& bd) {
  const int absolute_backdrop_index = lua_absindex(state, backdrop_index);
  lua_pushstring(state, bd.bg_file.c_str());
  lua_setfield(state, absolute_backdrop_index, "bgFile");

  lua_pushstring(state, bd.edge_file.c_str());
  lua_setfield(state, absolute_backdrop_index, "edgeFile");

  if (bd.tile) {
    lua_pushnumber(state, 1.0);
    lua_setfield(state, absolute_backdrop_index, "tile");
  } else {
    lua_pushnil(state);
    lua_setfield(state, absolute_backdrop_index, "tile");
  }

  lua_pushnumber(state, bd.tile_size);
  lua_setfield(state, absolute_backdrop_index, "tileSize");

  lua_pushnumber(state, bd.edge_size);
  lua_setfield(state, absolute_backdrop_index, "edgeSize");

  const int insets_index = EnsureBackdropInsetsOutputTable(state, absolute_backdrop_index);
  lua_pushnumber(state, bd.inset_left);
  lua_setfield(state, insets_index, "left");
  lua_pushnumber(state, bd.inset_right);
  lua_setfield(state, insets_index, "right");
  lua_pushnumber(state, bd.inset_top);
  lua_setfield(state, insets_index, "top");
  lua_pushnumber(state, bd.inset_bottom);
  lua_setfield(state, insets_index, "bottom");
  lua_pop(state, 1);
}

std::optional<openwow::render::text::TextLayout> MeasureGlueFontString(
    GlueLuaRuntime* runtime,
    GlueWidgetRuntime* widget_runtime,
    const GlueWidgetState& widget) {
  if (runtime == nullptr || widget.text.empty()) {
    return std::nullopt;
  }

  const auto* vfs = runtime->vfs();
  if (vfs == nullptr) {
    return std::nullopt;
  }

  const auto resolved_style =
      ResolveGlueFontStringStyle(runtime->font_registry(), widget);
  if (!resolved_style.has_bound_font || resolved_style.font.font_file.empty() ||
      resolved_style.font.height_px <= 0) {
    return std::nullopt;
  }

  const auto coordinates =
      widget_runtime != nullptr && !widget.name.empty()
          ? ResolveGlueFontCoordinateSpace(*widget_runtime, widget.name)
          : GlueFontCoordinateSpace{};
  openwow::render::text::TextLayoutRequest request;
  request.maximum_width = coordinates.ScreenPixelsToScriptUnits(
      static_cast<float>(widget.width));
  request.maximum_height = coordinates.ScreenPixelsToScriptUnits(
      static_cast<float>(widget.height));
  request.line_spacing = resolved_style.line_spacing_px;
  request.line_height =
      openwow::ui::StoredUiHorizontalCoordinateToPixels(
          widget.text_height_stored);
  request.maximum_lines = static_cast<std::uint32_t>(std::max(0, widget.max_lines));
  request.wrap = openwow::render::text::ResolveWrapMode(
      widget.word_wrap, resolved_style.non_space_wrap);
  request.indent_continuation_lines = resolved_style.indented_word_wrap;

  const std::uint64_t cache_key = BuildGlueTextLayoutCacheKey(
      *vfs, resolved_style.font.font_file, resolved_style.font.height_px,
      request,
      coordinates.render_scale());
  if (widget_runtime != nullptr && !widget.name.empty()) {
    if (const auto cached = widget_runtime->GetCachedTextExtent(
            widget.name, cache_key);
        cached.has_value()) {
      return openwow::render::text::TextLayout{
          .width = cached->width,
          .height = cached->height,
      };
    }
  }

  auto measured = openwow::ui::LayoutFontText(
      vfs, resolved_style.font.font_file, resolved_style.font.height_px,
      widget.text, request, coordinates.render_scale());
  if (widget_runtime != nullptr && !widget.name.empty() &&
      measured && measured->width > 0.0F && measured->height > 0.0F) {
    widget_runtime->CacheTextExtent(
        widget.name, cache_key,
        GlueTextExtent{.width = measured->width,
                       .height = measured->height},
        false, true);
  }
  return measured;
}

float ConvertGlueWidgetPixelsToScriptDimension(const GlueWidgetRuntime& runtime,
                                               const std::string& widget_name,
                                               const float pixels) {
  return ResolveGlueFontCoordinateSpace(runtime, widget_name)
      .ScreenPixelsToScriptUnits(pixels);
}

std::optional<GlueWidgetBoundsPixels> ResolveGlueWidgetBoundsPixels(
    lua_State* state,
    const std::string& root_name,
    const GlueWidgetState& root) {
  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    return std::nullopt;
  }

  GlueWidgetBoundsPixels bounds;
  bounds.Include(static_cast<float>(root.x), static_cast<float>(root.y),
                 static_cast<float>(root.width), static_cast<float>(root.height));

  for (const auto& candidate_name : runtime->ShownDescendantNames(root_name)) {
    const auto candidate = runtime->GetWidget(candidate_name);
    if (!candidate.has_value()) {
      continue;
    }

    float candidate_width = static_cast<float>(candidate->width);
    float candidate_height = static_cast<float>(candidate->height);
    if (openwow::text::EqualsIgnoreCaseAscii(candidate->kind, "FontString") &&
        candidate_name.ends_with(".__HTMLContent1") &&
        (candidate_height <= 0.0F || runtime->IsLayoutDirty())) {

      std::optional<GlueTextExtent> cached =
          runtime->GetCachedIntrinsicTextExtent(candidate_name);
      std::optional<openwow::render::text::TextLayout> measured;
      if (!cached.has_value()) {
        measured = MeasureGlueFontString(GetGlueRuntime(state), runtime,
                                         *candidate);
      }
      if (cached.has_value() || measured.has_value()) {
        const auto coordinates =
            ResolveGlueFontCoordinateSpace(*runtime, candidate_name);
        const float measured_width =
            cached.has_value() ? cached->width : measured->width;
        const float measured_height =
            cached.has_value() ? cached->height : measured->height;
        if (candidate_width <= 0.0f) {
          candidate_width =
              coordinates.ScriptUnitsToScreenPixels(measured_width);
        }
        candidate_height =
            coordinates.ScriptUnitsToScreenPixels(measured_height);
      }
    }

    bounds.Include(static_cast<float>(candidate->x),
                   static_cast<float>(candidate->y),
                   candidate_width, candidate_height);
  }

  return bounds.valid() ? std::optional<GlueWidgetBoundsPixels>{bounds}
                        : std::nullopt;
}

float ResolveGlueWidgetScriptDimension(lua_State* state,
                                       const std::string& widget_name,
                                       const GlueWidgetState& widget,
                                       const bool horizontal,
                                       const bool use_explicit) {
  auto* widget_runtime = GetWidgetRuntime(state);
  if (widget_runtime == nullptr) {
    return 0.0f;
  }

  const float raw_script_dimension = ConvertGlueWidgetPixelsToScriptDimension(
      *widget_runtime, widget_name,
      horizontal ? static_cast<float>(widget.width) : static_cast<float>(widget.height));

  const auto* layout_frame = widget_runtime->GetLayoutFrameDefinition(widget_name);
  const auto explicit_dimension =
      layout_frame != nullptr ? (horizontal ? layout_frame->width : layout_frame->height)
                              : std::nullopt;
  if (use_explicit) {
    return explicit_dimension.value_or(0.0f);
  }
  const bool is_font_string =
      openwow::text::EqualsIgnoreCaseAscii(widget.kind, "FontString");
  const bool cached_intrinsic_axis =
      layout_frame != nullptr &&
      (horizontal ? layout_frame->font_intrinsic_width
                  : layout_frame->font_intrinsic_height);
  const bool intrinsic_font_axis =
      is_font_string &&
      (cached_intrinsic_axis || !explicit_dimension.has_value() ||
       *explicit_dimension <= 0.0F);

  if (widget_runtime->IsLayoutDirty()) {
    if (explicit_dimension.has_value() && !intrinsic_font_axis) {
      return *explicit_dimension;
    }
  }

  if (openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Texture")) {
    if (raw_script_dimension != 0.0f) {
      return raw_script_dimension;
    }

    const bool solid_colour =
        widget.texture_file.empty() && widget.has_vertex_color;
    const openwow::ui::TextureNaturalSize natural =
        openwow::ui::ResolveTextureNaturalSize(
            widget_runtime->texture_natural_size_source(), widget.texture_file,
            solid_colour);
    const std::optional<float>& axis = horizontal ? natural.width : natural.height;
    return axis.value_or(raw_script_dimension);
  }

  if (!is_font_string) {
    return raw_script_dimension;
  }

  std::optional<openwow::render::text::TextLayout> measurement;
  if (raw_script_dimension == 0.0f || intrinsic_font_axis) {
    auto measurement_widget = widget;
    if (layout_frame != nullptr) {
      const bool intrinsic_width =
          layout_frame->font_intrinsic_width || !layout_frame->width.has_value() ||
          *layout_frame->width <= 0.0F;
      const bool intrinsic_height =
          layout_frame->font_intrinsic_height || !layout_frame->height.has_value() ||
          *layout_frame->height <= 0.0F;
      if (intrinsic_width) measurement_widget.width = 0;
      if (intrinsic_height) measurement_widget.height = 0;
    } else if (intrinsic_font_axis) {
      if (horizontal) measurement_widget.width = 0;
      else measurement_widget.height = 0;
    }
    measurement = MeasureGlueFontString(
        GetGlueRuntime(state), widget_runtime, measurement_widget);
  }

  return openwow::ui::ResolveFontStringEffectiveScriptDimension(
      intrinsic_font_axis ? 0.0F : raw_script_dimension,
      measurement.has_value()
          ? (horizontal ? measurement->width : measurement->height)
          : 0.0f);
}

void StoreGlueFrameId(lua_State* state, int index, int id) {
  index = lua_absindex(state, index);
  lua_pushnumber(state, static_cast<lua_Number>(id));
  lua_setfield(state, index, "__ow_id");
}

int ReadGlueFrameId(lua_State* state, int index) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, "__ow_id");
  int id = 0;
  if (lua_isnumber(state, -1) != 0) {
    id = static_cast<int>(lua_tonumber(state, -1));
  }
  lua_pop(state, 1);
  return id;
}

void SetGlueFrameBooleanField(lua_State* state, int index,
                                     const char* field, bool value) {
  index = lua_absindex(state, index);
  lua_pushboolean(state, value ? 1 : 0);
  lua_setfield(state, index, field);
}

bool GetGlueFrameBooleanField(lua_State* state, int index,
                                     const char* field) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, field);
  const bool value = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  return value;
}

std::string GetCheckedGlueWidgetName(lua_State* state) {
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
  if (!IsUiParentName(name) && !runtime->GetWidget(name).has_value()) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }
  return name;
}

bool GlueWidgetMatchesFrameType(const GlueWidgetState& widget) {
  return !openwow::text::EqualsIgnoreCaseAscii(widget.kind, "FontString") &&
         !openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Texture") &&
         !openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Line");
}

std::string ReadGlueTableStringField(lua_State* state, int index, const char* field_name) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, field_name);
  const char* value = lua_tostring(state, -1);
  std::string out = value != nullptr ? std::string(value) : std::string();
  lua_pop(state, 1);
  return out;
}

std::string ReadGlueWidgetRuntimeKey(lua_State* state, int index) {
  std::string key = ReadGlueTableStringField(state, index, kGlueLuaFrameRuntimeKeyField);
  if (!key.empty()) {
    return key;
  }
  return ReadGlueTableStringField(state, index, kGlueLuaFrameNameField);
}

bool IsGlueFrameLikeType(std::string_view frame_type) {
  return openwow::ui::widgets::ResolveRegisteredCreateFrameTypeName(frame_type) !=
         nullptr;
}

int ReadCreateFrameNumericId(lua_State* state, int index) {
  if (lua_isnoneornil(state, index) != 0) {
    return 0;
  }

  if (lua_isstring(state, index) != 0) {
    const char* raw = lua_tostring(state, index);
    return raw != nullptr
               ? openwow::ui::framexml::ParseIntegerAttributeValue(raw)
                     .value_or(0)
               : 0;
  }
  if (lua_isnumber(state, index) != 0) {
    return static_cast<int>(lua_tointeger(state, index));
  }
  return 0;
}

void PushAnonymousGlueFrame(lua_State* state,
                                   std::string_view frame_type,
                                   const std::string& runtime_key,
                                   int id) {
  EnsureWidgetMethodTable(state);
  lua_newtable(state);
  const int frame_index = lua_absindex(state, -1);

  if (!runtime_key.empty()) {
    lua_pushlstring(state, runtime_key.data(), runtime_key.size());
    lua_setfield(state, frame_index, kGlueLuaFrameRuntimeKeyField);
  }
  lua_pushliteral(state, "");
  lua_setfield(state, frame_index, "__ow_public_name");
  lua_pushlstring(state, frame_type.data(), frame_type.size());
  lua_setfield(state, frame_index, "__ow_type");
  lua_pushboolean(state, 1);
  lua_setfield(state, frame_index, "__ow_visible");
  if (id != 0) {
    StoreGlueFrameId(state, frame_index, id);
  }

  lua_getfield(state, LUA_REGISTRYINDEX, kWidgetMethodsRegistryKey);
  lua_setmetatable(state, frame_index);
  BindWidgetObjectTypeMethods(state, std::string(frame_type));
  StoreWidgetTableByRuntimeKey(state, runtime_key, frame_index);
}

std::string GetCheckedGlueFrameWidgetName(lua_State* state) {
  const auto name = GetCheckedGlueWidgetName(state);
  if (IsUiParentName(name)) {
    return name;
  }

  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }

  const auto widget = runtime->GetWidget(name);
  if (!widget.has_value()) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }
  if (!GlueWidgetMatchesFrameType(*widget)) {
    luaL_error(state, "Wrong object type for member function");
  }
  return name;
}

std::string GetCheckedGlueEditBoxWidgetName(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  auto* runtime = GetWidgetRuntime(state);
  const auto widget = runtime != nullptr && !IsUiParentName(name)
                          ? runtime->GetWidget(name)
                          : std::optional<GlueWidgetState>{};
  if (!widget.has_value() ||
      !openwow::text::EqualsIgnoreCaseAscii(widget->kind, "EditBox")) {
    luaL_error(state, "Wrong object type for member function");
  }
  return name;
}

bool GlueTypeMatchesRegionObjectType(const char* type_name) {
  return std::strcmp(type_name, "AnimationGroup") != 0 &&
         std::strcmp(type_name, "Animation") != 0 &&
         std::strcmp(type_name, "Alpha") != 0 &&
         std::strcmp(type_name, "Scale") != 0 &&
         std::strcmp(type_name, "Translation") != 0 &&
         std::strcmp(type_name, "Rotation") != 0 &&
         std::strcmp(type_name, "Path") != 0 &&
         std::strcmp(type_name, "ControlPoint") != 0 &&
         std::strcmp(type_name, "Font") != 0;
}

std::string GetCheckedGlueRegionWidgetName(lua_State* state) {
  if (lua_istable(state, 1) == 0) {
    luaL_error(state,
               "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }

  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }

  const auto name = WidgetNameFromArg(state, 1);
  if (!name.empty()) {
    if (IsUiParentName(name)) {
      return name;
    }
    if (runtime->GetWidget(name).has_value()) {
      return name;
    }
  }

  lua_getfield(state, 1, "__ow_type");
  const char* type_name = lua_tostring(state, -1);
  const bool has_type = type_name != nullptr && *type_name != '\0';
  const bool is_region = has_type && GlueTypeMatchesRegionObjectType(type_name);
  lua_pop(state, 1);

  if (!has_type) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }
  if (!is_region) {
    luaL_error(state, "Wrong object type for member function");
  }

  luaL_error(state, "Attempt to find 'this' in non-framescript object");
  return {};
}

std::string GetGlueFrameScriptObjectType(lua_State* state,
                                                std::string_view name) {
  if (IsUiParentName(std::string(name))) {
    return "Frame";
  }

  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    return "Frame";
  }

  const auto widget = runtime->GetWidget(std::string(name));
  if (!widget.has_value() || widget->kind.empty()) {
    return "Frame";
  }
  return widget->kind;
}

const openwow::ui::FrameScriptTypeInfo* ResolveGlueFrameScriptTypeInfo(
    lua_State* state, int self_index, int handler_index) {
  const auto name = WidgetNameFromArg(state, self_index);
  const char* handler_name = lua_tostring(state, handler_index);
  if (handler_name == nullptr || *handler_name == '\0') {
    return nullptr;
  }

  const std::string object_type = GetGlueFrameScriptObjectType(state, name);
  return openwow::ui::LookupFrameScriptTypeInfo(object_type, handler_name);
}

bool IsGlueFrameLikeForEnumeration(const GlueWidgetState& widget) {
  if (widget.virtual_template) {
    return false;
  }
  const std::string_view kind = widget.kind;
  return !(openwow::text::EqualsIgnoreCaseAscii(kind, "Region") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "Font") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "FontString") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "Texture") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "Line") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "AnimationGroup") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "Animation") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "Alpha") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "Scale") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "Translation") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "Rotation") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "Path") ||
           openwow::text::EqualsIgnoreCaseAscii(kind, "ControlPoint"));
}

std::vector<std::string> CollectGlueFrameNamesInCreationOrder(GlueWidgetRuntime& runtime) {
  std::vector<std::string> names;
  for (const auto& name : runtime.WidgetNamesInSourceOrder()) {
    const auto widget = runtime.GetWidget(name);
    if (!widget.has_value() || !IsGlueFrameLikeForEnumeration(*widget)) {
      continue;
    }
    names.push_back(name);
  }
  return names;
}

bool PushGlueWidgetGlobalTable(lua_State* state, const std::string& widget_name) {
  if (PushWidgetTableByRuntimeKey(state, widget_name)) {
    return true;
  }
  lua_getglobal(state, widget_name.c_str());
  if (lua_istable(state, -1) != 0) {
    StoreWidgetTableByRuntimeKey(state, widget_name, -1);
    return true;
  }
  lua_pop(state, 1);
  return false;
}

void FinalizePublishedWidgetGlobal(lua_State* state, const std::string& name) {
  FinalizePublishedWidgetGlobalImpl(state, name);
}

}
