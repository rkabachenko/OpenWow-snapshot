#pragma once

#include "openwow/render/ui/ui_renderer.h"
#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/lua_taint_api.h"
#include "openwow/ui/widgets/simple_texture.h"
#include "openwow/foundation/text/ascii.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::ui::game::runtime {

struct TextureRenderStateSource {

  std::optional<std::string> texture;

  bool texture_cleared{false};

  std::optional<std::string> portrait_unit;
  std::optional<std::string> portrait_guid;

  bool has_tex_coord_quad{false};
  std::array<double, 8> tex_coord_quad{};

  std::optional<bool> horizontal_tile;
  std::optional<bool> vertical_tile;

  std::array<std::optional<double>, 4> vertex_color{};

  std::array<std::optional<double>, 4> solid_color{};

  std::optional<openwow::ui::widgets::TextureGradientOrientation>
      gradient_orientation;

  std::array<std::optional<double>, 4> gradient_min{};
  std::array<std::optional<double>, 4> gradient_max{};

  std::optional<openwow::render::ui::BlendMode> blend;

  bool desaturated{false};

  struct TabardEmblem {
    std::string source_texture;
    std::string render_target_name;
    std::int32_t width{0};
    std::int32_t height{0};
    std::int32_t pitch{0};
    bool force_white_rgb{false};
    bool copy_source_alpha{false};
  };
  std::optional<TabardEmblem> tabard;
};

inline constexpr char kTextureRenderStateSourceField[] =
    "__ow_native_texture_state";

inline int DestroyTextureRenderStateSource(lua_State* const L) {
  auto* const source =
      static_cast<TextureRenderStateSource*>(lua_touserdata(L, 1));
  if (source != nullptr) {
    source->~TextureRenderStateSource();
  }
  return 0;
}

[[nodiscard]] inline const TextureRenderStateSource*
FindTextureRenderStateSource(lua_State* const L, const int table_index) {
  if (L == nullptr || lua_istable(L, table_index) == 0) {
    return nullptr;
  }
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  RawGetInternedLuaField(L, table_index, kTextureRenderStateSourceField);
  const TextureRenderStateSource* source =
      lua_type(L, -1) == LUA_TUSERDATA
          ? static_cast<const TextureRenderStateSource*>(lua_touserdata(L, -1))
          : nullptr;
  lua_pop(L, 1);
  return source;
}

struct TextureHandleState {

  const std::string* texture_path;
  bool solid_colour{false};
};

[[nodiscard]] inline TextureHandleState ResolveTextureHandleState(
    lua_State* const L, const int table_index,
    const openwow::ui::framexml::UiFrame* const authored) {
  static const std::string kNoTexturePath;
  TextureHandleState state{.texture_path = &kNoTexturePath};
  bool cleared = false;
  if (authored != nullptr) {
    state.texture_path = &authored->file;

    state.solid_colour = authored->file.empty() && authored->has_vertex_color;
  }
  if (const TextureRenderStateSource* const source =
          FindTextureRenderStateSource(L, table_index);
      source != nullptr) {
    if (source->texture.has_value()) {
      state.texture_path = &*source->texture;
    } else if (source->texture_cleared) {
      state.texture_path = &kNoTexturePath;
      cleared = true;
    }
    if (state.texture_path->empty() && !cleared) {
      for (const auto& component : source->solid_color) {
        state.solid_colour = state.solid_colour || component.has_value();
      }
    }
  }
  if (cleared || !state.texture_path->empty()) state.solid_colour = false;
  return state;
}

