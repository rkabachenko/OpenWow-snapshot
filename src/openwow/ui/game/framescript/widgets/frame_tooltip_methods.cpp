#include "openwow/ui/game/framescript/widgets/frame_tooltip_methods.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/xml/frame_xml_region_materializer.h"

#include "openwow/foundation/text/ascii.h"
#include "openwow/game/commerce/merchants/adapters/lua/merchant_lua_api.h"
#include "openwow/game/localization.h"
#include "openwow/game/tracking_system.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/tooltip_lua_adapter.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_c_api_convenience.h"

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
#include <utility>
#include <vector>

#include "openwow/ui/game/tooltip_frame_sync.h"

namespace openwow::ui::game::frame_api {

constexpr const char *kTooltipFontStringPairsField = "__ow_tooltip_fontstring_pairs";
constexpr const char *kTooltipUsedLineCountField = "__ow_tooltip_used_line_count";
constexpr const char *kTooltipOwnerFrameField = "__ow_tooltip_owner_frame";
constexpr const char *kTooltipOwnerAnchorField = "__ow_tooltip_owner_anchor";
constexpr const char *kTooltipOwnerOffsetXField = "__ow_tooltip_owner_offset_x";
constexpr const char *kTooltipOwnerOffsetYField = "__ow_tooltip_owner_offset_y";
constexpr const char *kTooltipCursorPixelScaleField =
    "__ow_tooltip_cursor_pixel_scale";
constexpr const char *kTooltipClearGenerationField = "__ow_tooltip_clear_generation";
constexpr const char *kTooltipPresentationRevisionField = "__ow_tooltip_presentation_revision";
constexpr const char* kTooltipUsedTextureCountField =
    "__ow_tooltip_used_texture_count";
constexpr const char *kTooltipPairLeftField = "left";
constexpr const char *kTooltipPairRightField = "right";
constexpr float kTooltipFramePadding = 10.24F;
constexpr float kTooltipLineSpacing = 2.0F;
constexpr float kTooltipDoubleColumnSpacing = 38.4F;
constexpr float kTooltipMaximumWrappedTextWidth = 230.4F;
constexpr float kTooltipTextureTextSpacing = 5.12F;
constexpr int kTooltipMaximumTextures = 10;
constexpr float kGameTooltipGoldR = 1.0F;
constexpr float kGameTooltipGoldG = 210.0F / 255.0F;
constexpr float kGameTooltipGoldB = 0.0F;

struct TooltipLuaTextStyle {
  float r{1.0f};
  float g{1.0f};
  float b{1.0f};
  bool wrap{false};
};

const char *CanonicalTooltipAnchorType(const char *anchor);

float ConvertTooltipScriptPixelsToStoredCoordinate(const float pixels) {
  return openwow::ui::PixelUiHorizontalCoordinateToStored(pixels);
}

float ReadTooltipOptionalOffset(lua_State* L, const int argument_index) {

  return lua_isnumber(L, argument_index) != 0
             ? static_cast<float>(lua_tonumber(L, argument_index))
             : 0.0F;
}

int ValidateTooltipOwnerFrame(lua_State *L, const int tooltip_index) {
  if (lua_istable(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:SetOwner(frame)",
                      lua_adapter::ScriptObjectDisplayName(L, tooltip_index));
  }
  if (lua_rawequal(L, tooltip_index, 2) != 0) {
    return luaL_error(L, "%s:SetOwner(): Can't set owner to self",
                      lua_adapter::ScriptObjectDisplayName(L, tooltip_index));
  }
  if (!openwow::ui::game::detail::LuaScriptObjectIsKindOfCanonicalType(
          L, 2, openwow::ui::widgets::ScriptObjectType::Frame)) {
    return luaL_error(L, "%s:SetOwner(): Wrong object type, expected frame",
                      lua_adapter::ScriptObjectDisplayName(L, tooltip_index));
  }
  return lua_absindex(L, 2);
}

float ReadTooltipLocalNumber(lua_State *L, int tooltip_index, const char *field) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_getfield(L, tooltip_index, field);
  const float value = lua_isnumber(L, -1) != 0 ? static_cast<float>(lua_tonumber(L, -1)) : 0.0F;
  lua_pop(L, 1);
  return value;
}

bool ReadTooltipLocalBoolean(lua_State *L, int tooltip_index, const char *field) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_getfield(L, tooltip_index, field);
  const bool value = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return value;
}

bool TooltipHasOwner(lua_State *L, int tooltip_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_getfield(L, tooltip_index, kTooltipOwnerFrameField);
  const bool has_owner = lua_istable(L, -1) != 0;
  lua_pop(L, 1);
  return has_owner;
}

void SyncTooltipClearGeneration(lua_State *L, int tooltip_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  const auto generation = openwow::ui::game::TooltipSystem::Get().GetClearGeneration();
  lua_getfield(L, tooltip_index, kTooltipClearGenerationField);
  const auto observed = lua_isnumber(L, -1) != 0 ? static_cast<std::uint64_t>(lua_tointeger(L, -1))
                                                 : std::numeric_limits<std::uint64_t>::max();
  lua_pop(L, 1);
  if (observed == generation) {
    return;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(generation));
  lua_setfield(L, tooltip_index, kTooltipClearGenerationField);
}

void SetTooltipUsedLineCount(lua_State *L, int tooltip_index, int count) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_pushinteger(L, static_cast<lua_Integer>(count));
  lua_setfield(L, tooltip_index, kTooltipUsedLineCountField);
}

int GetTooltipUsedLineCount(lua_State *L, int tooltip_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_getfield(L, tooltip_index, kTooltipUsedLineCountField);
  const int count =
      lua_isnumber(L, -1) != 0 ? std::max(0, static_cast<int>(lua_tointeger(L, -1))) : 0;
  lua_pop(L, 1);
  return count;
}

int EnsureTooltipFontStringPairsTable(lua_State *L, int tooltip_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_getfield(L, tooltip_index, kTooltipFontStringPairsField);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, tooltip_index, kTooltipFontStringPairsField);
  }
  return lua_absindex(L, -1);
}

void SetTooltipFontStringState(lua_State *L, int font_string_index, const char *text,
                               const TooltipLuaTextStyle &style, bool visible) {
  font_string_index = lua_absindex(L, font_string_index);

  lua_pushstring(L, text != nullptr ? text : "");
  lua_setfield(L, font_string_index, "__ow_text");

  lua_pushnumber(L, style.r);
  lua_setfield(L, font_string_index, "__ow_text_r");
  lua_pushnumber(L, style.g);
  lua_setfield(L, font_string_index, "__ow_text_g");
  lua_pushnumber(L, style.b);
  lua_setfield(L, font_string_index, "__ow_text_b");
  lua_pushnumber(L, 1.0);
  lua_setfield(L, font_string_index, "__ow_text_a");

  lua_pushboolean(L, visible ? 1 : 0);
  lua_setfield(L, font_string_index, "__ow_visible");
  lua_pushboolean(L, style.wrap ? 1 : 0);
  lua_setfield(L, font_string_index, "__ow_wordwrap");
}

void ClearTooltipFontStringState(lua_State *L, int font_string_index) {
  font_string_index = lua_absindex(L, font_string_index);
  SetTooltipFontStringState(L, font_string_index, "", TooltipLuaTextStyle{}, false);
  lua_pushnumber(L, 0.0);
  lua_setfield(L, font_string_index, "__ow_width");
  MarkLuaFontStringDimensionFromLayout(L, font_string_index, "__ow_width", false);
  lua_pushnumber(L, 0.0);
  lua_setfield(L, font_string_index, "__ow_height");
  MarkLuaFontStringDimensionFromLayout(L, font_string_index, "__ow_height", false);
}

