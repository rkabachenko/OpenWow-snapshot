#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_binding.h"
#include "openwow/ui/game/framescript/core/frame_font_face.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/game_ui_scale.h"
#include "openwow/ui/font_layout.h"
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

int SetPackedShadowColorForSharedFontObject(lua_State* L) {
  const int self_index = ValidateSharedFontObjectSelf(L);
  StorePackedColor(L, self_index, kShadowColorFieldNames, kShadowColorDefaults);
  PropagateSharedFontShadowStyle(L, self_index);
  return 0;
}

int GetPackedShadowColorForSharedFontObject(lua_State* L) {
  const int self_index = ValidateSharedFontObjectSelf(L);
  PushPackedColor(L, self_index, kShadowColorFieldNames, kShadowColorDefaults);
  return 4;
}

void StoreShadowOffsetForObject(lua_State *L, int self_index, const float x_pixels,
                                const float y_pixels) {
  self_index = lua_absindex(L, self_index);
  lua_pushnumber(L, openwow::ui::PixelUiHorizontalCoordinateToStored(x_pixels));
  lua_setfield(L, self_index, "__ow_shadow_x");
  lua_pushnumber(L, openwow::ui::PixelUiHorizontalCoordinateToStored(y_pixels));
  lua_setfield(L, self_index, "__ow_shadow_y");
}

void PushShadowOffsetComponentForObject(lua_State *L, int self_index,
                                        const char *field_name) {
  self_index = lua_absindex(L, self_index);
  lua_getfield(L, self_index, field_name);
  if (lua_isnumber(L, -1) == 0) {
    lua_pop(L, 1);
    lua_pushnumber(L, 0.0);
    return;
  }

  const float stored_component = static_cast<float>(lua_tonumber(L, -1));
  lua_pop(L, 1);
  lua_pushnumber(L, openwow::ui::StoredUiHorizontalCoordinateToPixels(stored_component));
}

int SetShadowOffsetForSharedFontObject(lua_State* L) {
  const int self_index = ValidateSharedFontObjectSelf(L);
  if (lua_isnumber(L, 2) == 0 || lua_isnumber(L, 3) == 0) {
    return luaL_error(L, "Usage: %s:SetShadowOffset(x, y)",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  StoreShadowOffsetForObject(
      L, self_index, static_cast<float>(lua_tonumber(L, 2)),
      static_cast<float>(lua_tonumber(L, 3)));
  PropagateSharedFontShadowStyle(L, self_index);
  return 0;
}

int GetShadowOffsetForSharedFontObject(lua_State* L) {
  const int self_index = ValidateSharedFontObjectSelf(L);
  PushShadowOffsetComponentForObject(L, self_index, "__ow_shadow_x");
  PushShadowOffsetComponentForObject(L, self_index, "__ow_shadow_y");
  return 2;
}

bool FontObjectHasNonEmptyStringField(lua_State *L, int index,
                                        const char *field_name) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, field_name);
  const char *value = lua_tostring(L, -1);
  const bool has_value = value != nullptr && *value != '\0';
  lua_pop(L, 1);
  return has_value;
}

bool FontObjectHasStoredField(lua_State *L, int index, const char *field_name) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, field_name);
  const bool has_value = lua_isnil(L, -1) == 0;
  lua_pop(L, 1);
  return has_value;
}

bool FontObjectHasAnyStoredFields(lua_State *L,
                                    int index,
                                    std::initializer_list<const char *> field_names) {
  for (const char *field_name : field_names) {
    if (FontObjectHasStoredField(L, index, field_name)) {
      return true;
    }
  }
  return false;
}

void CopyNamedFontObjectStyleGroup(lua_State *L,
                                          int target_index,
                                          int font_index,
                                          std::initializer_list<const char *> field_names) {
  for (const char *field_name : field_names) {
    openwow::ui::CopyLuaTableField(L, target_index, font_index, field_name);
  }
}

static bool TableReferencesFontObject(lua_State *L, int target_index, int font_index) {
  target_index = lua_absindex(L, target_index);
  font_index = lua_absindex(L, font_index);

  lua_getfield(L, target_index, "__ow_font_object");
  const bool matches = lua_rawequal(L, -1, font_index) != 0;
  lua_pop(L, 1);
  return matches;
}

static void CopyNamedFontObjectLayoutGroup(lua_State *L, int target_index, int font_index) {
  CopyNamedFontObjectStyleGroup(
      L, target_index, font_index,
      {"__ow_justifyH", "__ow_justifyV", "__ow_indented_wrap"});
}

