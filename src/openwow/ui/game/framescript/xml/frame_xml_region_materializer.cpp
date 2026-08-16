#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/widgets/button_method_support.h"
#include "openwow/ui/game/framescript/core/frame_anchor_runtime.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_face.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/xml/frame_xml_region_materializer.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/widgets/texture_asset_probe.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/render/resources/fonts/font_string_flags.h"
#include "openwow/ui/framexml/anchor_semantics.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/ui/widgets/status_bar.h"

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

void SetLuaStringField(lua_State *L, int table_index, const char *field_name,
                       const std::string_view value) {
  table_index = lua_absindex(L, table_index);
  lua_pushlstring(L, value.data(), value.size());
  lua_setfield(L, table_index, field_name);
}

void SetLuaNumberField(lua_State *L, int table_index, const char *field_name,
                       const double value) {
  table_index = lua_absindex(L, table_index);
  lua_pushnumber(L, value);
  lua_setfield(L, table_index, field_name);
}

void SetLuaBoolField(lua_State *L, int table_index, const char *field_name,
                     const bool value) {
  table_index = lua_absindex(L, table_index);
  lua_pushboolean(L, value ? 1 : 0);
  lua_setfield(L, table_index, field_name);
}

bool FloatDiffers(float lhs, float rhs) {
  return std::fabs(lhs - rhs) > std::numeric_limits<float>::epsilon();
}

bool HasColorOverride(const openwow::ui::framexml::UiFrame &frame) {
  return FloatDiffers(frame.color_r, 1.0f) ||
         FloatDiffers(frame.color_g, 1.0f) ||
         FloatDiffers(frame.color_b, 1.0f) ||
         FloatDiffers(frame.color_a, 1.0f);
}

void StoreQuantizedColorField(lua_State *L, int table_index,
                              const char *field_name, const float value) {
  SetLuaNumberField(
      L, table_index, field_name,
      NormalizeScriptColorByte(QuantizeScriptColorByte(value)));
}

void StoreQuantizedRenderStateColorField(
    lua_State *L, int table_index,
    const runtime::TextureRenderStateField field, const float value) {
  runtime::SetTextureRenderStateNumber(
      L, table_index, field,
      NormalizeScriptColorByte(QuantizeScriptColorByte(value)));
}

void StoreTemplateAnchor(lua_State *L,
                         const openwow::ui::framexml::UiAnchor &anchor) {
  lua_newtable(L);
  const int anchor_index = lua_absindex(L, -1);
  SetLuaStringField(L, anchor_index, "point",
                    openwow::ui::framexml::EffectiveAnchorPoint(anchor));
  if (!anchor.relative_to.empty()) {
    SetLuaStringField(L, anchor_index, "relativeTo", anchor.relative_to);
  }
  SetLuaStringField(
      L, anchor_index, "relativePoint",
      openwow::ui::framexml::EffectiveAnchorRelativePoint(anchor));
  SetLuaNumberField(L, anchor_index, "x", anchor.x);
  SetLuaNumberField(L, anchor_index, "y", anchor.y);
  if (anchor.flags != 0u) {
    lua_pushinteger(L, static_cast<lua_Integer>(anchor.flags));
    lua_setfield(L, anchor_index, "__ow_flags");
  }
}

void ApplyTemplateAnchors(lua_State *L, int region_index,
                          const openwow::ui::framexml::UiFrame &frame) {
  region_index = lua_absindex(L, region_index);
  if (frame.set_all_points) {
    lua_pushboolean(L, 1);
    lua_setfield(L, region_index, "__ow_setAllPoints");
    lua_pushnil(L);
    lua_setfield(L, region_index, "__ow_anchors");
    openwow::ui::game::detail::ReindexLuaAnchorDependents(L, region_index);
    return;
  }

  if (frame.anchors.empty()) {
    return;
  }

  lua_pushnil(L);
  lua_setfield(L, region_index, "__ow_setAllPoints");
  lua_newtable(L);
  const int anchors_index = lua_absindex(L, -1);
  lua_Integer next_anchor = 1;
  for (const auto &anchor : frame.anchors) {
    StoreTemplateAnchor(L, anchor);
    lua_rawseti(L, anchors_index, next_anchor++);
  }
  NormalizeLuaAnchorArray(L, anchors_index);
  lua_setfield(L, region_index, "__ow_anchors");
  openwow::ui::game::detail::ReindexLuaAnchorDependents(L, region_index);
}

