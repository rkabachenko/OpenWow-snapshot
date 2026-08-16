#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/game/framescript/core/frame_anchor_runtime.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_method_table_runtime.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/core/frame_region_state.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/framescript/xml/frame_xml_region_materializer.h"
#include "openwow/ui/game/framescript/core/frame_layout_methods.h"
#include "openwow/ui/lua_table_field.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/widgets/texture_asset_probe.h"
#include "openwow/ui/game/framescript/widgets/texture_state_methods.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/widgets/media/rotated_texture_quad.h"
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

enum class TextureGradientOrientation {
  kHorizontal = 0,
  kVertical = 1,
};

using runtime::TextureRenderStateField;
constexpr std::array<TextureRenderStateField, 4> kTextureGradientMinColorFields{{
    TextureRenderStateField::kGradientMinR,
    TextureRenderStateField::kGradientMinG,
    TextureRenderStateField::kGradientMinB,
    TextureRenderStateField::kGradientMinA,
}};
constexpr std::array<TextureRenderStateField, 4> kTextureGradientMaxColorFields{{
    TextureRenderStateField::kGradientMaxR,
    TextureRenderStateField::kGradientMaxG,
    TextureRenderStateField::kGradientMaxB,
    TextureRenderStateField::kGradientMaxA,
}};

double NormalizeStoredScriptColorComponent(double value) {
  return NormalizeScriptColorByte(QuantizeScriptColorByte(value));
}

std::optional<TextureGradientOrientation> ParseTextureGradientOrientation(
    std::string_view orientation) {
  if (openwow::text::EqualsIgnoreCaseAscii(orientation, "HORIZONTAL")) {
    return TextureGradientOrientation::kHorizontal;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(orientation, "VERTICAL")) {
    return TextureGradientOrientation::kVertical;
  }
  return std::nullopt;
}

const char *TextureGradientOrientationName(TextureGradientOrientation orientation) {
  return orientation == TextureGradientOrientation::kVertical ? "VERTICAL"
                                                             : "HORIZONTAL";
}

std::optional<std::string_view> ParseTextureBlendModeName(std::string_view mode) {
  if (openwow::text::EqualsIgnoreCaseAscii(mode, "DISABLE")) {
    return "DISABLE";
  }
  if (openwow::text::EqualsIgnoreCaseAscii(mode, "BLEND")) {
    return "BLEND";
  }
  if (openwow::text::EqualsIgnoreCaseAscii(mode, "ALPHAKEY")) {
    return "ALPHAKEY";
  }
  if (openwow::text::EqualsIgnoreCaseAscii(mode, "ADD")) {
    return "ADD";
  }
  if (openwow::text::EqualsIgnoreCaseAscii(mode, "MOD")) {
    return "MOD";
  }
  return std::nullopt;
}

bool IsTextureCoordinateInScriptRange(double value) noexcept {
  constexpr double kMinTextureCoordinate = -10000.0;
  constexpr double kMaxTextureCoordinate = 10000.0;
  return value >= kMinTextureCoordinate && value <= kMaxTextureCoordinate;
}

bool AreTextureCoordinatesInScriptRange(std::initializer_list<double> values) noexcept {
  return std::all_of(values.begin(), values.end(), IsTextureCoordinateInScriptRange);
}

void StoreTextureCoordinateRect(lua_State *L, int texture_index, double left,
                                double right, double top, double bottom) {
  texture_index = lua_absindex(L, texture_index);
  runtime::SetTextureRenderStateTexCoordQuad(L, texture_index, left, top, left,
                                             bottom, right, top, right, bottom);
  openwow::ui::WriteLuaNumberField(L, texture_index, "__ow_tc_l", left);
  openwow::ui::WriteLuaNumberField(L, texture_index, "__ow_tc_r", right);
  openwow::ui::WriteLuaNumberField(L, texture_index, "__ow_tc_t", top);
  openwow::ui::WriteLuaNumberField(L, texture_index, "__ow_tc_b", bottom);
}