void CopyNamedFontObjectStyle(lua_State *L, int target_index, int font_index);
static void PropagateBoundFontObjectStyleGroups(lua_State *L, int font_index);

static int EnsureFontDependentsTable(lua_State *L, int font_index) {
  font_index = lua_absindex(L, font_index);
  lua_getfield(L, font_index, kFontDependentsField);
  if (lua_istable(L, -1) != 0) {
    return lua_absindex(L, -1);
  }

  lua_pop(L, 1);
  lua_newtable(L);
  const int dependents_index = lua_absindex(L, -1);
  lua_pushvalue(L, dependents_index);
  lua_setfield(L, font_index, kFontDependentsField);
  return dependents_index;
}

static void RemoveFontDependent(lua_State *L, int font_index, int target_index) {
  font_index = lua_absindex(L, font_index);
  target_index = lua_absindex(L, target_index);

  lua_getfield(L, font_index, kFontDependentsField);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  const int dependents_index = lua_absindex(L, -1);
  const lua_Integer count = luaL_len(L, dependents_index);
  lua_Integer write_index = 1;
  for (lua_Integer read_index = 1; read_index <= count; ++read_index) {
    lua_geti(L, dependents_index, read_index);
    const bool keep = lua_istable(L, -1) != 0 && lua_rawequal(L, -1, target_index) == 0;
    if (keep) {
      if (write_index != read_index) {
        lua_pushvalue(L, -1);
        lua_seti(L, dependents_index, write_index);
      }
      ++write_index;
    }
    lua_pop(L, 1);
  }

  for (lua_Integer clear_index = write_index; clear_index <= count; ++clear_index) {
    lua_pushnil(L);
    lua_seti(L, dependents_index, clear_index);
  }
  lua_pop(L, 1);
}

static void AddFontDependent(lua_State *L, int font_index, int target_index) {
  font_index = lua_absindex(L, font_index);
  target_index = lua_absindex(L, target_index);

  const int dependents_index = EnsureFontDependentsTable(L, font_index);
  const lua_Integer count = luaL_len(L, dependents_index);
  for (lua_Integer read_index = 1; read_index <= count; ++read_index) {
    lua_geti(L, dependents_index, read_index);
    const bool exists = lua_istable(L, -1) != 0 && lua_rawequal(L, -1, target_index) != 0;
    lua_pop(L, 1);
    if (exists) {
      lua_pop(L, 1);
      return;
    }
  }

  lua_pushvalue(L, target_index);
  lua_seti(L, dependents_index, count + 1);
  lua_pop(L, 1);
}

void ClearBoundFontObject(lua_State *L, int target_index) {
  target_index = lua_absindex(L, target_index);

  lua_getfield(L, target_index, "__ow_font_object");
  if (lua_istable(L, -1) != 0) {
    RemoveFontDependent(L, -1, target_index);
  }
  lua_pop(L, 1);

  lua_pushnil(L);
  lua_setfield(L, target_index, "__ow_font_object");
}

void SetBoundFontObject(lua_State *L, int target_index, int font_index) {
  target_index = lua_absindex(L, target_index);
  font_index = lua_absindex(L, font_index);

  ClearBoundFontObject(L, target_index);
  lua_pushvalue(L, font_index);
  lua_setfield(L, target_index, "__ow_font_object");
  AddFontDependent(L, font_index, target_index);
}

enum class BoundFontObjectPropagationMode {
  LayoutOnly,
  FullStyleGroups,
  FontFaceOnly,
  TextColorOnly,
  AlphaOnly,
  SpacingOnly,
  ShadowOnly,
};

bool WouldIntroduceFontObjectBindingCycle(lua_State *L,
                                                 int target_index,
                                                 int candidate_index) {
  target_index = lua_absindex(L, target_index);
  candidate_index = lua_absindex(L, candidate_index);

  std::vector<const void *> visited;
  visited.reserve(4);
  lua_pushvalue(L, candidate_index);
  while (lua_istable(L, -1) != 0) {
    if (lua_rawequal(L, -1, target_index) != 0) {
      lua_pop(L, 1);
      return true;
    }

    const void *candidate_key = lua_topointer(L, -1);
    if (std::find(visited.begin(), visited.end(), candidate_key) != visited.end()) {
      lua_pop(L, 1);
      return false;
    }
    visited.push_back(candidate_key);

    lua_getfield(L, -1, "__ow_font_object");
    lua_remove(L, -2);
    if (lua_istable(L, -1) == 0) {
      lua_pop(L, 1);
      return false;
    }
  }

  lua_pop(L, 1);
  return false;
}

