#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/frame_anchor_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_face.h"
#include "openwow/ui/game/framescript/core/frame_text_expansion.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_method_table_runtime.h"
#include "openwow/ui/game/framescript/core/frame_model_lifecycle.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/core/frame_region_state.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/framexml/layout_anchor_resolution.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/game/localization.h"
#include "openwow/input/input_manager.h"
#include "openwow/render/resources/fonts/font_string_flags.h"
#include "openwow/ui/font_asset_path.h"
#include "openwow/ui/lua_argument_readers.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/ui_coordinate_space.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/widgets/simple_font.h"
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

std::string ExpandSimpleRenderScriptText(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return {};
  }

  return openwow::game::ExpandLocalizedTextTags(
      text, openwow::game::Localization::Get().GetLocale());
}

void PushExpandedSimpleRenderScriptText(lua_State *L, const char *text) {
  const std::string expanded = ExpandSimpleRenderScriptText(text);
  lua_pushlstring(L, expanded.c_str(), expanded.size());
}

bool IsAbsoluteFontPath(const std::string &path) {
  if (std::filesystem::path(path).is_absolute()) {
    return true;
  }

  return path.size() > 2 &&
         std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
         path[1] == ':';
}

bool ValidateLuaFontObjectFace(const std::string &path,
                               const float stored_height,
                               const openwow::vfs::VirtualFileSystem *vfs) {
  if (path.empty() || !(stored_height > 0.0f)) {
    return false;
  }

  if (vfs == nullptr || IsAbsoluteFontPath(path)) {
    return openwow::ui::widgets::CSimpleFont::CanUseStoredFontFace(path, stored_height);
  }

  if (vfs->Exists(path)) {
    return true;
  }

  std::string normalized = path;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  if (vfs->Exists(normalized)) {
    return true;
  }

  const auto resolved = openwow::ui::ResolveBuiltInFontAssetPath(path);
  return !resolved.empty() && vfs->Exists(resolved);
}

int SharedSetFontWorker(lua_State *L, int self_index,
                        const char *object_name,
                        const SetFontFailurePolicy failure_policy) {
  if (!lua_isstring(L, 2) || !lua_isnumber(L, 3)) {
    luaL_error(L, "Usage: %s:SetFont(\"font\", fontHeight [, flags])",
               object_name);
  }

  const char *path = lua_tostring(L, 2);
  const double pixel_height = lua_tonumber(L, 3);

  const float stored_height =
      openwow::ui::PixelUiHorizontalCoordinateToStored(
          static_cast<float>(pixel_height));

  if (failure_policy == SetFontFailurePolicy::kFontString) {
    if (!(stored_height > std::numeric_limits<float>::epsilon())) {
      return luaL_error(
          L, "ERROR: %s:SetFont(): invalid fontHeight: %f, height must be > 0",
          object_name, static_cast<double>(stored_height));
    }
    if (path == nullptr || *path == '\0') {
      return 0;
    }
  }

  std::uint32_t flags_bits = 0;
  if (lua_isstring(L, 4)) {
    flags_bits =
        openwow::render::ParseFontFlagsString(lua_tostring(L, 4));
  }

  const auto *manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
  const auto *vfs = manager != nullptr ? manager->vfs() : nullptr;
  if (!ValidateLuaFontObjectFace(path != nullptr ? path : "",
                                 stored_height, vfs)) {
    lua_pushnil(L);
    return 1;
  }

  self_index = lua_absindex(L, self_index);

  lua_pushstring(L, path);
  lua_setfield(L, self_index, "__ow_font_path");

  lua_pushnumber(L, pixel_height);
  lua_setfield(L, self_index, "__ow_font_size");

  lua_pushnumber(L, static_cast<double>(stored_height));
  lua_setfield(L, self_index, "__ow_text_height");

  if (flags_bits != 0) {
    const auto canonical =
        openwow::render::CanonicalizeFontFlagsString(flags_bits);
    lua_pushlstring(L, canonical.c_str(), canonical.size());
  } else {
    lua_pushnil(L);
  }
  lua_setfield(L, self_index, "__ow_font_flags");

  PropagateSharedFontFaceStyle(L, self_index);

  if (openwow::ui::game::detail::GetLuaCanonicalScriptObjectType(
          L, self_index) == openwow::ui::widgets::ScriptObjectType::FontString) {
    NotifyFrameInputMutation(L, self_index, false);
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int GetRegistryMethodTableRef(lua_State *L, const char *registry_key) {
  if (L == nullptr || registry_key == nullptr) {
    return LUA_NOREF;
  }

  lua_getfield(L, LUA_REGISTRYINDEX, registry_key);
  const int ref = lua_isnumber(L, -1) != 0
                      ? static_cast<int>(lua_tointeger(L, -1))
                      : LUA_NOREF;
  lua_pop(L, 1);
  return ref;
}

constexpr const char *kFrameTypeMethodAutoRegistrationKey =
    "openwow.frame_type_method_auto_registration";

bool IsFrameTypeMethodAutoRegistrationEnabled(lua_State *L) {
  if (L == nullptr) {
    return false;
  }

  lua_getfield(L, LUA_REGISTRYINDEX, kFrameTypeMethodAutoRegistrationKey);
  const bool enabled = lua_isnil(L, -1) != 0 || lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return enabled;
}

void SetFrameTypeMethodAutoRegistrationFlag(lua_State *L, const bool enabled) {
  if (L == nullptr) {
    return;
  }

  lua_pushboolean(L, enabled ? 1 : 0);
  lua_setfield(L, LUA_REGISTRYINDEX, kFrameTypeMethodAutoRegistrationKey);
}

void SetRegistryMethodTableRef(lua_State *L, const char *registry_key, int ref) {
  lua_pushinteger(L, ref);
  lua_setfield(L, LUA_REGISTRYINDEX, registry_key);
}

bool PushRegisteredMethodTable(lua_State *L, const char *registry_key) {
  const int ref = GetRegistryMethodTableRef(L, registry_key);
  if (ref < 0) {
    return false;
  }

  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }

  return true;
}

std::string NormalizeNamedFontObjectKey(const char *name) {
  if (name == nullptr) {
    return {};
  }

  std::string key;
  key.reserve(std::strlen(name));
  for (const unsigned char ch : std::string_view(name)) {
    if (ch >= 'A' && ch <= 'Z') {
      key.push_back(static_cast<char>(ch + ('a' - 'A')));
    } else {
      key.push_back(static_cast<char>(ch));
    }
  }
  return key;
}

int EnsureNamedFontObjectRegistry(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kNamedFontObjectRegistryKey);
  if (lua_istable(L, -1) != 0) {
    return lua_absindex(L, -1);
  }

  lua_pop(L, 1);
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, kNamedFontObjectRegistryKey);
  return lua_absindex(L, -1);
}