void ApplyTemplateCommonRegionFields(
    lua_State *L, int region_index,
    const openwow::ui::framexml::UiFrame &frame,
    const bool explicit_draw_layer_argument) {
  region_index = lua_absindex(L, region_index);

  if (frame.width.has_value()) {
    SetLuaNumberField(L, region_index, "__ow_width", *frame.width);
  }
  if (frame.height.has_value()) {
    SetLuaNumberField(L, region_index, "__ow_height", *frame.height);
  }
  if (!frame.visible) {
    lua_pushboolean(L, 0);
    lua_setfield(L, region_index, "__ow_visible");
  }
  if (!explicit_draw_layer_argument && !frame.draw_layer.empty()) {
    int draw_layer_id = 0;
    if (TryParseDrawLayerName(frame.draw_layer.c_str(), &draw_layer_id)) {
      SetLuaStringField(L, region_index, "__ow_draw_layer",
                        GetDrawLayerNameById(draw_layer_id));
    }
  }
  ApplyTemplateAnchors(L, region_index, frame);
}

void StoreTemplateTextureQuad(lua_State *L, int texture_index,
                              const openwow::ui::framexml::UiTextureCoordQuad &quad) {
  texture_index = lua_absindex(L, texture_index);
  runtime::SetTextureRenderStateTexCoordQuad(
      L, texture_index, quad.upper_left.u, quad.upper_left.v,
      quad.lower_left.u, quad.lower_left.v, quad.upper_right.u,
      quad.upper_right.v, quad.lower_right.u, quad.lower_right.v);
  SetLuaNumberField(L, texture_index, "__ow_tc_l", quad.upper_left.u);
  SetLuaNumberField(L, texture_index, "__ow_tc_r", quad.upper_right.u);
  SetLuaNumberField(L, texture_index, "__ow_tc_t", quad.upper_left.v);
  SetLuaNumberField(L, texture_index, "__ow_tc_b", quad.lower_left.v);
}

void ApplyTemplateTextureGradient(
    lua_State *L, int texture_index,
    const openwow::ui::framexml::TextureGradient &gradient) {
  if (!gradient.enabled) {
    return;
  }

  using runtime::TextureRenderStateField;
  constexpr std::array<TextureRenderStateField, 4> kTemplateGradientMinColorFields{{
      TextureRenderStateField::kGradientMinR,
      TextureRenderStateField::kGradientMinG,
      TextureRenderStateField::kGradientMinB,
      TextureRenderStateField::kGradientMinA,
  }};
  constexpr std::array<TextureRenderStateField, 4> kTemplateGradientMaxColorFields{{
      TextureRenderStateField::kGradientMaxR,
      TextureRenderStateField::kGradientMaxG,
      TextureRenderStateField::kGradientMaxB,
      TextureRenderStateField::kGradientMaxA,
  }};

  runtime::SetTextureRenderStateString(
      L, texture_index, TextureRenderStateField::kGradientOrientation,
      std::string_view(
          gradient.orientation ==
                  openwow::ui::framexml::TextureGradientOrientation::kVertical
              ? "VERTICAL"
              : "HORIZONTAL"));

  const auto store_color = [&](const openwow::ui::framexml::TextureGradientColor &color,
                               const std::array<TextureRenderStateField, 4> &fields) {
    StoreQuantizedRenderStateColorField(L, texture_index, fields[0], color.r);
    StoreQuantizedRenderStateColorField(L, texture_index, fields[1], color.g);
    StoreQuantizedRenderStateColorField(L, texture_index, fields[2], color.b);
    StoreQuantizedRenderStateColorField(L, texture_index, fields[3], color.a);
  };
  store_color(gradient.min_color, kTemplateGradientMinColorFields);
  store_color(gradient.max_color, kTemplateGradientMaxColorFields);
}