[[nodiscard]] inline TextureRenderStateSource* EnsureTextureRenderStateSource(
    lua_State* const L, const int table_index) {
  if (L == nullptr || lua_istable(L, table_index) == 0) {
    return nullptr;
  }
  const int table = lua_absindex(L, table_index);
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  RawGetInternedLuaField(L, table, kTextureRenderStateSourceField);
  if (lua_type(L, -1) == LUA_TUSERDATA) {
    auto* const source =
        static_cast<TextureRenderStateSource*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return source;
  }
  lua_pop(L, 1);

  void* const storage = lua_newuserdata(L, sizeof(TextureRenderStateSource));
  auto* const source = new (storage) TextureRenderStateSource();
  const int userdata_index = lua_gettop(L);

  lua_pushlightuserdata(
      L, const_cast<char*>(static_cast<const char*>(
             kTextureRenderStateSourceField)));
  lua_rawget(L, LUA_REGISTRYINDEX);
  if (lua_type(L, -1) != LUA_TTABLE) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushcfunction(L, DestroyTextureRenderStateSource);
    lua_setfield(L, -2, "__gc");
    lua_pushlightuserdata(
        L, const_cast<char*>(static_cast<const char*>(
               kTextureRenderStateSourceField)));
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
  }
  lua_setmetatable(L, userdata_index);
  PushInternedLuaFieldKey(L, kTextureRenderStateSourceField);
  lua_pushvalue(L, userdata_index);
  lua_rawset(L, table);
  lua_pop(L, 1);
  return source;
}

enum class TextureRenderStateField : std::uint8_t {
  kTexture,
  kTextureCleared,
  kPortraitUnit,
  kPortraitGuid,
  kHorizontalTile,
  kVerticalTile,
  kVertexColorR,
  kVertexColorG,
  kVertexColorB,
  kVertexColorA,
  kSolidColorR,
  kSolidColorG,
  kSolidColorB,
  kSolidColorA,
  kGradientOrientation,
  kGradientMinR,
  kGradientMinG,
  kGradientMinB,
  kGradientMinA,
  kGradientMaxR,
  kGradientMaxG,
  kGradientMaxB,
  kGradientMaxA,
  kBlend,
  kDesaturated,
};

[[nodiscard]] inline const char* TextureRenderStateFieldName(
    const TextureRenderStateField field) {
  switch (field) {
    case TextureRenderStateField::kTexture: return "__ow_texture";
    case TextureRenderStateField::kTextureCleared: return "__ow_texture_cleared";
    case TextureRenderStateField::kPortraitUnit: return "__ow_portrait_unit";
    case TextureRenderStateField::kPortraitGuid: return "__ow_portrait_guid";
    case TextureRenderStateField::kHorizontalTile: return "__ow_horiz_tile";
    case TextureRenderStateField::kVerticalTile: return "__ow_vert_tile";
    case TextureRenderStateField::kVertexColorR: return "__ow_vc_r";
    case TextureRenderStateField::kVertexColorG: return "__ow_vc_g";
    case TextureRenderStateField::kVertexColorB: return "__ow_vc_b";
    case TextureRenderStateField::kVertexColorA: return "__ow_vc_a";
    case TextureRenderStateField::kSolidColorR: return "__ow_tex_r";
    case TextureRenderStateField::kSolidColorG: return "__ow_tex_g";
    case TextureRenderStateField::kSolidColorB: return "__ow_tex_b";
    case TextureRenderStateField::kSolidColorA: return "__ow_tex_a";
    case TextureRenderStateField::kGradientOrientation: return "__ow_gradient_orient";
    case TextureRenderStateField::kGradientMinR: return "__ow_grad_min_r";
    case TextureRenderStateField::kGradientMinG: return "__ow_grad_min_g";
    case TextureRenderStateField::kGradientMinB: return "__ow_grad_min_b";
    case TextureRenderStateField::kGradientMinA: return "__ow_grad_min_a";
    case TextureRenderStateField::kGradientMaxR: return "__ow_grad_max_r";
    case TextureRenderStateField::kGradientMaxG: return "__ow_grad_max_g";
    case TextureRenderStateField::kGradientMaxB: return "__ow_grad_max_b";
    case TextureRenderStateField::kGradientMaxA: return "__ow_grad_max_a";
    case TextureRenderStateField::kBlend: return "__ow_blend";
    case TextureRenderStateField::kDesaturated: return "__ow_desat";
  }
  return "";
}