void BindNamedFontObjectGlobalIfMissing(lua_State *L, int font_index, const char *name) {

  if (name == nullptr || *name == '\0') {
    return;
  }

  font_index = lua_absindex(L, font_index);
  (void)openwow::ui::PublishLuaGlobalValueIfNil(L, name, font_index);
}

constexpr int kUnknownFramePointSlot = std::numeric_limits<int>::max();

int FramePointSlotOrUnknown(const char *point_name) {
  if (point_name == nullptr) {
    return kUnknownFramePointSlot;
  }
  const int slot =
      openwow::ui::framexml::detail::FramePointSlotOrInvalidExact(point_name);
  return slot < 0 ? kUnknownFramePointSlot : slot;
}

int GetLuaAnchorPointSlot(lua_State *L, int anchor_index) {
  anchor_index = lua_absindex(L, anchor_index);
  runtime::GetInternedLuaField(L, anchor_index, "point");
  const char *point_name = lua_tostring(L, -1);
  const int slot = FramePointSlotOrUnknown(point_name);
  lua_pop(L, 1);
  return slot;
}

std::uint32_t GetLuaAnchorFlags(lua_State *L, int anchor_index) {
  anchor_index = lua_absindex(L, anchor_index);
  runtime::GetInternedLuaField(L, anchor_index, "__ow_flags");
  const auto flags = lua_isinteger(L, -1) != 0
                         ? static_cast<std::uint32_t>(lua_tointeger(L, -1))
                         : 0u;
  lua_pop(L, 1);
  return flags;
}

bool LuaAnchorIsHidden(lua_State *L, int anchor_index) {
  return (GetLuaAnchorFlags(L, anchor_index) & 0x800u) != 0u;
}

struct LuaAnchorRef {
  int slot{kUnknownFramePointSlot};
  lua_Integer original_index{0};
  int registry_ref{LUA_NOREF};
};