void StoreTextureCoordinateQuad(lua_State *L, int texture_index, double upper_left_x,
                                double upper_left_y, double lower_left_x,
                                double lower_left_y, double upper_right_x,
                                double upper_right_y, double lower_right_x,
                                double lower_right_y) {
  texture_index = lua_absindex(L, texture_index);
  runtime::SetTextureRenderStateTexCoordQuad(
      L, texture_index, upper_left_x, upper_left_y, lower_left_x, lower_left_y,
      upper_right_x, upper_right_y, lower_right_x, lower_right_y);
  openwow::ui::WriteLuaNumberField(L, texture_index, "__ow_tc_l", upper_left_x);
  openwow::ui::WriteLuaNumberField(L, texture_index, "__ow_tc_r", upper_right_x);
  openwow::ui::WriteLuaNumberField(L, texture_index, "__ow_tc_t", upper_left_y);
  openwow::ui::WriteLuaNumberField(L, texture_index, "__ow_tc_b", lower_left_y);
}

void StoreTextureGradientColorFields(
    lua_State *L,
    int table_index,
    int first_argument_index,
    bool include_alpha,
    const std::array<TextureRenderStateField, 4> &field_names) {
  table_index = lua_absindex(L, table_index);

  const std::array<double, 4> values{{
      NormalizeStoredScriptColorComponent(
          GetScriptColorArgumentOrDefault(L, first_argument_index + 0, 0.0)),
      NormalizeStoredScriptColorComponent(
          GetScriptColorArgumentOrDefault(L, first_argument_index + 1, 0.0)),
      NormalizeStoredScriptColorComponent(
          GetScriptColorArgumentOrDefault(L, first_argument_index + 2, 0.0)),
      include_alpha
          ? NormalizeStoredScriptColorComponent(
                GetScriptColorArgumentOrDefault(L, first_argument_index + 3,
                                                0.0))
          : 1.0,
  }};

  for (std::size_t index = 0; index < values.size(); ++index) {
    runtime::SetTextureRenderStateNumber(L, table_index, field_names[index],
                                         values[index]);
  }
}