[[nodiscard]] inline std::optional<double>* TextureRenderStateNumberSlot(
    TextureRenderStateSource& source, const TextureRenderStateField field) {
  switch (field) {
    case TextureRenderStateField::kVertexColorR: return &source.vertex_color[0];
    case TextureRenderStateField::kVertexColorG: return &source.vertex_color[1];
    case TextureRenderStateField::kVertexColorB: return &source.vertex_color[2];
    case TextureRenderStateField::kVertexColorA: return &source.vertex_color[3];
    case TextureRenderStateField::kSolidColorR: return &source.solid_color[0];
    case TextureRenderStateField::kSolidColorG: return &source.solid_color[1];
    case TextureRenderStateField::kSolidColorB: return &source.solid_color[2];
    case TextureRenderStateField::kSolidColorA: return &source.solid_color[3];
    case TextureRenderStateField::kGradientMinR: return &source.gradient_min[0];
    case TextureRenderStateField::kGradientMinG: return &source.gradient_min[1];
    case TextureRenderStateField::kGradientMinB: return &source.gradient_min[2];
    case TextureRenderStateField::kGradientMinA: return &source.gradient_min[3];
    case TextureRenderStateField::kGradientMaxR: return &source.gradient_max[0];
    case TextureRenderStateField::kGradientMaxG: return &source.gradient_max[1];
    case TextureRenderStateField::kGradientMaxB: return &source.gradient_max[2];
    case TextureRenderStateField::kGradientMaxA: return &source.gradient_max[3];
    default: return nullptr;
  }
}

[[nodiscard]] inline openwow::render::ui::BlendMode
ResolveTextureBlendModeName(const std::string_view mode) {
  using openwow::render::ui::BlendMode;
  if (openwow::text::EqualsIgnoreCaseAscii(mode, "DISABLE")) return BlendMode::kOpaque;
  if (openwow::text::EqualsIgnoreCaseAscii(mode, "ALPHAKEY")) return BlendMode::kAlphaKey;
  if (openwow::text::EqualsIgnoreCaseAscii(mode, "ADD")) return BlendMode::kAdditive;
  if (openwow::text::EqualsIgnoreCaseAscii(mode, "MOD")) return BlendMode::kModulate;
  return BlendMode::kAlpha;
}

[[nodiscard]] inline std::optional<openwow::ui::widgets::TextureGradientOrientation>
ResolveTextureGradientOrientationName(const std::string_view orientation) {
  using openwow::ui::widgets::TextureGradientOrientation;
  if (openwow::text::EqualsIgnoreCaseAscii(orientation, "HORIZONTAL")) {
    return TextureGradientOrientation::Horizontal;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(orientation, "VERTICAL")) {
    return TextureGradientOrientation::Vertical;
  }
  return std::nullopt;
}

inline void SetTextureRenderStateString(
    lua_State* const L, const int table_index,
    const TextureRenderStateField field,
    const std::optional<std::string_view> value) {
  if (L == nullptr || lua_istable(L, table_index) == 0) {
    return;
  }
  const int table = lua_absindex(L, table_index);
  if (value.has_value()) {
    lua_pushlstring(L, value->data(), value->size());
  } else {
    lua_pushnil(L);
  }
  SetInternedLuaField(L, table, TextureRenderStateFieldName(field));

  TextureRenderStateSource* const source = EnsureTextureRenderStateSource(L, table);
  switch (field) {
    case TextureRenderStateField::kTexture:
      source->texture = value;
      break;
    case TextureRenderStateField::kPortraitUnit:
      source->portrait_unit = value;
      break;
    case TextureRenderStateField::kPortraitGuid:
      source->portrait_guid = value;
      break;
    case TextureRenderStateField::kGradientOrientation:
      source->gradient_orientation =
          value.has_value() ? ResolveTextureGradientOrientationName(*value)
                            : std::nullopt;
      break;
    case TextureRenderStateField::kBlend:
      source->blend = value.has_value()
                          ? std::optional(ResolveTextureBlendModeName(*value))
                          : std::nullopt;
      break;
    default:
      break;
  }
}

inline void SetTextureRenderStateBoolean(lua_State* const L,
                                         const int table_index,
                                         const TextureRenderStateField field,
                                         const bool value) {
  if (L == nullptr || lua_istable(L, table_index) == 0) {
    return;
  }
  const int table = lua_absindex(L, table_index);
  lua_pushboolean(L, value ? 1 : 0);
  SetInternedLuaField(L, table, TextureRenderStateFieldName(field));

  TextureRenderStateSource* const source = EnsureTextureRenderStateSource(L, table);
  switch (field) {
    case TextureRenderStateField::kTextureCleared:
      source->texture_cleared = value;
      break;
    case TextureRenderStateField::kHorizontalTile:
      source->horizontal_tile = value;
      break;
    case TextureRenderStateField::kVerticalTile:
      source->vertical_tile = value;
      break;
    case TextureRenderStateField::kDesaturated:
      source->desaturated = value;
      break;
    default:
      break;
  }
}