void NormalizeLuaAnchorArray(lua_State *L, int anchors_index) {
  anchors_index = lua_absindex(L, anchors_index);
  const lua_Integer len = luaL_len(L, anchors_index);
  std::vector<LuaAnchorRef> anchors;
  anchors.reserve(static_cast<std::size_t>(len));
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_rawgeti(L, anchors_index, i);
    if (lua_istable(L, -1) == 0) {
      lua_pop(L, 1);
      continue;
    }

    const int anchor_index = lua_absindex(L, -1);
    anchors.push_back(LuaAnchorRef{
        .slot = GetLuaAnchorPointSlot(L, anchor_index),
        .original_index = i,
        .registry_ref = luaL_ref(L, LUA_REGISTRYINDEX),
    });
  }

  std::stable_sort(anchors.begin(), anchors.end(),
                   [](const LuaAnchorRef &lhs, const LuaAnchorRef &rhs) {
                     if (lhs.slot != rhs.slot) {
                       return lhs.slot < rhs.slot;
                     }
                     return lhs.original_index < rhs.original_index;
                   });

  for (lua_Integer i = 1; i <= len; ++i) {
    lua_pushnil(L);
    lua_rawseti(L, anchors_index, i);
  }

  lua_Integer next = 1;
  for (const LuaAnchorRef &anchor : anchors) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, anchor.registry_ref);
    lua_rawseti(L, anchors_index, next++);
    luaL_unref(L, LUA_REGISTRYINDEX, anchor.registry_ref);
  }
}

int EnsureLuaAnchorArray(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  runtime::GetInternedLuaField(L, frame_index, "__ow_anchors");
  if (lua_istable(L, -1) != 0) {
    return lua_absindex(L, -1);
  }

  lua_pop(L, 1);
  lua_newtable(L);
  lua_pushvalue(L, -1);
  runtime::SetInternedLuaField(L, frame_index, "__ow_anchors");
  return lua_absindex(L, -1);
}

void PushAnchorRelativeToValue(lua_State *L, int anchor_index) {
  anchor_index = lua_absindex(L, anchor_index);
  runtime::GetInternedLuaField(L, anchor_index, "relativeTo");
  if (lua_isstring(L, -1) != 0) {
    const char *name = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (name != nullptr && std::strcmp(name, "UIParent") != 0) {
      lua_getglobal(L, name);
      if (lua_istable(L, -1) != 0) {
        return;
      }
      lua_pop(L, 1);
    }
    lua_pushnil(L);
    return;
  }

  if (lua_istable(L, -1) != 0) {
    lua_getglobal(L, "UIParent");
    if (lua_istable(L, -1) != 0 && lua_rawequal(L, -1, -2) != 0) {
      lua_pop(L, 2);
      lua_pushnil(L);
      return;
    }
    lua_pop(L, 1);
    return;
  }

  lua_pop(L, 1);
  lua_pushnil(L);
}

int CountVisibleLuaAnchors(lua_State *L, int anchors_index) {
  anchors_index = lua_absindex(L, anchors_index);
  const lua_Integer len = luaL_len(L, anchors_index);
  int count = 0;
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_rawgeti(L, anchors_index, i);
    if (lua_istable(L, -1) != 0 && !LuaAnchorIsHidden(L, -1)) {
      ++count;
    }
    lua_pop(L, 1);
  }
  return count;
}