void ApplyTextureTemplate(lua_State *L, int texture_index,
                          const openwow::ui::framexml::UiFrame &frame,
                          const bool explicit_draw_layer_argument) {
  texture_index = lua_absindex(L, texture_index);
  ApplyTemplateCommonRegionFields(L, texture_index, frame,
                                  explicit_draw_layer_argument);

  using runtime::TextureRenderStateField;
  if (!frame.file.empty()) {
    runtime::SetTextureRenderStateString(
        L, texture_index, TextureRenderStateField::kTexture,
        std::string_view(frame.file));
    runtime::SetTextureRenderStateBoolean(
        L, texture_index, TextureRenderStateField::kTextureCleared, false);

    QueueRegionTextureLoad(L, frame.file);
  }
  if (!frame.alpha_mode.empty()) {
    const std::string blend_mode =
        openwow::text::ToUpperAscii(frame.alpha_mode);
    runtime::SetTextureRenderStateString(
        L, texture_index, TextureRenderStateField::kBlend,
        std::string_view(blend_mode));
  }
  if (frame.tile_x_explicit || frame.tile_x) {
    runtime::SetTextureRenderStateBoolean(
        L, texture_index, TextureRenderStateField::kHorizontalTile,
        frame.tile_x);
  }
  if (frame.tile_y_explicit || frame.tile_y) {
    runtime::SetTextureRenderStateBoolean(
        L, texture_index, TextureRenderStateField::kVerticalTile,
        frame.tile_y);
  }

  if (openwow::ui::framexml::TextureCoordinatesWereSpecified(frame)) {
    StoreTemplateTextureQuad(
        L, texture_index,
        openwow::ui::framexml::EffectiveTextureCoordinates(frame));
  }

  if (frame.has_vertex_color || HasColorOverride(frame)) {
    StoreQuantizedRenderStateColorField(
        L, texture_index, TextureRenderStateField::kVertexColorR, frame.color_r);
    StoreQuantizedRenderStateColorField(
        L, texture_index, TextureRenderStateField::kVertexColorG, frame.color_g);
    StoreQuantizedRenderStateColorField(
        L, texture_index, TextureRenderStateField::kVertexColorB, frame.color_b);
    StoreTextureAlphaByte(L, texture_index,
                          QuantizeScriptColorByte(frame.color_a));
  }
  if (frame.texture_alpha.has_value()) {
    StoreTextureAlphaByte(
        L, texture_index,
        openwow::ui::game::QuantizeFrameAlphaByteTruncated(
            *frame.texture_alpha));
  }
  ApplyTemplateTextureGradient(L, texture_index, frame.gradient);
}

void BindNamedFontStyleIfPresent(lua_State *L, int font_string_index,
                                 const std::string &font_style) {
  if (font_style.empty()) {
    return;
  }
  if (!PushNamedFontObject(L, font_style.c_str())) {
    lua_pop(L, 1);
    return;
  }

  SetBoundFontObject(L, font_string_index, -1);
  CopyNamedFontObjectStyle(L, font_string_index, -1);
  lua_pop(L, 1);
}

void ApplyFrameXmlFontReference(
    lua_State* L, int font_string_index,
    const openwow::ui::framexml::UiFrame& frame) {
  if (frame.font_reference.empty()) {
    return;
  }

  font_string_index = lua_absindex(L, font_string_index);
  if (PushNamedFontObject(L, frame.font_reference.c_str())) {
    SetBoundFontObject(L, font_string_index, -1);
    CopyNamedFontObjectStyle(L, font_string_index, -1);
    lua_pop(L, 1);
    return;
  }
  lua_pop(L, 1);

  if (!frame.has_font_height ||
      !(frame.text_height_stored > std::numeric_limits<float>::epsilon())) {
    return;
  }
  const auto* manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
  const auto* vfs = manager != nullptr ? manager->vfs() : nullptr;
  if (!ValidateLuaFontObjectFace(frame.font_reference,
                                 frame.text_height_stored, vfs)) {
    return;
  }

  SetLuaStringField(L, font_string_index, "__ow_font_path",
                    frame.font_reference);
  SetLuaNumberField(
      L, font_string_index, "__ow_font_size",
      openwow::ui::StoredUiHorizontalCoordinateToPixels(
          frame.text_height_stored));
  SetLuaNumberField(L, font_string_index, "__ow_text_height",
                    frame.text_height_stored);

  std::uint32_t flags = 0;
  if (openwow::text::EqualsIgnoreCaseAscii(frame.font_outline, "NORMAL")) {
    flags |= 0x1U;
  } else if (openwow::text::EqualsIgnoreCaseAscii(frame.font_outline,
                                                   "THICK")) {
    flags |= 0x5U;
  }
  if (frame.font_monochrome.value_or(false)) {
    flags |= 0x2U;
  }
  if (flags != 0U) {
    SetLuaStringField(L, font_string_index, "__ow_font_flags",
                      openwow::render::CanonicalizeFontFlagsString(flags));
  }
}