void StoreTooltipAnchor(lua_State *L, int target_index, const char *point, int relative_index,
                        const char *relative_point, float offset_x, float offset_y) {
  target_index = lua_absindex(L, target_index);
  relative_index = relative_index != 0 ? lua_absindex(L, relative_index) : 0;

  lua_newtable(L);
  const int anchors_index = lua_absindex(L, -1);
  lua_newtable(L);
  const int anchor_index = lua_absindex(L, -1);
  lua_pushstring(L, point);
  lua_setfield(L, anchor_index, "point");
  if (relative_index != 0) {
    lua_pushvalue(L, relative_index);
    lua_setfield(L, anchor_index, "relativeTo");
  }
  lua_pushstring(L, relative_point);
  lua_setfield(L, anchor_index, "relativePoint");
  lua_pushnumber(L, offset_x);
  lua_setfield(L, anchor_index, "x");
  lua_pushnumber(L, offset_y);
  lua_setfield(L, anchor_index, "y");
  lua_rawseti(L, anchors_index, 1);
  lua_setfield(L, target_index, "__ow_anchors");
  lua_pushnil(L);
  lua_setfield(L, target_index, "__ow_setAllPoints");
  openwow::ui::game::detail::ReindexLuaAnchorDependents(L, target_index);
}

bool TooltipFontStringIsVisible(lua_State *L, int font_string_index) {
  font_string_index = lua_absindex(L, font_string_index);
  lua_getfield(L, font_string_index, "__ow_visible");
  const bool visible = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return visible;
}

struct TooltipFontStringMetrics {
  float width{0.0F};
  float height{0.0F};
};

TooltipFontStringMetrics MeasureTooltipFontString(lua_State *L, int font_string_index) {
  const auto measured = MeasureLuaFontStringMetrics(L, font_string_index);
  if (!measured.has_value()) {
    return {};
  }
  return {
      .width = std::max(0.0F, measured->width),
      .height = std::max(0.0F, measured->height),
  };
}

void ClearTooltipAnchors(lua_State* L, int tooltip_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_newtable(L);
  lua_setfield(L, tooltip_index, "__ow_anchors");
  lua_pushnil(L);
  lua_setfield(L, tooltip_index, "__ow_setAllPoints");
  openwow::ui::game::detail::ReindexLuaAnchorDependents(L, tooltip_index);
}

void ApplyTooltipOwnerAnchor(lua_State *L, int tooltip_index,
                             const bool reset_anchor_none = false) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_getfield(L, tooltip_index, kTooltipOwnerFrameField);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }
  const int owner_index = lua_absindex(L, -1);

  const char *anchor =
      openwow::ui::BorrowRawLuaStringField(L, tooltip_index, kTooltipOwnerAnchorField);
  anchor = CanonicalTooltipAnchorType(anchor);
  if (std::strcmp(anchor, "ANCHOR_PRESERVE") == 0) {
    lua_pop(L, 1);
    return;
  }
  if (std::strcmp(anchor, "ANCHOR_NONE") == 0) {

    if (reset_anchor_none) {
      ClearTooltipAnchors(L, tooltip_index);
    }
    lua_pop(L, 1);
    return;
  }

  const auto read_offset = [&](const char *field) {
    lua_getfield(L, tooltip_index, field);
    const float stored = lua_isnumber(L, -1) != 0 ? static_cast<float>(lua_tonumber(L, -1)) : 0.0F;
    lua_pop(L, 1);
    return openwow::ui::StoredUiHorizontalCoordinateToPixels(stored);
  };
  const float offset_x = read_offset(kTooltipOwnerOffsetXField);
  const float offset_y = read_offset(kTooltipOwnerOffsetYField);

  struct DirectionalAnchor {
    const char *type;
    const char *point;
    const char *relative_point;
  };

  static constexpr std::array<DirectionalAnchor, 8> kDirectionalAnchors{{
      {"ANCHOR_LEFT", "BOTTOMRIGHT", "TOPLEFT"},
      {"ANCHOR_RIGHT", "BOTTOMLEFT", "TOPRIGHT"},
      {"ANCHOR_BOTTOMLEFT", "TOPRIGHT", "BOTTOMLEFT"},
      {"ANCHOR_BOTTOM", "TOP", "BOTTOM"},
      {"ANCHOR_BOTTOMRIGHT", "TOPLEFT", "BOTTOMRIGHT"},
      {"ANCHOR_TOPLEFT", "BOTTOMLEFT", "TOPLEFT"},
      {"ANCHOR_TOP", "BOTTOM", "TOP"},
      {"ANCHOR_TOPRIGHT", "BOTTOMRIGHT", "TOPRIGHT"},
  }};
  for (const auto &entry : kDirectionalAnchors) {
    if (std::strcmp(anchor, entry.type) == 0) {
      StoreTooltipAnchor(L, tooltip_index, entry.point, owner_index, entry.relative_point, offset_x,
                         offset_y);
      lua_pop(L, 1);
      return;
    }
  }

  const auto [mouse_x, mouse_y] = openwow::input::InputManager::Get().GetMousePosition();
  const auto *manager = runtime::WorldUiRuntimeContext::FromLua(L);
  const float screen_height =
      manager != nullptr ? manager->screen_height()
                         : openwow::ui::kUiScriptScreenUnitHeight;

  const float pixel_scale =
      ScriptFrameUiUnitScale(L, tooltip_index).value;
  const float cursor_x = static_cast<float>(mouse_x) / pixel_scale;
  const float cursor_y =
      (screen_height - static_cast<float>(mouse_y)) / pixel_scale;
  lua_pushnumber(L, pixel_scale);
  lua_setfield(L, tooltip_index, kTooltipCursorPixelScaleField);
  lua_getglobal(L, "UIParent");
  const int ui_parent_index = lua_istable(L, -1) != 0 ? lua_absindex(L, -1) : 0;
  if (std::strcmp(anchor, "ANCHOR_CURSOR") == 0) {

    StoreTooltipAnchor(L, tooltip_index, "BOTTOM", ui_parent_index,
                       "BOTTOMLEFT", cursor_x, cursor_y);
  } else {

    StoreTooltipAnchor(L, tooltip_index, "BOTTOMLEFT", ui_parent_index,
                       "BOTTOMLEFT", cursor_x + offset_x,
                       cursor_y + offset_y);
  }
  lua_pop(L, 1);
  lua_pop(L, 1);
}

bool PushTooltipFontStringPair(lua_State *L, int tooltip_index, int slot_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_getfield(L, tooltip_index, kTooltipFontStringPairsField);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }

  lua_rawgeti(L, -1, static_cast<lua_Integer>(slot_index));
  lua_remove(L, -2);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }

  return true;
}

bool TooltipOwnsFontString(lua_State *L, int tooltip_index, int font_string_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  font_string_index = lua_absindex(L, font_string_index);
  if (!openwow::ui::game::detail::LuaScriptObjectIsKindOfCanonicalType(
          L, font_string_index, openwow::ui::widgets::ScriptObjectType::FontString)) {
    return false;
  }
  lua_getfield(L, font_string_index, "__ow_parent");
  const bool lua_owned = lua_rawequal(L, -1, tooltip_index) != 0;
  lua_pop(L, 1);
  if (!lua_owned)
    return false;

  const auto *manager = runtime::WorldUiRuntimeContext::FromLua(L);
  const char *region_key = GetFrameRuntimeKeyOrName(L, font_string_index);
  const char *owner_key = GetFrameRuntimeKeyOrName(L, tooltip_index);
  if (manager == nullptr || region_key == nullptr || owner_key == nullptr) {
    return false;
  }
  const auto *region = manager->frame_store().FindFrame(region_key);
  const auto binding = manager->frame_store().FindLuaRef(region_key);
  if (region == nullptr || region->parent != owner_key || !binding.has_value()) {
    return false;
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, *binding);
  const bool native_owned = lua_rawequal(L, -1, font_string_index) != 0;
  lua_pop(L, 1);
  return native_owned;
}