static void CopyFontObjectBindingFromSource(lua_State *L, int target_index, int source_index) {
  target_index = lua_absindex(L, target_index);
  source_index = lua_absindex(L, source_index);

  lua_getfield(L, source_index, "__ow_font_object");
  const bool has_source_binding =
      lua_istable(L, -1) != 0 &&
      !WouldIntroduceFontObjectBindingCycle(L, target_index, -1);
  if (has_source_binding) {
    lua_getfield(L, target_index, "__ow_font_object");
    const bool binding_matches = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 1);

    if (!binding_matches) {
      SetBoundFontObject(L, target_index, -1);
    }
    lua_pop(L, 1);
    return;
  }

  lua_pop(L, 1);

  lua_getfield(L, target_index, "__ow_font_object");
  const bool target_is_unbound = lua_isnil(L, -1) != 0;
  lua_pop(L, 1);

  if (!target_is_unbound) {
    ClearBoundFontObject(L, target_index);
  }
}

static void PropagateBoundFontObject(lua_State *L,
                                     int font_index,
                                     BoundFontObjectPropagationMode mode,
                                     std::vector<const void *> &visited) {
  font_index = lua_absindex(L, font_index);
  const void *font_key = lua_topointer(L, font_index);
  if (std::find(visited.begin(), visited.end(), font_key) != visited.end()) {
    return;
  }
  visited.push_back(font_key);

  lua_getfield(L, font_index, kFontDependentsField);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  const int dependents_index = lua_absindex(L, -1);
  const lua_Integer count = luaL_len(L, dependents_index);
  lua_Integer write_index = 1;
  for (lua_Integer read_index = 1; read_index <= count; ++read_index) {
    lua_geti(L, dependents_index, read_index);
    if (lua_istable(L, -1) == 0 || !TableReferencesFontObject(L, -1, font_index)) {
      lua_pop(L, 1);
      continue;
    }

    const int dependent_index = lua_absindex(L, -1);
    if (write_index != read_index) {
      lua_pushvalue(L, dependent_index);
      lua_seti(L, dependents_index, write_index);
    }
    ++write_index;

    switch (mode) {
      case BoundFontObjectPropagationMode::LayoutOnly:
        CopyNamedFontObjectLayoutGroup(L, dependent_index, font_index);
        break;
      case BoundFontObjectPropagationMode::FullStyleGroups:
        CopyNamedFontObjectStyle(L, dependent_index, font_index);
        break;
      case BoundFontObjectPropagationMode::FontFaceOnly:
        CopyNamedFontObjectStyleGroup(
            L, dependent_index, font_index,
            {"__ow_font_path", "__ow_font_size", "__ow_text_height",
             "__ow_font_flags"});
        break;
      case BoundFontObjectPropagationMode::TextColorOnly:
        CopyNamedFontObjectStyleGroup(
            L, dependent_index, font_index,
            {"__ow_text_r", "__ow_text_g", "__ow_text_b", "__ow_text_a", "__ow_alpha"});
        break;
      case BoundFontObjectPropagationMode::AlphaOnly:
        CopyNamedFontObjectStyleGroup(
            L, dependent_index, font_index, {"__ow_alpha", "__ow_text_a"});
        break;
      case BoundFontObjectPropagationMode::SpacingOnly:
        CopyNamedFontObjectStyleGroup(L, dependent_index, font_index, {"__ow_spacing"});
        break;
      case BoundFontObjectPropagationMode::ShadowOnly:
        CopyNamedFontObjectStyleGroup(
            L, dependent_index, font_index,
            {"__ow_shadow_r", "__ow_shadow_g", "__ow_shadow_b", "__ow_shadow_a",
             "__ow_shadow_x", "__ow_shadow_y"});
        break;
    }
    const char *type_name = openwow::ui::BorrowRawLuaStringField(L, dependent_index, "__ow_type");
    if (type_name != nullptr && std::strcmp(type_name, "FontString") == 0) {
      NotifyFrameInputMutation(L, dependent_index, false);
    }
    if (type_name != nullptr && std::strcmp(type_name, "Font") == 0) {
      PropagateBoundFontObject(L, dependent_index, mode, visited);
    }
    lua_pop(L, 1);
  }

  for (lua_Integer clear_index = write_index; clear_index <= count; ++clear_index) {
    lua_pushnil(L);
    lua_seti(L, dependents_index, clear_index);
  }
  lua_pop(L, 1);
}

