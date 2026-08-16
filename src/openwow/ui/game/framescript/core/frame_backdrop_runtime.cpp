#include "openwow/ui/game/framescript/core/frame_backdrop_runtime.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/runtime/frame_materializer.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/framexml/framexml_parser_detail.h"
#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/script_boolean.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <lua.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::ui::game::frame_api {

openwow::ui::widgets::CSimpleFrame *GetLuaBackdropFrame(lua_State *L,
                                                        int frame_index) {
  auto *script_object = lua_adapter::BorrowNativeScriptObject(L, frame_index);
  return dynamic_cast<openwow::ui::widgets::CSimpleFrame *>(script_object);
}

float NormalizeBackdropLuaColorComponent(lua_Number value) {
  if (std::isnan(value)) {
    value = 1.0;
  } else if (value < 0.0) {
    value = 0.0;
  } else if (value >= 1.0) {
    value = 1.0;
  }

  const int quantized =
      std::clamp(static_cast<int>(value * 255.0 + 0.5), 0, 255);
  return static_cast<float>(quantized) / 255.0f;
}

void StoreLuaBackdropColorShadow(lua_State *L, int table_index,
                                        const char *red_field,
                                        const char *green_field,
                                        const char *blue_field,
                                        const char *alpha_field,
                                        const float red,
                                        const float green,
                                        const float blue,
                                        const float alpha) {
  openwow::ui::WriteLuaNumberField(L, table_index, red_field, red);
  openwow::ui::WriteLuaNumberField(L, table_index, green_field, green);
  openwow::ui::WriteLuaNumberField(L, table_index, blue_field, blue);
  openwow::ui::WriteLuaNumberField(L, table_index, alpha_field, alpha);
}

void ClearLuaBackdropShadow(lua_State *L, int table_index) {
  table_index = lua_absindex(L, table_index);
  lua_pushnil(L);
  lua_setfield(L, table_index, kLuaBackdropField);
  lua_pushnil(L);
  lua_setfield(L, table_index, kLuaBackdropColorRField);
  lua_pushnil(L);
  lua_setfield(L, table_index, kLuaBackdropColorGField);
  lua_pushnil(L);
  lua_setfield(L, table_index, kLuaBackdropColorBField);
  lua_pushnil(L);
  lua_setfield(L, table_index, kLuaBackdropColorAField);
  lua_pushnil(L);
  lua_setfield(L, table_index, kLuaBackdropBorderColorRField);
  lua_pushnil(L);
  lua_setfield(L, table_index, kLuaBackdropBorderColorGField);
  lua_pushnil(L);
  lua_setfield(L, table_index, kLuaBackdropBorderColorBField);
  lua_pushnil(L);
  lua_setfield(L, table_index, kLuaBackdropBorderColorAField);
}

bool HasLuaBackdropShadow(lua_State *L, int table_index) {
  table_index = lua_absindex(L, table_index);
  runtime::GetInternedLuaField(L, table_index, kLuaBackdropField);
  const bool has_backdrop = lua_istable(L, -1) != 0;
  lua_pop(L, 1);
  return has_backdrop;
}

static float ReadLuaBackdropNumberFieldOrDefault(lua_State *L, int table_index,
                                                 const char *field_name,
                                                 const float default_value) {
  table_index = lua_absindex(L, table_index);
  runtime::GetInternedLuaField(L, table_index, field_name);
  const float value = lua_isnumber(L, -1) != 0
                          ? static_cast<float>(lua_tonumber(L, -1))
                          : default_value;
  lua_pop(L, 1);
  return value;
}

std::optional<FrameBackdropColors> ReadFrameBackdropColors(
    lua_State *L, int frame_index) {
  if (L == nullptr || lua_istable(L, frame_index) == 0 ||
      !HasLuaBackdropShadow(L, frame_index)) {
    return std::nullopt;
  }

  frame_index = lua_absindex(L, frame_index);
  FrameBackdropColors colors;
  colors.background = {
      ReadLuaBackdropNumberFieldOrDefault(
          L, frame_index, kLuaBackdropColorRField, 1.0F),
      ReadLuaBackdropNumberFieldOrDefault(
          L, frame_index, kLuaBackdropColorGField, 1.0F),
      ReadLuaBackdropNumberFieldOrDefault(
          L, frame_index, kLuaBackdropColorBField, 1.0F),
      ReadLuaBackdropNumberFieldOrDefault(
          L, frame_index, kLuaBackdropColorAField, 1.0F),
  };
  colors.border = {
      ReadLuaBackdropNumberFieldOrDefault(
          L, frame_index, kLuaBackdropBorderColorRField, 1.0F),
      ReadLuaBackdropNumberFieldOrDefault(
          L, frame_index, kLuaBackdropBorderColorGField, 1.0F),
      ReadLuaBackdropNumberFieldOrDefault(
          L, frame_index, kLuaBackdropBorderColorBField, 1.0F),
      ReadLuaBackdropNumberFieldOrDefault(
          L, frame_index, kLuaBackdropBorderColorAField, 1.0F),
  };
  return colors;
}