bool TooltipOwnsRegion(lua_State* L, int tooltip_index, int region_index,
                       const char* canonical_type) {
  tooltip_index = lua_absindex(L, tooltip_index);
  region_index = lua_absindex(L, region_index);
  if (!openwow::ui::game::detail::LuaScriptObjectIsKindOfCanonicalType(
          L, region_index,
          std::strcmp(canonical_type, "Texture") == 0
              ? openwow::ui::widgets::ScriptObjectType::Texture
              : openwow::ui::widgets::ScriptObjectType::FontString)) {
    return false;
  }
  lua_getfield(L, region_index, "__ow_parent");
  const bool lua_owned = lua_rawequal(L, -1, tooltip_index) != 0;
  lua_pop(L, 1);
  if (!lua_owned) {
    return false;
  }
  const auto* const manager = runtime::WorldUiRuntimeContext::FromLua(L);
  const char* const region_key =
      GetFrameRuntimeKeyOrName(L, region_index);
  const char* const owner_key =
      GetFrameRuntimeKeyOrName(L, tooltip_index);
  if (manager == nullptr || region_key == nullptr || owner_key == nullptr) {
    return false;
  }
  const auto* const region = manager->frame_store().FindFrame(region_key);
  const auto binding = manager->frame_store().FindLuaRef(region_key);
  if (region == nullptr || region->parent != owner_key ||
      !binding.has_value()) {
    return false;
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, *binding);
  const bool native_owned = lua_rawequal(L, -1, region_index) != 0;
  lua_pop(L, 1);
  return native_owned;
}

bool PushTooltipTextureRegion(lua_State* L, int tooltip_index,
                              const int texture_slot) {
  tooltip_index = lua_absindex(L, tooltip_index);
  if (texture_slot <= 0 || texture_slot > kTooltipMaximumTextures) {
    return false;
  }
  const char* const tooltip_name = openwow::ui::BorrowRawLuaStringField(
      L, tooltip_index, "__ow_name");
  if (tooltip_name == nullptr || tooltip_name[0] == '\0') {
    return false;
  }
  const std::string texture_name = std::string(tooltip_name) + "Texture" +
                                   std::to_string(texture_slot);
  lua_getglobal(L, texture_name.c_str());
  if (lua_istable(L, -1) == 0 ||
      !TooltipOwnsRegion(L, tooltip_index, -1, "Texture")) {
    lua_pop(L, 1);
    return false;
  }
  return true;
}

void StoreTooltipTextureCoordinates(lua_State* L, int texture_index,
                                    const TooltipTextureData& texture) {
  texture_index = lua_absindex(L, texture_index);
  const double left = texture.tex_coords[1];
  const double right = texture.tex_coords[3];
  const double top = texture.tex_coords[0];
  const double bottom = texture.tex_coords[2];
  runtime::SetTextureRenderStateTexCoordQuad(L, texture_index, left, top, left,
                                             bottom, right, top, right, bottom);
  for (const auto& [field, value] :
       std::array<std::pair<const char*, double>, 4>{{
           {"__ow_tc_l", left}, {"__ow_tc_r", right},
           {"__ow_tc_t", top}, {"__ow_tc_b", bottom},
       }}) {
    lua_pushnumber(L, value);
    lua_setfield(L, texture_index, field);
  }
}

int SyncTooltipRegisteredTextures(lua_State* L, int tooltip_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  const auto& textures = TooltipSystem::Get().GetTextures();
  lua_getfield(L, tooltip_index, kTooltipUsedTextureCountField);
  const int previous_count =
      lua_isnumber(L, -1) != 0
          ? std::clamp(static_cast<int>(lua_tointeger(L, -1)), 0,
                       kTooltipMaximumTextures)
          : 0;
  lua_pop(L, 1);
  const int active_count = std::min(
      static_cast<int>(textures.size()), kTooltipMaximumTextures);
  const int touched_count = std::max(previous_count, active_count);
  for (int slot = 1; slot <= touched_count; ++slot) {
    if (!PushTooltipTextureRegion(L, tooltip_index, slot)) {
      continue;
    }
    const int texture_index = lua_absindex(L, -1);
    if (static_cast<std::size_t>(slot) <= textures.size()) {
      const auto& texture = textures[static_cast<std::size_t>(slot - 1)];
      using runtime::TextureRenderStateField;
      runtime::SetTextureRenderStateString(
          L, texture_index, TextureRenderStateField::kTexture,
          std::string_view(texture.filename));
      runtime::SetTextureRenderStateBoolean(
          L, texture_index, TextureRenderStateField::kTextureCleared, false);
      StoreTooltipTextureCoordinates(L, texture_index, texture);
      const auto channel = [&](const unsigned shift) {
        return static_cast<double>((texture.vertex_color >> shift) & 0xffu) /
               255.0;
      };
      runtime::SetTextureRenderStateNumber(
          L, texture_index, TextureRenderStateField::kVertexColorR, channel(16));
      runtime::SetTextureRenderStateNumber(
          L, texture_index, TextureRenderStateField::kVertexColorG, channel(8));
      runtime::SetTextureRenderStateNumber(
          L, texture_index, TextureRenderStateField::kVertexColorB, channel(0));
      runtime::SetTextureRenderStateNumber(
          L, texture_index, TextureRenderStateField::kVertexColorA, channel(24));
      lua_pushboolean(L, 1);
      lua_setfield(L, texture_index, "__ow_visible");
    } else {
      using runtime::TextureRenderStateField;
      runtime::SetTextureRenderStateString(
          L, texture_index, TextureRenderStateField::kTexture, std::nullopt);
      runtime::SetTextureRenderStateBoolean(
          L, texture_index, TextureRenderStateField::kTextureCleared, true);
      lua_pushboolean(L, 0);
      lua_setfield(L, texture_index, "__ow_visible");
      ClearTooltipAnchors(L, texture_index);
    }
    lua_pop(L, 1);
  }
  lua_pushinteger(L, active_count);
  lua_setfield(L, tooltip_index, kTooltipUsedTextureCountField);
  return touched_count;
}

void SynchronizeTooltipTextureLayouts(lua_State* L, int tooltip_index,
                                      const int touched_count) {
  for (int texture_slot = 1; texture_slot <= touched_count;
       ++texture_slot) {
    if (PushTooltipTextureRegion(L, tooltip_index, texture_slot)) {
      NotifyFrameInputMutation(L, -1, false);
      lua_pop(L, 1);
    }
  }
}

void AppendTooltipFontStringPair(lua_State *L, int tooltip_index, int left_index, int right_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  left_index = lua_absindex(L, left_index);
  right_index = lua_absindex(L, right_index);

  const int pairs_index = EnsureTooltipFontStringPairsTable(L, tooltip_index);
  const auto count = static_cast<int>(lua_rawlen(L, pairs_index));

  lua_newtable(L);
  const int pair_index = lua_absindex(L, -1);
  lua_pushvalue(L, left_index);
  lua_setfield(L, pair_index, kTooltipPairLeftField);
  lua_pushvalue(L, right_index);
  lua_setfield(L, pair_index, kTooltipPairRightField);
  lua_rawseti(L, pairs_index, static_cast<lua_Integer>(count + 1));
  lua_pop(L, 1);
}