static void PropagateBoundFontObjectLayoutGroup(lua_State *L, int font_index) {
  std::vector<const void *> visited;
  visited.reserve(4);
  PropagateBoundFontObject(
      L, font_index, BoundFontObjectPropagationMode::LayoutOnly, visited);
}

static void PropagateBoundFontObjectStyleGroups(lua_State *L, int font_index) {
  std::vector<const void *> visited;
  visited.reserve(4);
  PropagateBoundFontObject(
      L, font_index, BoundFontObjectPropagationMode::FullStyleGroups, visited);
}

static void PropagateSharedFontStyleGroup(
    lua_State *L, int self_index, const BoundFontObjectPropagationMode mode) {
  self_index = lua_absindex(L, self_index);
  using openwow::ui::widgets::ScriptObjectType;
  if (openwow::ui::game::detail::GetLuaCanonicalScriptObjectType(L, self_index) !=
      ScriptObjectType::Font) {
    return;
  }

  std::vector<const void *> visited;
  visited.reserve(4);
  PropagateBoundFontObject(L, self_index, mode, visited);
}

void PropagateSharedFontFaceStyle(lua_State *L, int self_index) {
  PropagateSharedFontStyleGroup(L, self_index,
                                BoundFontObjectPropagationMode::FontFaceOnly);
}

void PropagateSharedFontShadowStyle(lua_State *L, int self_index) {
  PropagateSharedFontStyleGroup(L, self_index,
                                BoundFontObjectPropagationMode::ShadowOnly);
}

void PropagateSharedFontTextColorStyle(lua_State *L, int self_index) {
  PropagateSharedFontStyleGroup(L, self_index,
                                BoundFontObjectPropagationMode::TextColorOnly);
}

void PropagateSharedFontAlphaStyle(lua_State *L, int self_index) {
  PropagateSharedFontStyleGroup(L, self_index,
                                BoundFontObjectPropagationMode::AlphaOnly);
}

void PropagateSharedFontLayoutStyle(lua_State *L, int self_index) {
  PropagateSharedFontStyleGroup(L, self_index,
                                BoundFontObjectPropagationMode::LayoutOnly);
}

void PropagateSharedFontSpacingStyle(lua_State *L, int self_index) {
  PropagateSharedFontStyleGroup(L, self_index,
                                BoundFontObjectPropagationMode::SpacingOnly);
}

void CopyFontObjectStateFromSource(lua_State *L, int target_index, int source_index) {
  target_index = lua_absindex(L, target_index);
  source_index = lua_absindex(L, source_index);

  CopyNamedFontObjectStyle(L, target_index, source_index);
  CopyFontObjectBindingFromSource(L, target_index, source_index);
  PropagateBoundFontObjectStyleGroups(L, target_index);
}

struct ResolvedFontBinding {
  std::string path;
  int height_pixels{0};
  std::string flags;
};

ResolvedFontBinding ResolveEffectiveFontBinding(lua_State *L, int index) {

  constexpr int kMaximumFontObjectChainDepth = 16;
  ResolvedFontBinding resolved;
  int current = lua_absindex(L, index);
  int pushed = 0;

  for (int depth = 0; depth < kMaximumFontObjectChainDepth; ++depth) {
    if (resolved.path.empty()) {
      lua_getfield(L, current, "__ow_font_path");
      if (const char *path = lua_tostring(L, -1); path != nullptr && *path != '\0') {
        resolved.path = path;
      }
      lua_pop(L, 1);
    }
    if (resolved.height_pixels <= 0) {
      lua_getfield(L, current, "__ow_font_size");
      if (lua_isnumber(L, -1) != 0) {
        resolved.height_pixels = static_cast<int>(lua_tointeger(L, -1));
      }
      lua_pop(L, 1);
    }
    if (resolved.flags.empty()) {
      lua_getfield(L, current, "__ow_font_flags");
      if (const char *flags = lua_tostring(L, -1); flags != nullptr) {
        resolved.flags = flags;
      }
      lua_pop(L, 1);
    }
    if (!resolved.path.empty() && resolved.height_pixels > 0) {
      break;
    }

    lua_getfield(L, current, "__ow_font_object");
    if (lua_istable(L, -1) == 0) {
      lua_pop(L, 1);
      break;
    }
    current = lua_absindex(L, -1);
    ++pushed;
  }

  lua_pop(L, pushed);
  return resolved;
}