static std::string ReadLuaBackdropStringFieldOrDefault(lua_State *L, int table_index,
                                                       const char *field_name) {
  table_index = lua_absindex(L, table_index);
  lua_getfield(L, table_index, field_name);
  const char *value = lua_tostring(L, -1);
  std::string result;
  if (value != nullptr) {
    result = value;
  }
  lua_pop(L, 1);
  return result;
}

openwow::ui::widgets::BackdropInfo ReadLuaBackdropInfo(lua_State *L,
                                                              int table_index) {
  table_index = lua_absindex(L, table_index);

  openwow::ui::widgets::BackdropInfo backdrop;
  backdrop.edgeSize =
      openwow::ui::framexml::detail::BackdropCtorDefaultEdgeSizePixels();
  backdrop.bgFile = ReadLuaBackdropStringFieldOrDefault(L, table_index, "bgFile");
  backdrop.edgeFile = ReadLuaBackdropStringFieldOrDefault(L, table_index, "edgeFile");

  lua_getfield(L, table_index, "tile");
  backdrop.tile = ScriptReadBoolArgOrDefault(L, -1, false);
  lua_pop(L, 1);

  backdrop.tileSize =
      ReadLuaBackdropNumberFieldOrDefault(L, table_index, "tileSize", 0.0f);
  backdrop.edgeSize = ReadLuaBackdropNumberFieldOrDefault(
      L, table_index, "edgeSize", backdrop.edgeSize);

  lua_getfield(L, table_index, "insets");
  if (lua_istable(L, -1) != 0) {
    const int insets_index = lua_absindex(L, -1);
    backdrop.insetLeft =
        ReadLuaBackdropNumberFieldOrDefault(L, insets_index, "left", 0.0f);
    backdrop.insetRight =
        ReadLuaBackdropNumberFieldOrDefault(L, insets_index, "right", 0.0f);
    backdrop.insetTop =
        ReadLuaBackdropNumberFieldOrDefault(L, insets_index, "top", 0.0f);
    backdrop.insetBottom =
        ReadLuaBackdropNumberFieldOrDefault(L, insets_index, "bottom", 0.0f);
  }
  lua_pop(L, 1);

  return backdrop;
}

static void PushLuaBackdropTable(lua_State *L,
                                 const openwow::ui::widgets::BackdropInfo &backdrop) {
  lua_createtable(L, 0, 6);
  const int backdrop_index = lua_absindex(L, -1);

  lua_pushstring(L, backdrop.bgFile.c_str());
  lua_setfield(L, backdrop_index, "bgFile");

  lua_pushstring(L, backdrop.edgeFile.c_str());
  lua_setfield(L, backdrop_index, "edgeFile");

  if (backdrop.tile) {
    lua_pushnumber(L, 1.0);
    lua_setfield(L, backdrop_index, "tile");
  }

  lua_pushnumber(L, backdrop.tileSize);
  lua_setfield(L, backdrop_index, "tileSize");

  lua_pushnumber(L, backdrop.edgeSize);
  lua_setfield(L, backdrop_index, "edgeSize");

  lua_createtable(L, 0, 4);
  const int insets_index = lua_absindex(L, -1);
  lua_pushnumber(L, backdrop.insetLeft);
  lua_setfield(L, insets_index, "left");
  lua_pushnumber(L, backdrop.insetRight);
  lua_setfield(L, insets_index, "right");
  lua_pushnumber(L, backdrop.insetTop);
  lua_setfield(L, insets_index, "top");
  lua_pushnumber(L, backdrop.insetBottom);
  lua_setfield(L, insets_index, "bottom");
  lua_setfield(L, backdrop_index, "insets");
}

void StoreLuaBackdropShadow(lua_State *L, int table_index,
                                   const openwow::ui::widgets::BackdropInfo &backdrop) {
  table_index = lua_absindex(L, table_index);
  PushLuaBackdropTable(L, backdrop);
  lua_setfield(L, table_index, kLuaBackdropField);
}