bool PushVisibleLuaAnchorByIndex(lua_State *L, int anchors_index, int visible_index) {
  anchors_index = lua_absindex(L, anchors_index);
  const lua_Integer len = luaL_len(L, anchors_index);
  int count = 0;
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_rawgeti(L, anchors_index, i);
    if (lua_istable(L, -1) == 0 || LuaAnchorIsHidden(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    ++count;
    if (count == visible_index) {
      return true;
    }
    lua_pop(L, 1);
  }
  return false;
}

void SetFrameTypeMethodAutoRegistration(lua_State *L, const bool enabled) {
  SetFrameTypeMethodAutoRegistrationFlag(L, enabled);
}

bool IsLuaTableEffectivelyVisible(lua_State *L, int table_index) {
  if (L == nullptr || lua_istable(L, table_index) == 0) {
    return false;
  }

  return openwow::ui::game::detail::IsLuaWidgetEffectivelyVisible(
      L, table_index);
}

int LuaRegion_IsDragging(lua_State *L) {
  const int self_index = ValidateFrameScriptSelf(L);

  openwow::ui::game::detail::lua_pushwowbool(
      L, openwow::ui::ReadLuaBooleanFieldOrDefault(
             L, self_index, "__ow_drag_active", false));
  return 1;
}

int LuaRegion_IsMouseOver(lua_State *L) {
  const int self_index = ValidateFrameScriptSelf(L);
  auto *manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
  const char *frame_key = GetFrameRuntimeKeyOrName(L, self_index);
  const auto *rect =
      manager != nullptr && frame_key != nullptr && *frame_key != '\0'
          ? manager->retained_layout().FindRect(frame_key)
          : nullptr;
  if (rect == nullptr) {
    lua_pushboolean(L, 0);
    return 1;
  }

  const auto [mouse_x, mouse_y] =
      openwow::input::InputManager::Get().GetMousePosition();

  const auto scale = openwow::ui::ResolveDevicePixelsPerUiUnit(
      manager->screen_height(),
      static_cast<float>(ComputeFrameEffectiveScale(L, self_index)));
  const bool is_mouse_over = openwow::ui::IsCursorInsideHitRect(
      openwow::ui::DevicePixelEdgeRect{
          .left = static_cast<float>(rect->x),
          .top = static_cast<float>(rect->y),
          .right = static_cast<float>(rect->x + rect->width),
          .bottom = static_cast<float>(rect->y + rect->height),
      },
      openwow::ui::DevicePixelPoint{static_cast<float>(mouse_x),
                                    static_cast<float>(mouse_y)},
      openwow::ui::UiUnitHitInsets{
          .top = openwow::ui::ReadOptionalLuaNumberArgument(L, 2, 0.0f),
          .bottom = openwow::ui::ReadOptionalLuaNumberArgument(L, 3, 0.0f),
          .left = openwow::ui::ReadOptionalLuaNumberArgument(L, 4, 0.0f),
          .right = openwow::ui::ReadOptionalLuaNumberArgument(L, 5, 0.0f),
      },
      scale);

  lua_pushboolean(L, is_mouse_over ? 1 : 0);
  return 1;
}

void CopyMethodTableFields(lua_State *L, int source_index, int target_index) {
  source_index = lua_absindex(L, source_index);
  target_index = lua_absindex(L, target_index);

  lua_pushnil(L);
  while (lua_next(L, source_index) != 0) {
    const char *field_name =
        lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : nullptr;
    const bool copy_public_field =
        field_name != nullptr && std::strncmp(field_name, "__", 2) != 0;
    if (copy_public_field) {
      lua_pushvalue(L, -2);
      lua_pushvalue(L, -2);
      lua_settable(L, target_index);
    }
    lua_pop(L, 1);
  }
}

void SelfIndexMethodTable(lua_State *L, int table_index) {
  table_index = lua_absindex(L, table_index);
  lua_pushvalue(L, table_index);
  lua_setfield(L, table_index, "__index");
}

void ApplyRegisteredMethodTableAsMetatable(lua_State *L,
                                           const char *registry_key) {
  if (L == nullptr || lua_istable(L, -1) == 0) {
    return;
  }

  const int object_index = lua_absindex(L, -1);
  if (!PushRegisteredMethodTable(L, registry_key)) {
    return;
  }

  RemoveFunctionFieldsFromTable(L, object_index);
  lua_setmetatable(L, object_index);
}

bool TryAttachCachedMethodTableToFreshInstance(lua_State *L,
                                               const int object_index,
                                               const char *registry_key) {
  if (L == nullptr || lua_istable(L, object_index) == 0 ||
      !PushRegisteredMethodTable(L, registry_key)) {
    return false;
  }

  lua_setmetatable(L, object_index);
  return true;
}

void CacheFunctionFieldsAsMethodTable(lua_State *L,
                                      int object_index,
                                      const char *registry_key) {
  if (L == nullptr || lua_istable(L, object_index) == 0) {
    return;
  }

  object_index = lua_absindex(L, object_index);
  if (PushRegisteredMethodTable(L, registry_key)) {
    lua_pop(L, 1);
    return;
  }

  lua_newtable(L);
  const int method_table = lua_absindex(L, -1);
  lua_pushnil(L);
  while (lua_next(L, object_index) != 0) {
    const bool copy_entry =
        lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1) != 0;
    if (copy_entry) {
      lua_pushvalue(L, -2);
      lua_pushvalue(L, -2);
      lua_settable(L, method_table);
    }
    lua_pop(L, 1);
  }

  SelfIndexMethodTable(L, method_table);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  SetRegistryMethodTableRef(L, registry_key, ref);
  SetFrameTypeMethodAutoRegistrationFlag(L, true);
}