void StoreTemplateJustify(lua_State *L, int font_string_index,
                          const char *field_name,
                          const std::string &value) {
  if (value.empty()) {
    return;
  }

  std::uint32_t flags = 0;
  if (openwow::ui::JustifyStringToFlags(value.c_str(), &flags) == 0) {
    return;
  }
  SetLuaStringField(L, font_string_index, field_name,
                    openwow::ui::JustifyFlagsToString(flags));
}

void ApplyFontStringTemplate(lua_State *L, int font_string_index,
                             const openwow::ui::framexml::UiFrame &frame,
                             const bool explicit_draw_layer_argument) {
  font_string_index = lua_absindex(L, font_string_index);
  ApplyTemplateCommonRegionFields(L, font_string_index, frame,
                                  explicit_draw_layer_argument);

  StoreTemplateJustify(L, font_string_index, "__ow_justifyH",
                       frame.justify_h);
  StoreTemplateJustify(L, font_string_index, "__ow_justifyV",
                       frame.justify_v);
  if (!frame.text.empty()) {
    SetLuaStringField(L, font_string_index, "__ow_text", frame.text);
  }
  if (frame.word_wrap.has_value()) {
    SetLuaBoolField(L, font_string_index, "__ow_wordwrap", *frame.word_wrap);
  }
  if (frame.non_space_wrap.has_value()) {
    SetLuaBoolField(L, font_string_index, "__ow_nonspacewrap",
                    *frame.non_space_wrap);
  }
  if (frame.indented_word_wrap.has_value()) {
    SetLuaBoolField(L, font_string_index, "__ow_indented_wrap",
                    *frame.indented_word_wrap);
  }
  if (frame.has_text_spacing) {
    SetLuaNumberField(L, font_string_index, "__ow_spacing",
                      frame.text_spacing_stored);
  }
  if (frame.has_font_height && frame.text_height_stored != 0.0f) {
    SetLuaNumberField(L, font_string_index, "__ow_text_height",
                      frame.text_height_stored);
  }
  if (frame.max_lines != 0) {
    lua_pushinteger(L, static_cast<lua_Integer>(frame.max_lines));
    lua_setfield(L, font_string_index, "__ow_maxlines");
  }
  if (frame.has_text_color || HasColorOverride(frame)) {
    StoreQuantizedColorField(L, font_string_index, "__ow_text_r",
                             frame.color_r);
    StoreQuantizedColorField(L, font_string_index, "__ow_text_g",
                             frame.color_g);
    StoreQuantizedColorField(L, font_string_index, "__ow_text_b",
                             frame.color_b);
    StoreSharedFontAlphaByte(L, font_string_index,
                             QuantizeScriptColorByte(frame.color_a));
  }
  if (frame.has_text_shadow) {
    StoreQuantizedColorField(L, font_string_index, "__ow_shadow_r",
                             frame.text_shadow_r);
    StoreQuantizedColorField(L, font_string_index, "__ow_shadow_g",
                             frame.text_shadow_g);
    StoreQuantizedColorField(L, font_string_index, "__ow_shadow_b",
                             frame.text_shadow_b);
    StoreQuantizedColorField(L, font_string_index, "__ow_shadow_a",
                             frame.text_shadow_a);
    SetLuaNumberField(L, font_string_index, "__ow_shadow_x",
                      frame.text_shadow_x);
    SetLuaNumberField(L, font_string_index, "__ow_shadow_y",
                      frame.text_shadow_y);
  }
}