int SetTextureGradient(lua_State *L, bool include_alpha) {
  const int self_index = ValidateFrameObjectSelf(L, "Texture");
  const char *usage =
      include_alpha
          ? "Usage: %s:SetGradientAlpha(\"orientation\", minR, minG, minB, minA, maxR, maxG, maxB, maxA)"
          : "Usage: %s:SetGradient(\"orientation\", minR, minG, minB, maxR, maxG, maxB)";

  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, usage, lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  const char *orientation_name = lua_tostring(L, 2);
  const auto orientation =
      orientation_name == nullptr
          ? std::nullopt
          : ParseTextureGradientOrientation(orientation_name);
  if (!orientation.has_value()) {
    return luaL_error(L, usage, lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  runtime::SetTextureRenderStateString(
      L, self_index, TextureRenderStateField::kGradientOrientation,
      std::string_view(TextureGradientOrientationName(*orientation)));

  StoreTextureGradientColorFields(L, self_index, 3, include_alpha,
                                  kTextureGradientMinColorFields);
  StoreTextureGradientColorFields(L, self_index, include_alpha ? 7 : 6,
                                  include_alpha,
                                  kTextureGradientMaxColorFields);
  return 0;
}

int LuaTexture_SetGradient(lua_State *L) {
  return SetTextureGradient(L, false);
}

int LuaTexture_SetGradientAlpha(lua_State *L) {
  return SetTextureGradient(L, true);
}

const openwow::vfs::VirtualFileSystem *GetTextureVfsFromLua(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX,
               openwow::ui::game::detail::kTextureVfsRegistryKey);
  auto *vfs = static_cast<const openwow::vfs::VirtualFileSystem *>(
      lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (vfs != nullptr) {
    return vfs;
  }
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.world_ui_runtime_context");
  auto *manager = static_cast<openwow::ui::game::runtime::WorldUiRuntimeContext *>(
      lua_touserdata(L, -1));
  lua_pop(L, 1);
  return manager != nullptr ? manager->vfs() : nullptr;
}

constexpr const char *kTextureValidationCacheRegistryKey =
    "openwow.game.texture_validation_cache";
constexpr const char *kTextureValidationCacheEntriesField = "entries";
constexpr const char *kTextureValidationCacheVfsField = "vfs";
constexpr const char *kTextureValidationCacheRevisionField = "revision";
constexpr const char *kTextureValidationRequestsField = "requests";
constexpr const char *kTextureValidationHitsField = "hits";
constexpr const char *kTextureValidationReadsField = "reads";

void SetTextureValidationCounter(lua_State *L, const int cache_index,
                                 const char *field,
                                 const std::uint64_t value) {
  lua_pushinteger(L, static_cast<lua_Integer>(value));
  lua_setfield(L, cache_index, field);
}

std::uint64_t GetTextureValidationCounter(lua_State *L,
                                          const int cache_index,
                                          const char *field) {
  lua_getfield(L, cache_index, field);
  const auto value = lua_isnumber(L, -1) != 0
                         ? static_cast<std::uint64_t>(lua_tointeger(L, -1))
                         : 0u;
  lua_pop(L, 1);
  return value;
}

void IncrementTextureValidationCounter(lua_State *L, const int cache_index,
                                       const char *field) {
  SetTextureValidationCounter(
      L, cache_index, field,
      GetTextureValidationCounter(L, cache_index, field) + 1u);
}

int PushNewTextureValidationCache(
    lua_State *L, const openwow::vfs::VirtualFileSystem *const vfs) {
  lua_newtable(L);
  const int cache_index = lua_absindex(L, -1);

  lua_setnativestate(L, cache_index);
  lua_pushlightuserdata(
      L, const_cast<openwow::vfs::VirtualFileSystem *>(vfs));
  lua_setfield(L, cache_index, kTextureValidationCacheVfsField);
  lua_pushinteger(L, static_cast<lua_Integer>(
                         vfs != nullptr ? vfs->lookup_revision() : 0u));
  lua_setfield(L, cache_index, kTextureValidationCacheRevisionField);
  lua_newtable(L);
  lua_setnativestate(L, -1);
  lua_setfield(L, cache_index, kTextureValidationCacheEntriesField);
  SetTextureValidationCounter(L, cache_index,
                              kTextureValidationRequestsField, 0u);
  SetTextureValidationCounter(L, cache_index,
                              kTextureValidationHitsField, 0u);
  SetTextureValidationCounter(L, cache_index,
                              kTextureValidationReadsField, 0u);
  lua_pushvalue(L, cache_index);
  lua_setfield(L, LUA_REGISTRYINDEX,
               kTextureValidationCacheRegistryKey);
  return cache_index;
}

int PushTextureValidationCache(
    lua_State *L, const openwow::vfs::VirtualFileSystem *const vfs) {
  lua_getfield(L, LUA_REGISTRYINDEX,
               kTextureValidationCacheRegistryKey);
  if (lua_istable(L, -1) != 0) {
    const int cache_index = lua_absindex(L, -1);
    lua_getfield(L, cache_index, kTextureValidationCacheVfsField);
    const auto *cached_vfs =
        static_cast<const openwow::vfs::VirtualFileSystem *>(
            lua_touserdata(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, cache_index, kTextureValidationCacheRevisionField);
    const auto cached_revision =
        lua_isnumber(L, -1) != 0
            ? static_cast<std::uint64_t>(lua_tointeger(L, -1))
            : 0u;
    lua_pop(L, 1);
    const auto current_revision =
        vfs != nullptr ? vfs->lookup_revision() : 0u;
    if (cached_vfs == vfs && cached_revision == current_revision) {
      return cache_index;
    }
  }

  lua_pop(L, 1);
  return PushNewTextureValidationCache(L, vfs);
}

std::string NormalizeTextureValidationKey(
    const std::string &path_sans_ext) {
  std::string normalized = path_sans_ext;
  for (char &ch : normalized) {
    if (ch == '\\') {
      ch = '/';
    } else {
      ch = static_cast<char>(
          std::tolower(static_cast<unsigned char>(ch)));
    }
  }
  return normalized;
}

bool TryValidateTextureInVfs(lua_State *L,
                             const openwow::vfs::VirtualFileSystem &vfs,
                             const std::string &path_sans_ext) {
  const int initial_top = lua_gettop(L);
  const int cache_index = PushTextureValidationCache(L, &vfs);
  IncrementTextureValidationCounter(
      L, cache_index, kTextureValidationRequestsField);

  lua_getfield(L, cache_index, kTextureValidationCacheEntriesField);
  const int entries_index = lua_absindex(L, -1);
  const std::string normalized =
      NormalizeTextureValidationKey(path_sans_ext);
  lua_getfield(L, entries_index, normalized.c_str());
  if (lua_isboolean(L, -1) != 0) {
    const bool valid = lua_toboolean(L, -1) != 0;
    IncrementTextureValidationCounter(
        L, cache_index, kTextureValidationHitsField);
    lua_settop(L, initial_top);
    return valid;
  }
  lua_pop(L, 1);

  bool valid = false;
  for (const char *ext : {".blp", ".tga"}) {
    const std::string candidate = normalized + ext;

    if (!vfs.Exists(candidate)) {
      continue;
    }
    IncrementTextureValidationCounter(
        L, cache_index, kTextureValidationReadsField);
    constexpr std::size_t kTextureHeaderProbeBytes = 18u;
    const auto bytes =
        vfs.ReadFilePrefix(candidate, kTextureHeaderProbeBytes);
    if (!bytes.has_value() || bytes->empty()) {
      continue;
    }

    if (std::strcmp(ext, ".blp") == 0) {

      if (bytes->size() >= 8 &&
          bytes->at(0) == 'B' && bytes->at(1) == 'L' &&
          bytes->at(2) == 'P' &&
          (bytes->at(3) == '2' || bytes->at(3) == '1')) {
        valid = true;
        break;
      }
    } else {

      if (bytes->size() >= 18) {
        const auto image_type = bytes->at(2);
        if (image_type <= 3 || image_type == 9 ||
            image_type == 10 || image_type == 11) {
          valid = true;
          break;
        }
      }
    }
  }

  lua_pushboolean(L, valid ? 1 : 0);
  lua_setfield(L, entries_index, normalized.c_str());
  lua_settop(L, initial_top);
  return valid;
}

TextureValidationPerformanceCounters
GetTextureValidationPerformanceCounters(lua_State *L) {
  if (L == nullptr) {
    return {};
  }
  const int initial_top = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX,
               kTextureValidationCacheRegistryKey);
  TextureValidationPerformanceCounters counters;
  if (lua_istable(L, -1) != 0) {
    const int cache_index = lua_absindex(L, -1);
    counters.requests = GetTextureValidationCounter(
        L, cache_index, kTextureValidationRequestsField);
    counters.cache_hits = GetTextureValidationCounter(
        L, cache_index, kTextureValidationHitsField);
    counters.source_reads = GetTextureValidationCounter(
        L, cache_index, kTextureValidationReadsField);
  }
  lua_settop(L, initial_top);
  return counters;
}

void ResetTextureValidationPerformanceCounters(lua_State *L) {
  if (L == nullptr) {
    return;
  }
  const int initial_top = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX,
               kTextureValidationCacheRegistryKey);
  if (lua_istable(L, -1) != 0) {
    const int cache_index = lua_absindex(L, -1);
    SetTextureValidationCounter(L, cache_index,
                                kTextureValidationRequestsField, 0u);
    SetTextureValidationCounter(L, cache_index,
                                kTextureValidationHitsField, 0u);
    SetTextureValidationCounter(L, cache_index,
                                kTextureValidationReadsField, 0u);
  }
  lua_settop(L, initial_top);
}

static void ApplyTextureIdentityMethods(lua_State *L,
                                        const int texture_index) {
  const int texture = lua_absindex(L, texture_index);
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "Texture");
    lua_pushstring(Ls, "Texture");
    return 1;
  }, 0);
  lua_setfield(L, texture, "GetObjectType");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Texture");
    lua_getfield(Ls, self, "__ow_name");
    const char* name = lua_tostring(Ls, -1);
    if (name != nullptr && *name != '\0') {
      return 1;
    }
    lua_pop(Ls, 1);
    lua_pushnil(Ls);
    return 1;
  }, 0);
  lua_setfield(L, texture, "GetName");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Texture");
    if (lua_isstring(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:IsObjectType(\"TYPE\")",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }
    const char* query = lua_tostring(Ls, 2);
    if (openwow::text::EqualsIgnoreCaseAscii(query, "Texture") ||
        openwow::text::EqualsIgnoreCaseAscii(query, "Region") ||
        openwow::text::EqualsIgnoreCaseAscii(query, "Object")) {
      lua_pushnumber(Ls, 1);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, texture, "IsObjectType");
}

void CreateTextureTable(lua_State *L, int parent_idx) {
  const bool has_parent =
      parent_idx != 0 && lua_istable(L, parent_idx) != 0;
  parent_idx = has_parent ? lua_absindex(L, parent_idx) : 0;
  lua_newtable(L);
  int tx = lua_absindex(L, -1);

  lua_pushstring(L, "Texture");
  lua_setfield(L, tx, "__ow_type");
  openwow::ui::game::lua_adapter::AttachScriptObjectIdentity(L, tx);

  lua_pushboolean(L, 1);
  lua_setfield(L, tx, "__ow_visible");
  if (has_parent) {
    lua_pushvalue(L, parent_idx);
    lua_setfield(L, tx, "__ow_parent");
  }

  if (TryAttachCachedMethodTableToFreshInstance(
          L, tx, kTextureMethodTableRegistryKey)) {
    if (has_parent) {
      PrependToRegions(L, parent_idx);
    }
    SyncRegionDrawLayerEnabled(L, tx);
    return;
  }

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }

    if (lua_isnumber(Ls, 2)) {

      runtime::SetTextureRenderStateNumber(
          Ls, 1, TextureRenderStateField::kSolidColorR,
          NormalizeStoredScriptColorComponent(
              GetScriptColorArgumentOrDefault(Ls, 2, 0.0)));
      runtime::SetTextureRenderStateNumber(
          Ls, 1, TextureRenderStateField::kSolidColorG,
          NormalizeStoredScriptColorComponent(
              GetScriptColorArgumentOrDefault(Ls, 3, 0.0)));
      runtime::SetTextureRenderStateNumber(
          Ls, 1, TextureRenderStateField::kSolidColorB,
          NormalizeStoredScriptColorComponent(
              GetScriptColorArgumentOrDefault(Ls, 4, 0.0)));
      runtime::SetTextureRenderStateNumber(
          Ls, 1, TextureRenderStateField::kSolidColorA,
          NormalizeStoredScriptColorComponent(
              GetScriptColorArgumentOrDefault(Ls, 5, 1.0)));

      runtime::SetTextureRenderStateString(
          Ls, 1, TextureRenderStateField::kTexture, std::string_view{});
      runtime::SetTextureRenderStateBoolean(
          Ls, 1, TextureRenderStateField::kTextureCleared, false);
      lua_pushnumber(Ls, 1);
      return 1;
    }

    if (lua_isstring(Ls, 2)) {
      const char *raw_path = lua_tostring(Ls, 2);
      if (raw_path == nullptr || *raw_path == '\0') {

        runtime::SetTextureRenderStateString(
            Ls, 1, TextureRenderStateField::kTexture, std::nullopt);
        runtime::SetTextureRenderStateBoolean(
            Ls, 1, TextureRenderStateField::kTextureCleared, true);
        lua_pushnumber(Ls, 1);
        return 1;
      }

      const std::string path_sans_ext =
          openwow::data::StripTexture3CharExtension(raw_path);

      lua_getfield(Ls, 1, "__ow_texture");
      if (lua_isstring(Ls, -1)) {
        const char *existing = lua_tostring(Ls, -1);
        if (existing != nullptr &&
            openwow::data::TexturePathMatchesIgnoreCaseSansExt(
                existing, path_sans_ext)) {
          lua_pop(Ls, 1);
          lua_pushnumber(Ls, 1);
          return 1;
        }
      }
      lua_pop(Ls, 1);

      const auto *vfs = GetTextureVfsFromLua(Ls);
      if (vfs != nullptr &&
          !TryValidateTextureInVfs(Ls, *vfs, path_sans_ext)) {

        lua_pushnil(Ls);
        return 1;
      }

      runtime::SetTextureRenderStateString(
          Ls, 1, TextureRenderStateField::kTexture,
          std::string_view(path_sans_ext));
      runtime::SetTextureRenderStateBoolean(
          Ls, 1, TextureRenderStateField::kTextureCleared, false);

      QueueRegionTextureLoad(Ls, path_sans_ext);
      lua_pushnumber(Ls, 1);
      return 1;
    }

    runtime::SetTextureRenderStateString(
        Ls, 1, TextureRenderStateField::kTexture, std::nullopt);
    runtime::SetTextureRenderStateBoolean(
        Ls, 1, TextureRenderStateField::kTextureCleared, true);
    lua_pushnumber(Ls, 1);
    return 1;
  }, 0);
  lua_setfield(L, tx, "SetTexture");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_texture");
    if (lua_isstring(Ls, -1)) {
      std::size_t len = 0;
      lua_tolstring(Ls, -1, &len);
      if (len == 0) {
        lua_pop(Ls, 1);
        lua_pushnil(Ls);
      }
    }
    return 1;
  }, 0);
  lua_setfield(L, tx, "GetTexture");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "Texture");
    const int argument_count = lua_gettop(Ls);
    if (argument_count == 5) {
      const double left = luaL_checknumber(Ls, 2);
      const double right = luaL_checknumber(Ls, 3);
      const double top = luaL_checknumber(Ls, 4);
      const double bottom = luaL_checknumber(Ls, 5);
      if (!AreTextureCoordinatesInScriptRange({left, right, top, bottom})) {
        return luaL_error(Ls, "TexCoord out of range");
      }
      StoreTextureCoordinateRect(Ls, self_index, left, right, top, bottom);
      return 0;
    }

    if (argument_count == 9) {

      const double upper_left_x = luaL_checknumber(Ls, 2);
      const double upper_left_y = luaL_checknumber(Ls, 3);
      const double lower_left_x = luaL_checknumber(Ls, 4);
      const double lower_left_y = luaL_checknumber(Ls, 5);
      const double upper_right_x = luaL_checknumber(Ls, 6);
      const double upper_right_y = luaL_checknumber(Ls, 7);
      const double lower_right_x = luaL_checknumber(Ls, 8);
      const double lower_right_y = luaL_checknumber(Ls, 9);
      if (!AreTextureCoordinatesInScriptRange(
              {upper_left_x, upper_left_y, lower_left_x, lower_left_y,
               upper_right_x, upper_right_y, lower_right_x, lower_right_y})) {
        return luaL_error(Ls, "TexCoord out of range");
      }
      StoreTextureCoordinateQuad(Ls, self_index, upper_left_x, upper_left_y,
                                 lower_left_x, lower_left_y, upper_right_x,
                                 upper_right_y, lower_right_x, lower_right_y);
      return 0;
    }

    return luaL_error(
        Ls,
        "Usage: %s:SetTexCoord(minX, maxX, minY, maxY) or SetTexCoord(ULx, ULy, LLx, LLy, URx, URy, LRx, LRy)",
        lua_adapter::ScriptObjectDisplayName(Ls, self_index));
  }, 0);
  lua_setfield(L, tx, "SetTexCoord");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      const double r = GetScriptColorArgumentOrDefault(Ls, 2, 1.0);
      const double g = GetScriptColorArgumentOrDefault(Ls, 3, 1.0);
      const double b = GetScriptColorArgumentOrDefault(Ls, 4, 1.0);
      const std::uint8_t alpha_byte =
          lua_isnumber(Ls, 5) != 0
              ? QuantizeScriptColorByte(lua_tonumber(Ls, 5))
              : ReadTextureAlphaByteOrDefault(Ls, 1);
      const double normalized_red =
          NormalizeScriptColorByte(QuantizeScriptColorByte(r));
      const double normalized_green =
          NormalizeScriptColorByte(QuantizeScriptColorByte(g));
      const double normalized_blue =
          NormalizeScriptColorByte(QuantizeScriptColorByte(b));
      const double normalized_alpha = NormalizeScriptColorByte(alpha_byte);

      runtime::SetTextureRenderStateNumber(
          Ls, 1, TextureRenderStateField::kVertexColorR, normalized_red);
      runtime::SetTextureRenderStateNumber(
          Ls, 1, TextureRenderStateField::kVertexColorG, normalized_green);
      runtime::SetTextureRenderStateNumber(
          Ls, 1, TextureRenderStateField::kVertexColorB, normalized_blue);
      StoreTextureAlphaByte(Ls, 1, alpha_byte);
      SyncTrackedTextureColor(
          Ls, 1, static_cast<float>(normalized_red),
          static_cast<float>(normalized_green),
          static_cast<float>(normalized_blue),
          static_cast<float>(normalized_alpha));
    }
    return 0;
  }, 0);
  lua_setfield(L, tx, "SetVertexColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 1);
      lua_pushnumber(Ls, 1);
      lua_pushnumber(Ls, 1);
      lua_pushnumber(Ls, 1);
      return 4;
    }
    PushPackedColor(Ls, 1, kTextureVertexColorFieldNames, kTextureVertexColorDefaults);
    return 4;
  }, 0);
  lua_setfield(L, tx, "GetVertexColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaSetPointInternal(Ls, LuaAnchorTargetValidation::kRequireScriptObjectThis);
  }, 0);
  lua_setfield(L, tx, "SetPoint");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaClearAllPointsInternal(Ls, LuaAnchorTargetValidation::kRequireScriptObjectThis);
  }, 0);
  lua_setfield(L, tx, "ClearAllPoints");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaSetAllPointsInternal(Ls, LuaAnchorTargetValidation::kRequireScriptObjectThis);
  }, 0);
  lua_setfield(L, tx, "SetAllPoints");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetTypedLuaScriptRegionShown(Ls, "Texture", true);
  }, 0);
  lua_setfield(L, tx, "Show");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetTypedLuaScriptRegionShown(Ls, "Texture", false);
  }, 0);
  lua_setfield(L, tx, "Hide");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_visible");
    const bool shown = lua_isboolean(Ls, -1) ? (lua_toboolean(Ls, -1) != 0) : true;
    lua_pop(Ls, 1);
    if (shown)
      lua_pushnumber(Ls, 1);
    else
      lua_pushnil(Ls);
    return 1;
  }, 0);
  lua_setfield(L, tx, "IsShown");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    if (!IsLuaTableEffectivelyVisible(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_pushnumber(Ls, 1);
    return 1;
  }, 0);
  lua_setfield(L, tx, "IsVisible");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetLuaRegionDimension(Ls, "SetWidth", "width", "__ow_width");
  }, 0);
  lua_setfield(L, tx, "SetWidth");

  lua_pushcfunction(L, LuaRegion_GetWidth);
  lua_setfield(L, tx, "GetWidth");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetLuaRegionDimension(Ls, "SetHeight", "height", "__ow_height");
  }, 0);
  lua_setfield(L, tx, "SetHeight");

  lua_pushcfunction(L, LuaRegion_GetHeight);
  lua_setfield(L, tx, "GetHeight");

  lua_pushcfunction(L, LuaRegion_GetSize);
  lua_setfield(L, tx, "GetSize");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      if (lua_isnumber(Ls, 2) == 0) {
        return luaL_error(Ls, "Usage: %s:SetAlpha(alpha)",
                          lua_adapter::ScriptObjectDisplayName(Ls, 1));
      }
      StoreTextureAlphaByte(
          Ls, 1,
          openwow::ui::game::QuantizeFrameAlphaByteTruncated(lua_tonumber(Ls, 2)));
    }
    return 0;
  }, 0);
  lua_setfield(L, tx, "SetAlpha");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushnumber(
          Ls,
          openwow::ui::game::NormalizeFrameAlphaByte(ReadTextureAlphaByteOrDefault(Ls, 1)));
      return 1;
    }
    lua_pushnumber(Ls, 1);
    return 1;
  }, 0);
  lua_setfield(L, tx, "GetAlpha");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "Texture");
    if (lua_isstring(Ls, 2) != 0) {
      std::size_t length = 0;
      const char *raw_mode = lua_tolstring(Ls, 2, &length);
      const auto mode =
          raw_mode != nullptr
              ? ParseTextureBlendModeName(std::string_view(raw_mode, length))
              : std::nullopt;
      if (mode.has_value()) {
        runtime::SetTextureRenderStateString(
            Ls, self_index, TextureRenderStateField::kBlend, *mode);
        return 0;
      }
    }
    return luaL_error(Ls, "Usage: %s:SetBlendMode(\"mode\")",
                      lua_adapter::ScriptObjectDisplayName(Ls, self_index));
  }, 0);
  lua_setfield(L, tx, "SetBlendMode");

  ApplyTextureIdentityMethods(L, tx);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const bool desaturated = detail::ScriptReadBoolArgOrDefault(Ls, 2, true);
    const bool supported = detail::TextureStateSupported(Ls, desaturated);
    if (lua_istable(Ls, 1)) {
      runtime::SetTextureRenderStateBoolean(
          Ls, 1, TextureRenderStateField::kDesaturated,
          supported && desaturated);
    }
    if (supported) {
      lua_pushnumber(Ls, 1);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, tx, "SetDesaturated");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_getfield(Ls, 1, "__ow_desat");
      const bool desat = lua_toboolean(Ls, -1) != 0;
      lua_pop(Ls, 1);
      if (desat && detail::TextureStateSupported(Ls, true)) {
        lua_pushnumber(Ls, 1);
      } else {
        lua_pushnil(Ls);
      }
      return 1;
    }
    lua_pushnil(Ls);
    return 1;
  }, 0);
  lua_setfield(L, tx, "IsDesaturated");

  lua_pushcfunction(L, LuaTexture_SetGradient);
  lua_setfield(L, tx, "SetGradient");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;
    const double angle = luaL_optnumber(Ls, 2, 0);
    lua_pushnumber(Ls, angle);
    lua_setfield(Ls, 1, "__ow_rotation");
    double cx = 0.5;
    double cy = 0.5;
    if (lua_isnumber(Ls, 3) && lua_isnumber(Ls, 4)) {
      cx = lua_tonumber(Ls, 3);
      cy = lua_tonumber(Ls, 4);
    }
    const auto rotated_uvs = openwow::ui::media::ComputeRotatedTextureQuad(
        static_cast<float>(angle), static_cast<float>(cx),
        static_cast<float>(cy));
    runtime::SetTextureRenderStateTexCoordQuad(
        Ls, 1, rotated_uvs.upper_left_u, rotated_uvs.upper_left_v,
        rotated_uvs.lower_left_u, rotated_uvs.lower_left_v,
        rotated_uvs.upper_right_u, rotated_uvs.upper_right_v,
        rotated_uvs.lower_right_u, rotated_uvs.lower_right_v);
    return 0;
  }, 0);
  lua_setfield(L, tx, "SetRotation");

  lua_pushcclosure(L, [](lua_State *Ls) -> int { return SetLuaRegionSize(Ls); }, 0);
  lua_setfield(L, tx, "SetSize");

  openwow::ui::anim::ApplyAnimationRegionMethods(L);
  ApplyTextureStateMethods(L, tx);
  lua_pushvalue(L, tx);
  ApplyLayoutFrameMethods(L);
  lua_pop(L, 1);

  ApplyTextureIdentityMethods(L, tx);

  ApplyCachedMethodTableAndStripFunctions(L, tx, kTextureMethodTableRegistryKey);

  if (has_parent) {
    PrependToRegions(L, parent_idx);
  }
  SyncRegionDrawLayerEnabled(L, tx);
}

}