inline void SetTextureRenderStateNumber(lua_State* const L,
                                        const int table_index,
                                        const TextureRenderStateField field,
                                        const double value) {
  if (L == nullptr || lua_istable(L, table_index) == 0) {
    return;
  }
  const int table = lua_absindex(L, table_index);
  lua_pushnumber(L, value);
  SetInternedLuaField(L, table, TextureRenderStateFieldName(field));

  TextureRenderStateSource* const source = EnsureTextureRenderStateSource(L, table);
  if (std::optional<double>* const slot =
          TextureRenderStateNumberSlot(*source, field);
      slot != nullptr) {
    *slot = value;
  }
}

inline void SetTextureRenderStateTexCoordQuad(
    lua_State* const L, const int table_index, const double upper_left_x,
    const double upper_left_y, const double lower_left_x,
    const double lower_left_y, const double upper_right_x,
    const double upper_right_y, const double lower_right_x,
    const double lower_right_y) {
  if (L == nullptr || lua_istable(L, table_index) == 0) {
    return;
  }
  const int table = lua_absindex(L, table_index);
  static constexpr const char* const kCornerFieldNames[8] = {
      "__ow_tc_ul_x", "__ow_tc_ul_y", "__ow_tc_ll_x", "__ow_tc_ll_y",
      "__ow_tc_ur_x", "__ow_tc_ur_y", "__ow_tc_lr_x", "__ow_tc_lr_y",
  };
  const std::array<double, 8> quad{
      upper_left_x,  upper_left_y,  lower_left_x,  lower_left_y,
      upper_right_x, upper_right_y, lower_right_x, lower_right_y,
  };
  for (std::size_t index = 0; index < quad.size(); ++index) {
    lua_pushnumber(L, quad[index]);
    SetInternedLuaField(L, table, kCornerFieldNames[index]);
  }

  TextureRenderStateSource* const source = EnsureTextureRenderStateSource(L, table);
  source->has_tex_coord_quad = true;
  source->tex_coord_quad = quad;
}

inline void SetTextureRenderStateTabardEmblem(
    lua_State* const L, const int table_index,
    const std::string_view source_texture,
    const std::string_view render_target_name, const std::int32_t width,
    const std::int32_t height, const std::int32_t pitch,
    const bool force_white_rgb, const bool copy_source_alpha) {
  if (L == nullptr || lua_istable(L, table_index) == 0) {
    return;
  }
  const int table = lua_absindex(L, table_index);
  lua_pushlstring(L, source_texture.data(), source_texture.size());
  SetInternedLuaField(L, table, "__ow_tabard_rt_source_texture");
  lua_pushlstring(L, render_target_name.data(), render_target_name.size());
  SetInternedLuaField(L, table, "__ow_tabard_rt_name");
  lua_pushinteger(L, static_cast<lua_Integer>(width));
  SetInternedLuaField(L, table, "__ow_tabard_rt_width");
  lua_pushinteger(L, static_cast<lua_Integer>(height));
  SetInternedLuaField(L, table, "__ow_tabard_rt_height");
  lua_pushinteger(L, static_cast<lua_Integer>(pitch));
  SetInternedLuaField(L, table, "__ow_tabard_rt_pitch");
  lua_pushboolean(L, force_white_rgb ? 1 : 0);
  SetInternedLuaField(L, table, "__ow_tabard_rt_force_white_rgb");
  lua_pushboolean(L, copy_source_alpha ? 1 : 0);
  SetInternedLuaField(L, table, "__ow_tabard_rt_copy_source_alpha");

  TextureRenderStateSource* const source = EnsureTextureRenderStateSource(L, table);
  source->tabard = TextureRenderStateSource::TabardEmblem{
      .source_texture = std::string(source_texture),
      .render_target_name = std::string(render_target_name),
      .width = width,
      .height = height,
      .pitch = pitch,
      .force_white_rgb = force_white_rgb,
      .copy_source_alpha = copy_source_alpha,
  };
}

}