void ApplyFrameXmlRegionDefinition(
    lua_State *L, int region_index,
    const openwow::ui::framexml::UiFrame &frame) {
  if (L == nullptr || lua_istable(L, region_index) == 0) {
    return;
  }

  region_index = lua_absindex(L, region_index);
  const auto type =
      openwow::ui::widgets::ScriptObjectTypeFromName(frame.kind);
  if (type == openwow::ui::widgets::ScriptObjectType::Texture) {
    ApplyTextureTemplate(L, region_index, frame, false);
  } else if (type == openwow::ui::widgets::ScriptObjectType::FontString) {
    BindNamedFontStyleIfPresent(L, region_index, frame.font_style);
    ApplyFrameXmlFontReference(L, region_index, frame);
    ApplyFontStringTemplate(L, region_index, frame, false);
  } else {
    return;
  }

  SyncRegionDrawLayerEnabled(L, region_index);
}

void ApplyFrameXmlMessageFontDefinition(
    lua_State* L, int message_frame_index,
    const openwow::ui::framexml::UiFrame& font_definition) {
  if (L == nullptr || lua_istable(L, message_frame_index) == 0) {
    return;
  }
  message_frame_index = lua_absindex(L, message_frame_index);
  BindNamedFontStyleIfPresent(L, message_frame_index,
                              font_definition.font_style);
  StoreTemplateJustify(L, message_frame_index, "__ow_justifyH",
                       font_definition.justify_h);
  StoreTemplateJustify(L, message_frame_index, "__ow_justifyV",
                       font_definition.justify_v);
  if (font_definition.indented_word_wrap.has_value()) {
    SetLuaBoolField(L, message_frame_index, "__ow_indented_wrap",
                    *font_definition.indented_word_wrap);
  }
  if (font_definition.non_space_wrap.has_value()) {
    SetLuaBoolField(L, message_frame_index, "__ow_nonspacewrap",
                    *font_definition.non_space_wrap);
  }
  if (HasColorOverride(font_definition)) {
    StoreQuantizedColorField(L, message_frame_index, "__ow_text_r",
                             font_definition.color_r);
    StoreQuantizedColorField(L, message_frame_index, "__ow_text_g",
                             font_definition.color_g);
    StoreQuantizedColorField(L, message_frame_index, "__ow_text_b",
                             font_definition.color_b);
    StoreSharedFontAlphaByte(
        L, message_frame_index,
        QuantizeScriptColorByte(font_definition.color_a));
  }
}

const char *ResolveInheritedFontStringDefaultAnchorPoint(lua_State *L,
                                                         int font_string_index) {
  std::uint32_t flags = 0;
  const char *justify = openwow::ui::BorrowRawLuaStringField(L, font_string_index,
                                            "__ow_justifyH");
  if (openwow::ui::StringToHorizontalJustify(justify, &flags) == 0) {
    return "CENTER";
  }
  if ((flags & 0x1u) != 0u) {
    return "LEFT";
  }
  if ((flags & 0x4u) != 0u) {
    return "RIGHT";
  }
  return "CENTER";
}

void SetInheritedFontStringDefaultAnchor(lua_State *L, int owner_index,
                                         int font_string_index) {
  owner_index = lua_absindex(L, owner_index);
  font_string_index = lua_absindex(L, font_string_index);
  const char *point =
      ResolveInheritedFontStringDefaultAnchorPoint(L, font_string_index);

  lua_pushnil(L);
  lua_setfield(L, font_string_index, "__ow_setAllPoints");
  lua_newtable(L);
  const int anchors_index = lua_absindex(L, -1);
  lua_newtable(L);
  const int anchor_index = lua_absindex(L, -1);
  lua_pushstring(L, point);
  lua_setfield(L, anchor_index, "point");
  lua_pushstring(L, point);
  lua_setfield(L, anchor_index, "relativePoint");
  const char *owner_name = openwow::ui::BorrowRawLuaStringField(L, owner_index, "__ow_name");
  if (owner_name != nullptr && owner_name[0] != '\0') {
    lua_pushstring(L, owner_name);
    lua_setfield(L, anchor_index, "relativeTo");
  }
  lua_pushnumber(L, 0.0);
  lua_setfield(L, anchor_index, "x");
  lua_pushnumber(L, 0.0);
  lua_setfield(L, anchor_index, "y");
  lua_rawseti(L, anchors_index, 1);
  lua_setfield(L, font_string_index, "__ow_anchors");
  openwow::ui::game::detail::ReindexLuaAnchorDependents(L, font_string_index);
}