static bool FontStringHasFontBinding(lua_State *L, int index) {
  index = lua_absindex(L, index);
  if (FontObjectHasNonEmptyStringField(L, index, "__ow_font_path")) {
    return true;
  }

  lua_getfield(L, index, "__ow_font_object");
  const bool has_font_object = lua_isnil(L, -1) == 0;
  lua_pop(L, 1);
  return has_font_object;
}

static std::optional<float> ReadLuaTableNumberField(lua_State *L, int index,
                                                    const char *field_name) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, field_name);
  std::optional<float> value;
  if (lua_isnumber(L, -1) != 0) {
    value = static_cast<float>(lua_tonumber(L, -1));
  }
  lua_pop(L, 1);
  return value;
}

static std::optional<std::int32_t> ReadLuaTableIntegerField(lua_State *L, int index,
                                                            const char *field_name) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, field_name);
  std::optional<std::int32_t> value;
  if (lua_isnumber(L, -1) != 0) {
    value = static_cast<std::int32_t>(lua_tointeger(L, -1));
  }
  lua_pop(L, 1);
  return value;
}

static bool ReadLuaTableBooleanField(lua_State *L, int index, const char *field_name,
                                     const bool default_value) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, field_name);
  const bool value = lua_isnil(L, -1) != 0 ? default_value : lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return value;
}

float ResolveLuaFontStringRasterScale(lua_State *L, int font_string_index) {
  const float effective_scale =
      static_cast<float>(ComputeFrameEffectiveScale(L, font_string_index));
  const auto *manager =
      openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr) {
    return effective_scale;
  }
  return openwow::ui::game::ComputeGameUiRenderPixelScale(
      manager->screen_height(), effective_scale);
}

std::optional<openwow::render::text::TextLayout>
MeasureLuaFontStringMetrics(lua_State *L, int font_string_index) {
  font_string_index = lua_absindex(L, font_string_index);

  lua_getfield(L, font_string_index, "__ow_text");
  const char *raw_text = lua_tostring(L, -1);
  const std::string text = raw_text != nullptr ? raw_text : "";
  lua_pop(L, 1);

  const auto font_binding = ResolveEffectiveFontBinding(L, font_string_index);
  const std::string font_path = font_binding.path;
  const int font_height_pixels = font_binding.height_pixels;

  if (text.empty() || font_path.empty() || font_height_pixels <= 0) {
    return std::nullopt;
  }

  openwow::render::text::TextLayoutRequest request;
  request.maximum_width =
      ReadLuaTableNumberField(L, font_string_index, "__ow_width").value_or(0.0f);
  request.maximum_height =
      ReadLuaTableNumberField(L, font_string_index, "__ow_height").value_or(0.0f);
  request.line_spacing = openwow::ui::StoredUiHorizontalCoordinateToPixels(
      ReadLuaTableNumberField(L, font_string_index, "__ow_spacing").value_or(0.0f));
  request.line_height = openwow::ui::StoredUiHorizontalCoordinateToPixels(
      ReadLuaTableNumberField(L, font_string_index, "__ow_text_height").value_or(0.0f));
  request.maximum_lines = static_cast<std::uint32_t>(
      std::max<std::int32_t>(
          0, ReadLuaTableIntegerField(L, font_string_index, "__ow_maxlines").value_or(0)));
  request.indent_continuation_lines =
      ReadLuaTableBooleanField(L, font_string_index, "__ow_indented_wrap", false);

  const bool non_space_wrap =
      ReadLuaTableBooleanField(L, font_string_index, "__ow_nonspacewrap", false);
  const bool word_wrap =
      ReadLuaTableBooleanField(L, font_string_index, "__ow_wordwrap", true);
  request.wrap =
      openwow::render::text::ResolveWrapMode(word_wrap, non_space_wrap);

  const float effective_scale =
      ResolveLuaFontStringRasterScale(L, font_string_index);

  if (const auto *manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
      manager != nullptr && manager->vfs() != nullptr &&
      !IsAbsoluteFontPath(font_path)) {
    return openwow::ui::LayoutFontText(
        manager->vfs(), font_path, font_height_pixels, text, request,
        effective_scale);
  }

  return openwow::ui::LayoutFontText(
      nullptr, font_path, font_height_pixels, text, request, effective_scale);
}