namespace {

constexpr std::string_view kBackdropPieceNameSuffixes[] = {
    ".__BackdropBackground",       ".__BackdropBorderTopLeft",
    ".__BackdropBorderTopRight",   ".__BackdropBorderBottomLeft",
    ".__BackdropBorderBottomRight", ".__BackdropBorderTop",
    ".__BackdropBorderBottom",     ".__BackdropBorderLeft",
    ".__BackdropBorderRight",
};

constexpr int kBackdropBackgroundPieceIndex = 1;

}

void ClearRuntimeBackdropPieces(lua_State *L, int frame_index) {
  auto *context = runtime::WorldUiRuntimeContext::FromLua(L);
  if (context == nullptr || lua_istable(L, frame_index) == 0) {
    return;
  }
  frame_index = lua_absindex(L, frame_index);

  lua_getfield(L, frame_index, kLuaBackdropPiecesField);
  if (lua_istable(L, -1) != 0) {
    const int pieces_index = lua_absindex(L, -1);
    lua_pushnil(L);
    while (lua_next(L, pieces_index) != 0) {
      if (lua_istable(L, -1) != 0) {
        if (const char *piece_key = GetFrameRuntimeKeyOrName(L, -1);
            piece_key != nullptr && piece_key[0] != '\0') {
          context->DestroyNamedFrame(piece_key);
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  lua_pushnil(L);
  lua_setfield(L, frame_index, kLuaBackdropPiecesField);

  if (const char *owner_key = GetFrameRuntimeKeyOrName(L, frame_index);
      owner_key != nullptr && owner_key[0] != '\0') {
    const std::string owner(owner_key);
    for (const auto suffix : kBackdropPieceNameSuffixes) {
      context->DestroyNamedFrame(owner + std::string(suffix));
    }
  }
}

void RebuildRuntimeBackdropPieces(lua_State *L, int frame_index) {
  auto *context = runtime::WorldUiRuntimeContext::FromLua(L);
  if (context == nullptr || lua_istable(L, frame_index) == 0) {
    return;
  }
  frame_index = lua_absindex(L, frame_index);

  ClearRuntimeBackdropPieces(L, frame_index);

  openwow::ui::widgets::BackdropInfo backdrop;
  if (!TryReadLuaBackdropShadow(L, frame_index, &backdrop)) {
    return;
  }

  const char *owner_key = GetFrameRuntimeKeyOrName(L, frame_index);
  if (owner_key == nullptr || owner_key[0] == '\0') {
    return;
  }

  openwow::ui::framexml::BackdropSpec spec;
  spec.bg_file = backdrop.bgFile;
  spec.edge_file = backdrop.edgeFile;
  spec.tile = backdrop.tile;
  spec.tile_size = backdrop.tileSize;
  spec.edge_size = backdrop.edgeSize;
  spec.inset_left = backdrop.insetLeft;
  spec.inset_right = backdrop.insetRight;
  spec.inset_top = backdrop.insetTop;
  spec.inset_bottom = backdrop.insetBottom;
  spec.alpha_mode = backdrop.alphaMode;
  if (const auto colors = ReadFrameBackdropColors(L, frame_index);
      colors.has_value()) {
    spec.bg_color_r = colors->background[0];
    spec.bg_color_g = colors->background[1];
    spec.bg_color_b = colors->background[2];
    spec.bg_color_a = colors->background[3];
    spec.has_bg_color = true;
    spec.border_color_r = colors->border[0];
    spec.border_color_g = colors->border[1];
    spec.border_color_b = colors->border[2];
    spec.border_color_a = colors->border[3];
    spec.has_border_color = true;
  }

  openwow::ui::framexml::UiFrame owner;
  owner.name = owner_key;
  owner.visible = true;
  if (const auto *owner_row = context->frame_store().FindFrame(owner_key);
      owner_row != nullptr) {
    owner.frame_strata = owner_row->frame_strata;
    owner.frame_level = owner_row->frame_level;
    owner.visible = owner_row->visible;
  }

  std::unordered_map<std::string, std::size_t> index_by_name;
  std::vector<openwow::ui::framexml::UiFrame> pieces;
  openwow::ui::framexml::detail::InjectBackdropPieces(spec, owner,
                                                      &index_by_name, &pieces);
  if (pieces.empty()) {
    return;
  }

  lua_createtable(L, static_cast<int>(pieces.size()), 0);
  const int pieces_index = lua_absindex(L, -1);
  int stored = 0;
  for (const auto &piece : pieces) {
    CreateTextureTable(L, frame_index);
    const int piece_index = lua_absindex(L, -1);

    RemoveExactValueFromArrayField(L, frame_index, "__ow_regions", piece_index);
    (void)context->frame_materializer().AdoptRuntimeFrame(piece);
    lua_pushvalue(L, piece_index);
    lua_rawseti(L, pieces_index, ++stored);
    lua_pop(L, 1);
  }
  lua_setfield(L, frame_index, kLuaBackdropPiecesField);
}

void ApplyRuntimeBackdropPieceColors(lua_State *L, int frame_index,
                                     const bool border, const float red,
                                     const float green, const float blue,
                                     const float alpha) {
  if (lua_istable(L, frame_index) == 0) {
    return;
  }
  frame_index = lua_absindex(L, frame_index);

  lua_getfield(L, frame_index, kLuaBackdropPiecesField);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }
  const int pieces_index = lua_absindex(L, -1);
  const lua_Integer count = luaL_len(L, pieces_index);
  for (lua_Integer index = 1; index <= count; ++index) {
    const bool is_background = index == kBackdropBackgroundPieceIndex;
    if (is_background == border) {
      continue;
    }
    lua_rawgeti(L, pieces_index, index);
    if (lua_istable(L, -1) != 0) {
      const int piece_index = lua_absindex(L, -1);
      using runtime::TextureRenderStateField;
      runtime::SetTextureRenderStateNumber(
          L, piece_index, TextureRenderStateField::kVertexColorR, red);
      runtime::SetTextureRenderStateNumber(
          L, piece_index, TextureRenderStateField::kVertexColorG, green);
      runtime::SetTextureRenderStateNumber(
          L, piece_index, TextureRenderStateField::kVertexColorB, blue);
      runtime::SetTextureRenderStateNumber(
          L, piece_index, TextureRenderStateField::kVertexColorA, alpha);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

void ApplyFrameXmlBackdropDefinition(
    lua_State *L, int frame_index,
    const openwow::ui::framexml::UiFrame &frame) {
  if (L == nullptr || !frame.backdrop.has_value()) {
    return;
  }

  frame_index = lua_absindex(L, frame_index);
  const auto &spec = *frame.backdrop;
  openwow::ui::widgets::BackdropInfo backdrop;
  backdrop.bgFile = spec.bg_file;
  backdrop.edgeFile = spec.edge_file;
  backdrop.tile = spec.tile;
  backdrop.tileSize = spec.tile_size;
  backdrop.edgeSize = spec.edge_size;
  backdrop.insetLeft = spec.inset_left;
  backdrop.insetRight = spec.inset_right;
  backdrop.insetTop = spec.inset_top;
  backdrop.insetBottom = spec.inset_bottom;

  StoreLuaBackdropShadow(L, frame_index, backdrop);

  const auto quantize = [](const float value) {
    return NormalizeBackdropLuaColorComponent(value);
  };
  const float background_r = quantize(spec.bg_color_r);
  const float background_g = quantize(spec.bg_color_g);
  const float background_b = quantize(spec.bg_color_b);
  const float background_a = quantize(spec.bg_color_a);
  const float border_r = quantize(spec.border_color_r);
  const float border_g = quantize(spec.border_color_g);
  const float border_b = quantize(spec.border_color_b);
  const float border_a = quantize(spec.border_color_a);
  StoreLuaBackdropColorShadow(
      L, frame_index, kLuaBackdropColorRField, kLuaBackdropColorGField,
      kLuaBackdropColorBField, kLuaBackdropColorAField, background_r,
      background_g, background_b, background_a);
  StoreLuaBackdropColorShadow(
      L, frame_index, kLuaBackdropBorderColorRField,
      kLuaBackdropBorderColorGField, kLuaBackdropBorderColorBField,
      kLuaBackdropBorderColorAField, border_r, border_g, border_b, border_a);

  if (auto *frame_object = GetLuaBackdropFrame(L, frame_index);
      frame_object != nullptr) {
    frame_object->SetBackdrop(backdrop);
    frame_object->SetBackdropColor(background_r, background_g, background_b,
                                   background_a);
    frame_object->SetBackdropBorderColor(border_r, border_g, border_b,
                                         border_a);
  }
}

}