void ApplyResolvedTextureTemplates(
    lua_State *L, int texture_index, const TemplateResolveResult &templates,
    const bool explicit_draw_layer_argument) {
  for (const auto *frame : templates.base_to_derived) {
    ApplyTextureTemplate(L, texture_index, *frame,
                         explicit_draw_layer_argument);
  }
}

void ApplyResolvedFontStringTemplates(
    lua_State *L, int owner_index, int font_string_index,
    const TemplateResolveResult &templates,
    const bool explicit_draw_layer_argument) {
  owner_index = lua_absindex(L, owner_index);
  font_string_index = lua_absindex(L, font_string_index);
  for (const auto *frame : templates.base_to_derived) {
    BindNamedFontStyleIfPresent(L, font_string_index, frame->font_style);
  }
  for (const auto *frame : templates.base_to_derived) {
    ApplyFontStringTemplate(L, font_string_index, *frame,
                            explicit_draw_layer_argument);
  }
  if (!templates.base_to_derived.empty() &&
      !FontStringHasUsableAnchors(L, font_string_index)) {
    SetInheritedFontStringDefaultAnchor(L, owner_index, font_string_index);
  }
}

void BindButtonFontStringRegion(lua_State *L, int button_idx,
                                int font_string_idx) {
  button_idx = lua_absindex(L, button_idx);
  font_string_idx = lua_absindex(L, font_string_idx);

  lua_getfield(L, button_idx, "__ow_btn_fontstr");
  if (lua_istable(L, -1) != 0 && lua_rawequal(L, -1, font_string_idx) != 0) {
    lua_pop(L, 1);
    return;
  }

  if (lua_istable(L, -1) != 0) {
    const int current_idx = lua_absindex(L, -1);
    RemoveExactValueFromArrayField(L, button_idx, "__ow_regions", current_idx);
    lua_getfield(L, current_idx, "__ow_parent");
    if (lua_rawequal(L, -1, button_idx) != 0) {
      lua_pushnil(L);
      lua_setfield(L, current_idx, "__ow_parent");
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  lua_pushvalue(L, font_string_idx);
  lua_setfield(L, button_idx, "__ow_btn_fontstr");

  lua_getfield(L, font_string_idx, "__ow_parent");
  if (lua_istable(L, -1) != 0 && lua_rawequal(L, -1, button_idx) == 0) {
    RemoveExactValueFromArrayField(L, -1, "__ow_regions", font_string_idx);
  }
  lua_pop(L, 1);

  lua_pushvalue(L, button_idx);
  lua_setfield(L, font_string_idx, "__ow_parent");
  lua_pushboolean(L, 1);
  lua_setfield(L, font_string_idx, "__ow_visible");

  if (!ArrayFieldContainsExactValue(L, button_idx, "__ow_regions",
                                    font_string_idx)) {
    lua_pushvalue(L, font_string_idx);
    PrependToRegions(L, button_idx);
  }
  SyncRegionDrawLayerEnabled(L, font_string_idx);

  if (!FontStringHasUsableAnchors(L, font_string_idx)) {
    const char *point =
        ResolveButtonLabelAnchorPoint(L, button_idx, font_string_idx);
    SetButtonFontStringDefaultAnchor(L, button_idx, font_string_idx, point);
  } else {
    ResolveButtonLabelAnchorPoint(L, button_idx, font_string_idx);
  }
}

const char *NativeTextureSlotField(
    const openwow::ui::framexml::UiFrame &frame) {
  using TextureRole = openwow::ui::framexml::UiFrame::TextureRole;
  switch (frame.texture_role) {
    case TextureRole::ButtonNormal:
      return "__ow_btn_normal_tex";
    case TextureRole::ButtonPushed:
      return "__ow_btn_pushed_tex";
    case TextureRole::ButtonDisabled:
      return "__ow_btn_disabled_tex";
    case TextureRole::ButtonHighlight:
      return "__ow_btn_highlight_tex";
    case TextureRole::CheckButtonChecked:
      return "__ow_checked_tex";
    case TextureRole::CheckButtonDisabledChecked:
      return "__ow_disabled_checked_tex";
    case TextureRole::SliderThumb:
      return "__ow_sl_thumb";
    case TextureRole::StatusBarFill:
      return "__ow_sb_texture";
    case TextureRole::ColorSelectWheel:
    case TextureRole::ColorSelectWheelThumb:
    case TextureRole::ColorSelectValue:
    case TextureRole::ColorSelectValueThumb:
    case TextureRole::Normal:
      return nullptr;
  }
  return nullptr;
}

bool BindNativeTextureRegion(
    lua_State *L, int owner_index, int texture_index,
    const openwow::ui::framexml::UiFrame &frame) {
  using ScriptObjectType = openwow::ui::widgets::ScriptObjectType;
  using TextureRole = openwow::ui::framexml::UiFrame::TextureRole;

  owner_index = lua_absindex(L, owner_index);
  texture_index = lua_absindex(L, texture_index);

  const char *slot = NativeTextureSlotField(frame);
  if (slot == nullptr) {
    return false;
  }

  ScriptObjectType required_owner = ScriptObjectType::COUNT_;
  switch (frame.texture_role) {
    case TextureRole::ButtonNormal:
    case TextureRole::ButtonPushed:
    case TextureRole::ButtonDisabled:
    case TextureRole::ButtonHighlight:
      required_owner = ScriptObjectType::Button;
      break;
    case TextureRole::CheckButtonChecked:
    case TextureRole::CheckButtonDisabledChecked:
      required_owner = ScriptObjectType::CheckButton;
      break;
    case TextureRole::SliderThumb:
      required_owner = ScriptObjectType::Slider;
      break;
    case TextureRole::StatusBarFill:
      required_owner = ScriptObjectType::StatusBar;
      break;
    case TextureRole::ColorSelectWheel:
    case TextureRole::ColorSelectWheelThumb:
    case TextureRole::ColorSelectValue:
    case TextureRole::ColorSelectValueThumb:
    case TextureRole::Normal:
      return false;
  }

  if (!openwow::ui::game::detail::LuaScriptObjectIsKindOfCanonicalType(
          L, owner_index, required_owner)) {
    return false;
  }

  BindTextureOwnership(L, texture_index, owner_index, frame.texture_role);
  lua_pushvalue(L, texture_index);
  lua_setfield(L, owner_index, slot);
  if (frame.texture_role == TextureRole::StatusBarFill) {
    if (auto* status_bar = dynamic_cast<openwow::ui::widgets::StatusBar*>(
            lua_adapter::BorrowNativeScriptObject(L, owner_index));
        status_bar != nullptr) {

      if (const auto initial_color =
              status_bar->AttachTexture(lua_topointer(L, texture_index));
          initial_color.has_value()) {
        (void)ApplyTextureVertexColor(L, texture_index, *initial_color);
      }
      (void)ApplyStatusBarTextureRotation(L, texture_index,
                                         status_bar->Snapshot().rotates_texture);
    }
  }
  return true;
}

void FireScript(lua_State *L, int frame_idx, const char *handler, int extra_args) {
  frame_idx = lua_absindex(L, frame_idx);
  const auto invocation = InvokeFrameScriptHandler(
      L, frame_idx, handler, extra_args);
  if (invocation.status != LUA_OK) {
    const char* const message =
        lua_isstring(L, -1) != 0 ? lua_tostring(L, -1) : nullptr;
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        std::string("FrameScript handler ") + handler + " failed: " +
            (message != nullptr ? message : "unknown Lua error"));
    lua_pop(L, 1);
  }
}

void FirePendingTooltipMoneyScript(lua_State *L, int tooltip_index) {
  auto pending_money = openwow::ui::game::TooltipSystem::Get().TakePendingMoneyScript();
  if (!pending_money.has_value()) {
    return;
  }

  lua_pushnumber(L, static_cast<lua_Number>(pending_money->cost));
  if (pending_money->max_cost.has_value()) {
    lua_pushnumber(L, static_cast<lua_Number>(*pending_money->max_cost));
  } else {
    lua_pushnil(L);
  }
  FireScript(L, tooltip_index, "OnTooltipAddMoney", 2);
}

}