int ClearTooltipRegisteredFontStrings(lua_State *L, int tooltip_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  const int used_count = GetTooltipUsedLineCount(L, tooltip_index);
  lua_getfield(L, tooltip_index, kTooltipFontStringPairsField);
  if (lua_istable(L, -1) != 0) {
    const int count = std::min(
        used_count, static_cast<int>(lua_rawlen(L, -1)));
    for (int slot_index = 1; slot_index <= count; ++slot_index) {
      lua_rawgeti(L, -1, static_cast<lua_Integer>(slot_index));
      if (lua_istable(L, -1) != 0) {
        lua_getfield(L, -1, kTooltipPairLeftField);
        if (lua_istable(L, -1) != 0) {
          ClearTooltipFontStringState(L, -1);
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, kTooltipPairRightField);
        if (lua_istable(L, -1) != 0) {
          ClearTooltipFontStringState(L, -1);
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  SetTooltipUsedLineCount(L, tooltip_index, 0);
  return used_count;
}

void SynchronizeTooltipFontStringLayouts(lua_State *L, int tooltip_index,
                                         const int touched_count) {
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_getfield(L, tooltip_index, kTooltipFontStringPairsField);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  const int count = std::min(
      std::max(0, touched_count), static_cast<int>(lua_rawlen(L, -1)));
  for (int slot_index = 1; slot_index <= count; ++slot_index) {
    lua_rawgeti(L, -1, static_cast<lua_Integer>(slot_index));
    if (lua_istable(L, -1) != 0) {
      lua_getfield(L, -1, kTooltipPairLeftField);
      if (lua_istable(L, -1) != 0) {
        NotifyFrameInputMutation(L, lua_absindex(L, -1), false);
      }
      lua_pop(L, 1);

      lua_getfield(L, -1, kTooltipPairRightField);
      if (lua_istable(L, -1) != 0) {
        NotifyFrameInputMutation(L, lua_absindex(L, -1), false);
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

void SyncTooltipRegisteredFontStrings(lua_State *L, int tooltip_index, int slot_index,
                                      const char *left_text, const TooltipLuaTextStyle &left_style,
                                      const char *right_text,
                                      const TooltipLuaTextStyle &right_style) {
  tooltip_index = lua_absindex(L, tooltip_index);
  if (!PushTooltipFontStringPair(L, tooltip_index, slot_index)) {
    return;
  }

  const int pair_index = lua_absindex(L, -1);
  const bool show_left = left_text != nullptr && left_text[0] != '\0';
  const bool show_right = right_text != nullptr && right_text[0] != '\0';

  lua_getfield(L, pair_index, kTooltipPairLeftField);
  if (lua_istable(L, -1) != 0) {
    if (show_left) {
      SetTooltipFontStringState(L, -1, left_text, left_style, true);
    } else {
      ClearTooltipFontStringState(L, -1);
    }
  }
  lua_pop(L, 1);

  lua_getfield(L, pair_index, kTooltipPairRightField);
  if (lua_istable(L, -1) != 0) {
    if (show_right) {
      SetTooltipFontStringState(L, -1, right_text, right_style, true);
    } else {
      ClearTooltipFontStringState(L, -1);
    }
  }
  lua_pop(L, 1);

  lua_pop(L, 1);
}

bool EnsureTooltipFontStringSlot(lua_State *L, int tooltip_index, int slot_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  if (slot_index <= 0) {
    return false;
  }
  if (PushTooltipFontStringPair(L, tooltip_index, slot_index)) {
    lua_pop(L, 1);
    return true;
  }

  const char *raw_tooltip_name =
      openwow::ui::BorrowRawLuaStringField(L, tooltip_index, "__ow_name");
  const bool named_tooltip = raw_tooltip_name != nullptr && raw_tooltip_name[0] != '\0';
  const std::string left_name =
      named_tooltip ? std::string(raw_tooltip_name) + "TextLeft" + std::to_string(slot_index)
                    : std::string{};
  const std::string right_name =
      named_tooltip ? std::string(raw_tooltip_name) + "TextRight" + std::to_string(slot_index)
                    : std::string{};

  if (named_tooltip)
    lua_getglobal(L, left_name.c_str());
  else
    lua_pushnil(L);
  const bool has_xml_left = lua_istable(L, -1) != 0 && TooltipOwnsFontString(L, tooltip_index, -1);
  if (named_tooltip)
    lua_getglobal(L, right_name.c_str());
  else
    lua_pushnil(L);
  const bool has_xml_right = lua_istable(L, -1) != 0 && TooltipOwnsFontString(L, tooltip_index, -1);
  if (has_xml_left && has_xml_right) {
    AppendTooltipFontStringPair(L, tooltip_index, -2, -1);
    lua_pop(L, 2);
    return true;
  }
  lua_pop(L, 2);

  CreateFontStringTable(L, tooltip_index);
  const int left_index = lua_absindex(L, -1);
  if (named_tooltip) {
    lua_pushstring(L, left_name.c_str());
    lua_setfield(L, left_index, "__ow_name");
    openwow::ui::ReplaceLuaGlobalValue(L, left_name.c_str(), left_index);
  }
  lua_pushvalue(L, tooltip_index);
  lua_setfield(L, left_index, "__ow_parent");
  ClearTooltipFontStringState(L, left_index);

  CreateFontStringTable(L, tooltip_index);
  const int right_index = lua_absindex(L, -1);
  if (named_tooltip) {
    lua_pushstring(L, right_name.c_str());
    lua_setfield(L, right_index, "__ow_name");
    openwow::ui::ReplaceLuaGlobalValue(L, right_name.c_str(), right_index);
  }
  lua_pushvalue(L, tooltip_index);
  lua_setfield(L, right_index, "__ow_parent");
  ClearTooltipFontStringState(L, right_index);

  if (slot_index > 1 && PushTooltipFontStringPair(L, tooltip_index, slot_index - 1)) {
    const int previous_pair_index = lua_absindex(L, -1);

    lua_getfield(L, previous_pair_index, kTooltipPairLeftField);
    if (lua_istable(L, -1) != 0) {
      CopyNamedFontObjectStyle(L, left_index, -1);
      StoreTooltipAnchor(L, left_index, "TOPLEFT", -1, "BOTTOMLEFT", 0.0F, -kTooltipLineSpacing);
    }
    lua_pop(L, 1);

    lua_getfield(L, previous_pair_index, kTooltipPairRightField);
    if (lua_istable(L, -1) != 0) {
      CopyNamedFontObjectStyle(L, right_index, -1);
    }
    lua_pop(L, 1);

    lua_pop(L, 1);
  } else {
    StoreTooltipAnchor(L, left_index, "TOPLEFT", tooltip_index, "TOPLEFT", kTooltipFramePadding,
                       -kTooltipFramePadding);
  }

  StoreTooltipAnchor(L, right_index, "RIGHT", left_index, "LEFT", kTooltipDoubleColumnSpacing,
                     0.0F);

  TrackRuntimeRegion(L, tooltip_index, left_index, "FontString",
                     named_tooltip ? left_name.c_str() : nullptr, "ARTWORK");
  TrackRuntimeRegion(L, tooltip_index, right_index, "FontString",
                     named_tooltip ? right_name.c_str() : nullptr, "ARTWORK");

  AppendTooltipFontStringPair(L, tooltip_index, left_index, right_index);
  lua_pop(L, 2);
  return true;
}

void FinalizeTooltipLuaLayout(lua_State *L, int tooltip_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  if (!TooltipHasOwner(L, tooltip_index)) {
    return;
  }

  struct RowLayout {
    int slot{0};
    TooltipFontStringMetrics left{};
    TooltipFontStringMetrics right{};
    std::vector<int> texture_slots;
    float texture_extent{0.0F};
    float texture_height{0.0F};
    bool left_visible{false};
    bool right_visible{false};
    bool constrained{false};
  };

  const int line_count = GetTooltipUsedLineCount(L, tooltip_index);
  const auto& tooltip_system = openwow::ui::game::TooltipSystem::Get();
  const bool force_minimum_width = tooltip_system.IsForceMinWidth();
  float common_width = tooltip_system.GetMinimumWidth();
  if (!std::isfinite(common_width)) {
    common_width = 0.0F;
  }

  std::vector<RowLayout> rows;
  rows.reserve(static_cast<std::size_t>(line_count));
  for (int slot_index = 1; slot_index <= line_count; ++slot_index) {
    if (!PushTooltipFontStringPair(L, tooltip_index, slot_index)) {
      continue;
    }
    const int pair_index = lua_absindex(L, -1);
    lua_getfield(L, pair_index, kTooltipPairLeftField);
    const int left_index = lua_istable(L, -1) != 0 ? lua_absindex(L, -1) : 0;
    lua_getfield(L, pair_index, kTooltipPairRightField);
    const int right_index = lua_istable(L, -1) != 0 ? lua_absindex(L, -1) : 0;

    RowLayout row;
    row.slot = slot_index;
    row.left_visible = left_index != 0 && TooltipFontStringIsVisible(L, left_index);
    row.right_visible = right_index != 0 && TooltipFontStringIsVisible(L, right_index);

    if (row.left_visible) {
      lua_pushnumber(L, 0.0);
      lua_setfield(L, left_index, "__ow_width");
      MarkLuaFontStringDimensionFromLayout(L, left_index, "__ow_width", false);
      lua_pushnumber(L, 0.0);
      lua_setfield(L, left_index, "__ow_height");
      MarkLuaFontStringDimensionFromLayout(L, left_index, "__ow_height", false);
      row.left = MeasureTooltipFontString(L, left_index);
      row.constrained =
          force_minimum_width || ReadTooltipLocalBoolean(L, left_index, "__ow_wordwrap");
    }
    if (row.right_visible) {
      lua_pushnumber(L, 0.0);
      lua_setfield(L, right_index, "__ow_width");
      MarkLuaFontStringDimensionFromLayout(L, right_index, "__ow_width", false);
      lua_pushnumber(L, 0.0);
      lua_setfield(L, right_index, "__ow_height");
      MarkLuaFontStringDimensionFromLayout(L, right_index, "__ow_height", false);
      row.right = MeasureTooltipFontString(L, right_index);
    }
    const auto& tooltip_textures = tooltip_system.GetTextures();
    for (std::size_t texture_index = 0;
         texture_index < tooltip_textures.size(); ++texture_index) {
      if (tooltip_textures[texture_index].line_index !=
          static_cast<std::uint32_t>(slot_index - 1)) {
        continue;
      }
      const int texture_slot = static_cast<int>(texture_index + 1u);
      if (!PushTooltipTextureRegion(L, tooltip_index, texture_slot)) {
        continue;
      }
      const auto size = ResolveLuaRegionSizeValues(L, -1, false);
      lua_pop(L, 1);
      row.texture_slots.push_back(texture_slot);
      row.texture_extent += std::max(0.0F, size.width) +
                            kTooltipTextureTextSpacing;
      row.texture_height =
          std::max(row.texture_height, std::max(0.0F, size.height));
    }

    if (!row.constrained) {
      float row_width =
          row.texture_extent + row.left.width + row.right.width;
      if (row.left_visible && row.right_visible) {
        row_width += kTooltipDoubleColumnSpacing;
      }
      common_width = std::max(common_width, row_width);
    }
    rows.push_back(row);
    lua_pop(L, 3);
  }

  for (const auto &row : rows) {
    if (row.left_visible && row.constrained) {
      if (PushTooltipFontStringPair(L, tooltip_index, row.slot)) {
        lua_getfield(L, -1, kTooltipPairLeftField);
        if (lua_istable(L, -1) != 0) {
          const float cap = std::min(row.left.width,
                                     kTooltipMaximumWrappedTextWidth);
          lua_pushnumber(L, cap);
          lua_setfield(L, -2, "__ow_width");
          const auto wrapped = MeasureTooltipFontString(L, -1);
          common_width = std::max(
              common_width,
              row.texture_extent +
                  std::max(wrapped.width,
                           row.right_visible ? row.right.width : 0.0F));
        }
        lua_pop(L, 2);
      }
    }
  }

  float content_height = 0.0F;
  bool has_visible_line = false;
  float previous_texture_extent = 0.0F;
  bool has_previous_row = false;
  for (auto &row : rows) {
    if (!PushTooltipFontStringPair(L, tooltip_index, row.slot)) {
      continue;
    }
    const int pair_index = lua_absindex(L, -1);
    lua_getfield(L, pair_index, kTooltipPairLeftField);
    const int left_index = lua_istable(L, -1) != 0 ? lua_absindex(L, -1) : 0;
    lua_getfield(L, pair_index, kTooltipPairRightField);
    const int right_index = lua_istable(L, -1) != 0 ? lua_absindex(L, -1) : 0;
    if (left_index != 0) {
      if (!has_previous_row) {
        StoreTooltipAnchor(L, left_index, "TOPLEFT", tooltip_index,
                           "TOPLEFT",
                           kTooltipFramePadding + row.texture_extent,
                           -kTooltipFramePadding);
      } else if (PushTooltipFontStringPair(L, tooltip_index,
                                           row.slot - 1)) {
        lua_getfield(L, -1, kTooltipPairLeftField);
        if (lua_istable(L, -1) != 0) {
          StoreTooltipAnchor(
              L, left_index, "TOPLEFT", -1, "BOTTOMLEFT",
              row.texture_extent - previous_texture_extent,
              -kTooltipLineSpacing);
        }
        lua_pop(L, 2);
      }
    }
    if (left_index != 0 && row.left_visible && row.constrained) {

      const float available_text_width =
          std::max(1.0F, common_width - row.texture_extent);
      lua_pushnumber(L, available_text_width);
      lua_setfield(L, left_index, "__ow_width");
      MarkLuaFontStringDimensionFromLayout(L, left_index, "__ow_width", true);
      row.left = MeasureTooltipFontString(L, left_index);
      row.left.width = available_text_width;
    }
    if (left_index != 0 && right_index != 0 && row.right_visible) {
      StoreTooltipAnchor(L, right_index, "RIGHT", left_index, "LEFT",
                         std::max(0.0F,
                                  common_width - row.texture_extent),
                         0.0F);
    }
    if (left_index != 0 && !row.texture_slots.empty()) {
      float placed_width = 0.0F;
      for (const int texture_slot : row.texture_slots) {
        if (!PushTooltipTextureRegion(L, tooltip_index, texture_slot)) {
          continue;
        }
        const auto size = ResolveLuaRegionSizeValues(L, -1, false);
        StoreTooltipAnchor(
            L, -1, "TOPLEFT", left_index, "TOPLEFT",
            -row.texture_extent + placed_width, 0.0F);
        placed_width += std::max(0.0F, size.width) +
                        kTooltipTextureTextSpacing;
        lua_pop(L, 1);
      }
    }
    if (row.left_visible || row.right_visible) {
      if (has_visible_line) {
        content_height += kTooltipLineSpacing;
      }

      content_height += std::max(
          {row.left_visible ? row.left.height : 0.0F,
           row.right_visible ? row.right.height : 0.0F,
           row.texture_height});
      has_visible_line = true;
    }
    previous_texture_extent = row.texture_extent;
    has_previous_row = true;
    lua_pop(L, 3);
  }

  float extra_width = tooltip_system.GetPadding();
  if (!std::isfinite(extra_width)) {
    extra_width = 0.0F;
  }
  lua_pushnumber(L, 2.0F * kTooltipFramePadding + extra_width + common_width);
  lua_setfield(L, tooltip_index, "__ow_width");
  lua_pushnumber(L, 2.0F * kTooltipFramePadding + content_height);
  lua_setfield(L, tooltip_index, "__ow_height");
  ApplyTooltipOwnerAnchor(L, tooltip_index);
}

void SyncTooltipRegisteredLinesFromSystem(lua_State *L, int tooltip_index) {
  tooltip_index = lua_absindex(L, tooltip_index);
  SyncTooltipClearGeneration(L, tooltip_index);
  const int previous_line_count =
      ClearTooltipRegisteredFontStrings(L, tooltip_index);

  const auto &tooltip_system = openwow::ui::game::TooltipSystem::Get();
  const auto &lines = tooltip_system.GetLines();
  int slot_index = 0;
  for (const auto &line : lines) {
    ++slot_index;
    EnsureTooltipFontStringSlot(L, tooltip_index, slot_index);
    SyncTooltipRegisteredFontStrings(
        L, tooltip_index, slot_index, line.left_text.c_str(),
        TooltipLuaTextStyle{line.left_r, line.left_g, line.left_b, line.wrap},
        line.right_text.empty() ? nullptr : line.right_text.c_str(),
        TooltipLuaTextStyle{line.right_r, line.right_g, line.right_b, false});
  }

  SetTooltipUsedLineCount(L, tooltip_index, slot_index);
  const int synchronized_texture_count =
      SyncTooltipRegisteredTextures(L, tooltip_index);
  FinalizeTooltipLuaLayout(L, tooltip_index);

  if (tooltip_system.IsShown()) {

    lua_pushnumber(L, 1.0);
    lua_setfield(L, tooltip_index, "__ow_alpha");
    (void)openwow::ui::game::detail::ShowLuaScriptFrame(L, tooltip_index);
  } else {
    (void)openwow::ui::game::detail::HideLuaScriptFrame(L, tooltip_index);
  }

  NotifyFrameInputMutation(L, tooltip_index, false);
  SynchronizeTooltipFontStringLayouts(
      L, tooltip_index, std::max(previous_line_count, slot_index));
  SynchronizeTooltipTextureLayouts(L, tooltip_index,
                                   synchronized_texture_count);

  lua_pushinteger(L, static_cast<lua_Integer>(tooltip_system.GetPresentationRevision()));
  lua_setfield(L, tooltip_index, kTooltipPresentationRevisionField);
}

bool TooltipRetainedPresentationIsCurrent(lua_State *L, int tooltip_index) {
  if (L == nullptr || lua_istable(L, tooltip_index) == 0) {
    return false;
  }
  tooltip_index = lua_absindex(L, tooltip_index);
  lua_getfield(L, tooltip_index, kTooltipPresentationRevisionField);
  const bool current =
      lua_isnumber(L, -1) != 0 && static_cast<std::uint64_t>(lua_tointeger(L, -1)) ==
                                      TooltipSystem::Get().GetPresentationRevision();
  lua_pop(L, 1);
  return current;
}

void RefreshCursorAnchoredTooltipFrames(lua_State* L) {
  if (L == nullptr) {
    return;
  }
  auto* const manager = runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr) {
    return;
  }
  const auto [mouse_x, mouse_y] =
      openwow::input::InputManager::Get().GetMousePosition();

  auto& frames = manager->frame_store();
  const auto tooltip_keys = frames.FramesOfKind("GameTooltip");
  const std::vector<std::string> tooltips(tooltip_keys.begin(),
                                          tooltip_keys.end());
  for (const auto& key : tooltips) {
    const auto* const frame = frames.FindFrame(key);
    const auto ref = frames.FindLuaRef(key);
    if (frame == nullptr || !ref.has_value() ||
        !openwow::text::EqualsIgnoreCaseAscii(frame->kind, "GameTooltip")) {
      continue;
    }
    const int base = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, *ref);
    if (lua_istable(L, -1) == 0 ||
        !ReadTooltipLocalBoolean(L, -1, "__ow_visible")) {
      lua_settop(L, base);
      continue;
    }
    const char* anchor = openwow::ui::BorrowRawLuaStringField(
        L, -1, kTooltipOwnerAnchorField);
    anchor = CanonicalTooltipAnchorType(anchor);
    if (std::strcmp(anchor, "ANCHOR_CURSOR") != 0 &&
        std::strcmp(anchor, "ANCHOR_CURSOR_RIGHT") != 0) {
      lua_settop(L, base);
      continue;
    }

    const int tooltip_index = lua_absindex(L, -1);
    lua_getfield(L, tooltip_index, "__ow_tooltip_cursor_pixel_x");
    const int prior_x = lua_isnumber(L, -1) != 0
                            ? static_cast<int>(lua_tointeger(L, -1))
                            : std::numeric_limits<int>::min();
    lua_pop(L, 1);
    lua_getfield(L, tooltip_index, "__ow_tooltip_cursor_pixel_y");
    const int prior_y = lua_isnumber(L, -1) != 0
                            ? static_cast<int>(lua_tointeger(L, -1))
                            : std::numeric_limits<int>::min();
    lua_pop(L, 1);
    lua_getfield(L, tooltip_index, kTooltipCursorPixelScaleField);
    const float prior_scale = lua_isnumber(L, -1) != 0
                                  ? static_cast<float>(lua_tonumber(L, -1))
                                  : 0.0F;
    lua_pop(L, 1);
    const float current_scale =
        ScriptFrameUiUnitScale(L, tooltip_index).value;
    if (prior_x != mouse_x || prior_y != mouse_y ||
        std::fabs(prior_scale - current_scale) >
            std::numeric_limits<float>::epsilon()) {
      lua_pushinteger(L, mouse_x);
      lua_setfield(L, tooltip_index, "__ow_tooltip_cursor_pixel_x");
      lua_pushinteger(L, mouse_y);
      lua_setfield(L, tooltip_index, "__ow_tooltip_cursor_pixel_y");
      ApplyTooltipOwnerAnchor(L, tooltip_index);
      NotifyFrameInputMutation(L, tooltip_index, false);
    }
    lua_settop(L, base);
  }
}

const char *CanonicalTooltipAnchorType(const char *anchor) {
  static constexpr std::array<const char *, 12> kAnchors = {
      "ANCHOR_LEFT",        "ANCHOR_RIGHT",   "ANCHOR_BOTTOMLEFT", "ANCHOR_BOTTOM",
      "ANCHOR_BOTTOMRIGHT", "ANCHOR_TOPLEFT", "ANCHOR_TOP",        "ANCHOR_TOPRIGHT",
      "ANCHOR_CURSOR",      "ANCHOR_NONE",    "ANCHOR_PRESERVE",   "ANCHOR_CURSOR_RIGHT",
  };

  if (anchor != nullptr) {
    for (const char *candidate : kAnchors) {
      if (openwow::text::EqualsIgnoreCaseAscii(anchor, candidate)) {
        return candidate;
      }
    }
  }
  return "ANCHOR_LEFT";
}

const char *StoreTooltipAnchorState(lua_State *L, const int tooltip_index, const char *anchor,
                                    const float offset_x, const float offset_y) {
  const char *canonical_anchor = CanonicalTooltipAnchorType(anchor);
  lua_pushstring(L, canonical_anchor);
  lua_setfield(L, tooltip_index, kTooltipOwnerAnchorField);
  lua_pushnumber(L, offset_x);
  lua_setfield(L, tooltip_index, kTooltipOwnerOffsetXField);
  lua_pushnumber(L, offset_y);
  lua_setfield(L, tooltip_index, kTooltipOwnerOffsetYField);
  return canonical_anchor;
}

void ApplyGameTooltipMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int tooltip_index = ValidateFrameObjectSelf(Ls, "GameTooltip");
        const int owner_index = ValidateTooltipOwnerFrame(Ls, tooltip_index);

        const char *anchor = luaL_optstring(Ls, 3, "ANCHOR_LEFT");
        const float offset_x = ConvertTooltipScriptPixelsToStoredCoordinate(
            ReadTooltipOptionalOffset(Ls, 4));
        const float offset_y = ConvertTooltipScriptPixelsToStoredCoordinate(
            ReadTooltipOptionalOffset(Ls, 5));

        lua_pushvalue(Ls, owner_index);
        lua_setfield(Ls, tooltip_index, kTooltipOwnerFrameField);
        const char *canonical_anchor =
            StoreTooltipAnchorState(Ls, tooltip_index, anchor, offset_x, offset_y);

        const char *owner_key = GetFrameRuntimeKeyOrName(Ls, owner_index);
        auto &tooltip = openwow::ui::game::TooltipSystem::Get();
        tooltip.SetOwner(owner_key != nullptr ? owner_key : "<anonymous>", canonical_anchor);
        ApplyTooltipOwnerAnchor(Ls, tooltip_index, true);
        return 0;
      },
      0);
  lua_setfield(L, f, "SetOwner");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipText.trampoline>(
      L, f, "SetText");

  lua_pushcfunction(L, detail::kGetTooltipItem.trampoline);
  lua_setfield(L, f, "GetItem");
  lua_pushcfunction(L, detail::kGetTooltipSpell.trampoline);
  lua_setfield(L, f, "GetSpell");
  lua_pushcfunction(L, detail::kGetTooltipUnit.trampoline);
  lua_setfield(L, f, "GetUnit");

  lua_pushcfunction(L, detail::kAddTooltipLine.trampoline);
  lua_setfield(L, f, "AddLine");

  lua_pushcfunction(L, detail::kAddTooltipDoubleLine.trampoline);
  lua_setfield(L, f, "AddDoubleLine");

  lua_pushcfunction(L, detail::kClearTooltipLines.trampoline);
  lua_setfield(L, f, "ClearLines");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int tooltip_index = ValidateFrameObjectSelf(Ls, "GameTooltip");
        const bool show_hidden = lua_toboolean(Ls, 2) != 0;

        auto &tooltip_system = openwow::ui::game::TooltipSystem::Get();
        auto *manager = runtime::WorldUiRuntimeContext::FromLua(Ls);
        if (manager == nullptr) {
          tooltip_system.ClearLines();
          const int cleared_count =
              ClearTooltipRegisteredFontStrings(Ls, tooltip_index);
          SynchronizeTooltipFontStringLayouts(Ls, tooltip_index,
                                              cleared_count);
          const int cleared_texture_count =
              SyncTooltipRegisteredTextures(Ls, tooltip_index);
          SynchronizeTooltipTextureLayouts(Ls, tooltip_index,
                                           cleared_texture_count);
          return 0;
        }

        TooltipFrameStackSnapshot snapshot;
        if (!manager->BuildFrameStackSnapshot(show_hidden, &snapshot)) {
          tooltip_system.ClearLines();
          const int cleared_count =
              ClearTooltipRegisteredFontStrings(Ls, tooltip_index);
          SynchronizeTooltipFontStringLayouts(Ls, tooltip_index,
                                              cleared_count);
          const int cleared_texture_count =
              SyncTooltipRegisteredTextures(Ls, tooltip_index);
          SynchronizeTooltipTextureLayouts(Ls, tooltip_index,
                                           cleared_texture_count);
          return 0;
        }

        tooltip_system.SetFrameStack(snapshot);
        FireScript(Ls, tooltip_index, "OnTooltipSetFrameStack");
        SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
        return 0;
      },
      0);
  lua_setfield(L, f, "SetFrameStack");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int tooltip_index = ValidateFrameObjectSelf(Ls, "GameTooltip");
        const std::string usage_name = lua_adapter::ScriptObjectDisplayName(Ls, tooltip_index);

        if (lua_istable(Ls, 2) == 0 || lua_istable(Ls, 3) == 0) {
          return luaL_error(Ls, "Usage: %s:AddFontStrings(leftstring, rightstring)",
                            usage_name.c_str());
        }

        const char *left_type = openwow::ui::BorrowRawLuaStringField(Ls, 2, "__ow_type");
        if (left_type == nullptr || left_type[0] == '\0') {
          return luaL_error(Ls, "%s:AddFontStrings(): Couldn't find 'this' in fontstring",
                            usage_name.c_str());
        }
        if (std::strcmp(left_type, "FontString") != 0) {
          return luaL_error(Ls, "%s:AddFontStrings(): Wrong object type, expected fontstring",
                            usage_name.c_str());
        }

        const char *right_type = openwow::ui::BorrowRawLuaStringField(Ls, 3, "__ow_type");
        if (right_type == nullptr || right_type[0] == '\0') {
          return luaL_error(Ls, "%s:AddFontStrings(): Couldn't find 'this' in fontstring",
                            usage_name.c_str());
        }
        if (std::strcmp(right_type, "FontString") != 0) {
          return luaL_error(Ls, "%s:AddFontStrings(): Wrong object type, expected fontstring",
                            usage_name.c_str());
        }

        AppendTooltipFontStringPair(Ls, tooltip_index, 2, 3);
        return 0;
      },
      0);
  lua_setfield(L, f, "AddFontStrings");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipUnit.trampoline>(
      L, f, "SetUnit");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int tooltip_index = ValidateFrameObjectSelf(Ls, "GameTooltip");
        if (lua_isnumber(Ls, 2) == 0) {
          return luaL_error(Ls, "Invalid spell ID in %s:SetSpellByID",
                            lua_adapter::ScriptObjectDisplayName(Ls, tooltip_index));
        }

        const auto signed_spell_id =
            openwow::ui::game::detail::TruncateLuaNumberToSseI32(lua_tonumber(Ls, 2));
        if (signed_spell_id < 0) {
          return luaL_error(Ls, "Invalid spell ID in %s:SetSpellByID",
                            lua_adapter::ScriptObjectDisplayName(Ls, tooltip_index));
        }

        const auto spell_id = static_cast<std::uint32_t>(signed_spell_id);
        const bool from_pet_book = detail::ScriptReadBoolArgOrDefault(Ls, 3, false);
        const bool show_rank = detail::ScriptReadBoolArgOrDefault(Ls, 4, false);
        if (spell_id == 0 ||
            !openwow::ui::game::detail::FindSpellBookSlotIndexBySpellId(Ls, spell_id, from_pet_book)
                 .has_value()) {
          lua_pushnil(Ls);
          return 1;
        }

        if (!openwow::ui::game::TooltipSystem::Get().SetSpellById(spell_id, show_rank,
                                                                  from_pet_book)) {
          lua_pushnil(Ls);
          return 1;
        }
        SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
        lua_pushnumber(Ls, 1.0);
        return 1;
      },
      0);
  lua_setfield(L, f, "SetSpellByID");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipHyperlink.trampoline>(
      L, f, "SetHyperlink");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipBagItem.trampoline>(
      L, f, "SetBagItem");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipInventoryItem.trampoline>(
      L, f, "SetInventoryItem");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipAction.trampoline>(
      L, f, "SetAction");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipUnitBuff.trampoline>(
      L, f, "SetUnitBuff");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipUnitDebuff.trampoline>(
      L, f, "SetUnitDebuff");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipUnitAura.trampoline>(
      L, f, "SetUnitAura");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipAuctionSellItem.trampoline>(
      L, f, "SetAuctionSellItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipAuctionItem.trampoline>(
      L, f, "SetAuctionItem");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int tooltip_index = ValidateFrameObjectSelf(Ls, "GameTooltip");
        const int result = openwow::ui::game::detail::LuaSetMerchantItem(Ls);
        if (openwow::ui::game::TooltipSystem::Get().GetNumLines() > 0) {
          SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
        }
        return result;
      },
      0);
  lua_setfield(L, f, "SetMerchantItem");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int tooltip_index = ValidateFrameObjectSelf(Ls, "GameTooltip");
        const int result = openwow::ui::game::detail::LuaSetMerchantCostItem(Ls);
        if (openwow::ui::game::TooltipSystem::Get().GetNumLines() > 0) {
          SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
        }
        return result;
      },
      0);
  lua_setfield(L, f, "SetMerchantCostItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipBackpackToken.handler>(
      L, f, "SetBackpackToken");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipBuybackItem.handler>(
      L, f, "SetBuybackItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipCurrencyToken.handler>(
      L, f, "SetCurrencyToken");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipEquipmentSet.handler>(
      L, f, "SetEquipmentSet");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipGlyph.trampoline>(
      L, f, "SetGlyph");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipHyperlinkCompareItem.trampoline>(
      L, f, "SetHyperlinkCompareItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipInboxItem.trampoline>(
      L, f, "SetInboxItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipLootItem.trampoline>(
      L, f, "SetLootItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipLootRollItem.trampoline>(
      L, f, "SetLootRollItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipPossession.handler>(
      L, f, "SetPossession");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipQuestLogSpecialItem.trampoline>(
      L, f, "SetQuestLogSpecialItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipLFGDungeonReward.handler>(
      L, f, "SetLFGDungeonReward");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipLFGCompletionReward.handler>(
      L, f, "SetLFGCompletionReward");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipShapeshift.trampoline>(
      L, f, "SetShapeshift");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipSendMailItem.trampoline>(
      L, f, "SetSendMailItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipSocketedItem.trampoline>(
      L, f, "SetSocketedItem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipExistingSocketGem.trampoline>(
      L, f, "SetExistingSocketGem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipSocketGem.trampoline>(
      L, f, "SetSocketGem");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipTalent.trampoline>(
      L, f, "SetTalent");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipGuildBankItem.handler>(
      L, f, "SetGuildBankItem");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int tooltip_index = ValidateFrameObjectSelf(Ls, "GameTooltip");
        auto &tooltip_system = openwow::ui::game::TooltipSystem::Get();
        auto &tracking = openwow::game::TrackingSystem::Get();

        for (const auto &entry : tracking.GetActiveTracking()) {
          if (entry.spellId != 0) {

            openwow::ui::game::BuildSimpleSpellTooltip(tooltip_system, entry.spellId);
            tooltip_system.Show();
            SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
            return 0;
          }
          if (!entry.name.empty()) {
            tooltip_system.ClearLines();
            tooltip_system.AddLine(entry.name, kGameTooltipGoldR, kGameTooltipGoldG,
                                   kGameTooltipGoldB);
            tooltip_system.Show();
            SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
            return 0;
          }
        }

        const auto lua_tracking_count = tracking.GetLuaTrackingTypeCount();
        for (std::uint32_t index = 1; index <= lua_tracking_count; ++index) {
          const auto info = tracking.GetLuaTrackingInfo(index);
          if (info.has_value() && info->active) {
            tooltip_system.ClearLines();
            tooltip_system.AddLine(info->name, kGameTooltipGoldR, kGameTooltipGoldG,
                                   kGameTooltipGoldB);
            tooltip_system.Show();
            SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
            return 0;
          }
        }

        tooltip_system.ClearLines();
        tooltip_system.AddLine(
            openwow::game::Localization::Get().GetString("MINIMAP_TRACKING_TOOLTIP_NONE"),
            kGameTooltipGoldR, kGameTooltipGoldG, kGameTooltipGoldB);
        tooltip_system.Show();
        SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
        return 0;
      },
      0);
  lua_setfield(L, f, "SetTracking");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        if (!lua_isstring(Ls, 2)) {
          return luaL_error(Ls, "Usage: %s:IsUnit(\"unit\")", "GameTooltip");
        }
        const std::string unit_token = openwow::ui::game::detail::SafeLuaString(Ls, 2);
        auto &ts = openwow::ui::game::TooltipSystem::Get();
        const std::uint64_t tooltip_guid = ts.GetUnitGuid();
        if (tooltip_guid != 0) {
          auto *session = openwow::ui::game::detail::GetWorldSession(Ls);
          if (session != nullptr) {
            const auto resolved = openwow::ui::game::detail::ResolveUnitId(session, unit_token);
            if (!resolved.IsEmpty() && resolved.GetRawValue() == tooltip_guid) {
              lua_pushnumber(Ls, 1.0);
              return 1;
            }
          }
        }
        lua_pushnil(Ls);
        return 1;
      },
      0);
  lua_setfield(L, f, "IsUnit");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        ValidateFrameObjectSelf(Ls, "GameTooltip");
        const auto &tooltip_system = openwow::ui::game::TooltipSystem::Get();
        const auto item_guid = tooltip_system.GetItemGuid();
        if (item_guid != 0) {
          const auto slot =
              openwow::ui::game::detail::RequirePlayerInventoryReplica(Ls).FindSlotByGuid(
                  item_guid);
          if (slot >= openwow::game::InventorySlots::kEquipStart &&
              slot < openwow::game::InventorySlots::kEquipEnd) {
            lua_pushnumber(Ls, 1.0);
            return 1;
          }
        }
        lua_pushnil(Ls);
        return 1;
      },
      0);
  lua_setfield(L, f, "IsEquippedItem");

  lua_pushcfunction(L, detail::kGetTooltipNumLines.trampoline);
  lua_setfield(L, f, "NumLines");

  lua_pushcfunction(L, detail::kFadeTooltip.trampoline);
  lua_setfield(L, f, "FadeOut");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipMinimumWidth.trampoline>(
      L, f, "SetMinimumWidth");

  lua_pushcfunction(L, detail::kGetTooltipMinimumWidth.trampoline);
  lua_setfield(L, f, "GetMinimumWidth");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipPadding.trampoline>(
      L, f, "SetPadding");

  lua_pushcfunction(L, detail::kGetTooltipPadding.trampoline);
  lua_setfield(L, f, "GetPadding");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int tooltip_index = ValidateFrameObjectSelf(Ls, "GameTooltip");
        if (lua_istable(Ls, 2) == 0) {
          return luaL_error(Ls, "Usage: %s:IsOwned(frame)",
                            lua_adapter::ScriptObjectDisplayName(Ls, tooltip_index));
        }

        lua_getfield(Ls, tooltip_index, kTooltipOwnerFrameField);
        const bool owned = lua_istable(Ls, -1) != 0 && lua_rawequal(Ls, -1, 2) != 0;
        lua_pop(Ls, 1);
        lua_pushboolean(Ls, owned);
        return 1;
      },
      0);
  lua_setfield(L, f, "IsOwned");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int tooltip_index = ValidateFrameObjectSelf(Ls, "GameTooltip");
        lua_getfield(Ls, tooltip_index, kTooltipOwnerFrameField);
        if (lua_istable(Ls, -1) == 0) {
          lua_pop(Ls, 1);
          lua_pushnil(Ls);
        }
        return 1;
      },
      0);
  lua_setfield(L, f, "GetOwner");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        const int tooltip_index =
            ValidateFrameObjectSelf(Ls, "GameTooltip");
        if (lua_isstring(Ls, 2) == 0) {
          return luaL_error(
              Ls,
              "Usage: %s:SetAnchorType( anchorType [,Xoffset] [,Yoffset] )",
              lua_adapter::ScriptObjectDisplayName(Ls, tooltip_index));
        }

        auto& tooltip = openwow::ui::game::TooltipSystem::Get();
        if (!tooltip.HasOwner()) {
          return 0;
        }
        const char* const canonical_anchor = StoreTooltipAnchorState(
            Ls, tooltip_index, lua_tostring(Ls, 2),
            ConvertTooltipScriptPixelsToStoredCoordinate(
                ReadTooltipOptionalOffset(Ls, 3)),
            ConvertTooltipScriptPixelsToStoredCoordinate(
                ReadTooltipOptionalOffset(Ls, 4)));
        tooltip.SetAnchor(canonical_anchor);
        ApplyTooltipOwnerAnchor(Ls, tooltip_index, true);
        return 0;
      },
      0);
  lua_setfield(L, f, "SetAnchorType");

  lua_pushcfunction(L, detail::kGetTooltipAnchorType.trampoline);
  lua_setfield(L, f, "GetAnchorType");
}

}