void RemoveFunctionFieldsFromTable(lua_State *L, int table_index) {
  if (L == nullptr || lua_istable(L, table_index) == 0) {
    return;
  }

  table_index = lua_absindex(L, table_index);
  std::vector<std::string> method_names;
  lua_pushnil(L);
  while (lua_next(L, table_index) != 0) {
    const char *field_name = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : nullptr;
    if (field_name != nullptr && lua_isfunction(L, -1) != 0) {
      method_names.emplace_back(field_name);
    }
    lua_pop(L, 1);
  }

  for (const std::string &field_name : method_names) {
    lua_pushnil(L);
    lua_setfield(L, table_index, field_name.c_str());
  }
}

void ApplyCachedMethodTableAndStripFunctions(lua_State *L,
                                             int object_index,
                                             const char *registry_key) {
  if (L == nullptr || lua_istable(L, object_index) == 0) {
    return;
  }

  object_index = lua_absindex(L, object_index);
  if (!PushRegisteredMethodTable(L, registry_key)) {
    return;
  }

  RemoveFunctionFieldsFromTable(L, object_index);
  lua_setmetatable(L, object_index);
}

void CopyRegisteredMethodTableFields(lua_State *L,
                                     const char *registry_key,
                                     const int target_index) {
  if (L == nullptr || lua_istable(L, target_index) == 0) {
    return;
  }

  const int target_abs_index = lua_absindex(L, target_index);
  if (!PushRegisteredMethodTable(L, registry_key)) {
    return;
  }

  CopyMethodTableFields(L, -1, target_abs_index);
  lua_pop(L, 1);
}

void UnregisterCachedMethodTable(lua_State *L, const char *registry_key) {
  if (L == nullptr) {
    return;
  }

  const int ref = GetRegistryMethodTableRef(L, registry_key);
  if (ref >= 0) {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }

  SetRegistryMethodTableRef(L, registry_key, LUA_NOREF);
}

namespace {

constexpr std::string_view kLuaParentNameToken = "$parent";

bool HasLeadingLuaParentNameToken(const std::string_view name) {
  return name.size() >= kLuaParentNameToken.size() &&
         openwow::text::EqualsIgnoreCaseAscii(
             name.substr(0, kLuaParentNameToken.size()),
             kLuaParentNameToken);
}

}

std::string ExpandLuaParentNameToken(lua_State *L, int parent_index,
                                     const char *name) {
  const std::string_view requested_name = name != nullptr ? name : "";
  if (!HasLeadingLuaParentNameToken(requested_name)) {
    return std::string(requested_name);
  }

  std::string expanded_name;
  if (L != nullptr && parent_index != 0 &&
      lua_istable(L, parent_index) != 0) {
    const int initial_top = lua_gettop(L);
    lua_pushvalue(L, lua_absindex(L, parent_index));
    for (int depth = 0; depth < 256 && lua_istable(L, -1) != 0; ++depth) {
      const int candidate_index = lua_absindex(L, -1);
      lua_getfield(L, candidate_index, "__ow_name");
      const char *candidate_name = lua_tostring(L, -1);
      if (candidate_name != nullptr && candidate_name[0] != '\0') {
        expanded_name = candidate_name;
        lua_pop(L, 1);
        break;
      }
      lua_pop(L, 1);

      lua_getfield(L, candidate_index, "__ow_parent");
      lua_remove(L, candidate_index);
    }
    lua_settop(L, initial_top);
  }

  return ExpandLuaParentNameToken(expanded_name, name);
}

std::string ExpandLuaParentNameToken(
    const std::string_view nearest_parent_name, const char *name) {
  const std::string_view requested_name = name != nullptr ? name : "";
  if (!HasLeadingLuaParentNameToken(requested_name)) {
    return std::string(requested_name);
  }

  std::string expanded_name(nearest_parent_name);
  expanded_name.append(requested_name.substr(kLuaParentNameToken.size()));
  return expanded_name;
}

const char *GetFrameRuntimeKeyOrName(lua_State *L, int frame_index) {
  const char *frame_key = runtime::BorrowInternedLuaStringField(
      L, frame_index, kLuaFrameRuntimeKeyField);
  if (frame_key != nullptr && *frame_key != '\0') {
    return frame_key;
  }

  const char *frame_name =
      runtime::BorrowInternedLuaStringField(L, frame_index, "__ow_name");
  return frame_name != nullptr && *frame_name != '\0' ? frame_name : nullptr;
}

}