int PushLuaFontStringGetFontResults(lua_State *L, int font_string_index) {
  font_string_index = lua_absindex(L, font_string_index);

  const auto effective_font = ResolveEffectiveFontBinding(L, font_string_index);
  if (!effective_font.path.empty()) {
    lua_pushstring(L, effective_font.path.c_str());
  } else {
    lua_pushnil(L);
  }

  lua_getfield(L, font_string_index, "__ow_text_height");
  if (lua_isnumber(L, -1) != 0) {
    const float stored_height = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    lua_pushnumber(L, openwow::ui::StoredUiHorizontalCoordinateToPixels(
                          stored_height));
  } else {
    lua_pop(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(effective_font.height_pixels));
  }

  lua_pushstring(L, effective_font.flags.c_str());
  return 3;
}

int ValidateFontStringTextSelf(lua_State *L, const char *method_name) {
  const int self = ValidateFrameObjectSelf(L, "FontString");
  if (!FontStringHasFontBinding(L, self)) {
    luaL_error(L, "%s:%s(): Font not set",
               lua_adapter::ScriptObjectDisplayName(L, self), method_name);
  }

  return self;
}

int SetTableJustifyField(lua_State *L, const char *expected_type,
                         const char *field_name, const char *method_name,
                         bool horizontal) {
  static_cast<void>(horizontal);
  const int self = ValidateFrameObjectSelf(L, expected_type);
  uint32_t flags = 0;
  const char *raw = (lua_isstring(L, 2) != 0) ? lua_tostring(L, 2) : nullptr;
  if (openwow::ui::JustifyStringToFlags(raw, &flags) == 0) {
    const char* usage_name = lua_adapter::ScriptObjectDisplayName(L, self);
    return luaL_error(L, "Usage: %s:%s(\"justify\")", usage_name, method_name);
  }

  lua_pushstring(L, openwow::ui::JustifyFlagsToString(flags));
  lua_setfield(L, self, field_name);
  if (std::strcmp(expected_type, "Font") == 0) {
    PropagateBoundFontObjectLayoutGroup(L, self);
  }
  return 0;
}

void PrependToRegions(lua_State *L, int parent_idx) {
  parent_idx = lua_absindex(L, parent_idx);
  int region_idx = lua_absindex(L, -1);

  lua_getfield(L, parent_idx, "__ow_regions");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, parent_idx, "__ow_regions");
  }
  int arr = lua_absindex(L, -1);
  lua_Integer len = luaL_len(L, arr);
  for (lua_Integer i = len; i >= 1; --i) {
    lua_geti(L, arr, i);
    lua_seti(L, arr, i + 1);
  }
  lua_pushvalue(L, region_idx);
  lua_seti(L, arr, 1);
  lua_pop(L, 1);
}

bool ArrayFieldContainsExactValue(lua_State *L, int owner_idx,
                                         const char *field_name,
                                         int value_idx) {
  owner_idx = lua_absindex(L, owner_idx);
  value_idx = lua_absindex(L, value_idx);

  lua_getfield(L, owner_idx, field_name);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }

  const int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_geti(L, array_idx, i);
    const bool matches = lua_rawequal(L, -1, value_idx) != 0;
    lua_pop(L, 1);
    if (matches) {
      lua_pop(L, 1);
      return true;
    }
  }

  lua_pop(L, 1);
  return false;
}

void RemoveExactValueFromArrayField(lua_State *L, int owner_idx,
                                           const char *field_name,
                                           int value_idx) {
  owner_idx = lua_absindex(L, owner_idx);
  value_idx = lua_absindex(L, value_idx);

  lua_getfield(L, owner_idx, field_name);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  const int source_idx = lua_absindex(L, -1);
  lua_newtable(L);
  const int replacement_idx = lua_absindex(L, -1);
  lua_Integer out_index = 1;
  const lua_Integer len = luaL_len(L, source_idx);
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_geti(L, source_idx, i);
    if (lua_rawequal(L, -1, value_idx) == 0) {
      lua_pushvalue(L, -1);
      lua_seti(L, replacement_idx, out_index++);
    }
    lua_pop(L, 1);
  }

  lua_pushvalue(L, replacement_idx);
  lua_setfield(L, owner_idx, field_name);
  lua_pop(L, 2);
}

}
