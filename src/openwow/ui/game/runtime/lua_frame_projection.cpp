#include "openwow/ui/game/runtime/lua_frame_projection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <lua.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/game/localization.h"
#include "openwow/game/world_session.h"
#include "openwow/input/input_manager.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/models/characters/portrait_renderer.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/framexml/backdrop_render_utils.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/framescript/core/frame_backdrop_runtime.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/widgets/quest_poi_frame_methods.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/runtime/frame_script_handler_fields.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/ui_region_render_policy.h"
#include "openwow/ui/game/world_map_system.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/ui_paint_order.h"

namespace openwow::ui::game {
namespace lua_projection {

using UiFrame = openwow::ui::framexml::UiFrame;

using openwow::ui::game::runtime::FrameScriptHandlerFields;
using openwow::ui::game::runtime::GetInternedLuaField;
using openwow::ui::game::runtime::RawGetInternedLuaField;

namespace metadata_field {

constexpr const char* const kNames[] = {
    "__ow_parent_name",
    "__ow_parent",
    openwow::ui::game::frame_api::kLuaFrameLevelField,
    openwow::ui::game::frame_api::kLuaFrameStrataField,
    "__ow_depth",
    "__ow_width",
    "__ow_height",
    "__ow_scale",
    "__ow_mouse_enabled",
    "__ow_enableMouse",
    "__ow_type",
    "__ow_visible",
    "__ow_alpha",
    "__ow_draw_layer_enabled",
    "__ow_mousewheel_enabled",
    "__ow_keyboard_enabled",
    "__ow_enableKeyboard",
    runtime::kMouseCategoryHandlerFields[0].stored,
    runtime::kMouseCategoryHandlerFields[0].direct,
    runtime::kMouseCategoryHandlerFields[1].stored,
    runtime::kMouseCategoryHandlerFields[1].direct,
    runtime::kMouseCategoryHandlerFields[2].stored,
    runtime::kMouseCategoryHandlerFields[2].direct,
    runtime::kMouseCategoryHandlerFields[3].stored,
    runtime::kMouseCategoryHandlerFields[3].direct,
    runtime::kMouseCategoryHandlerFields[4].stored,
    runtime::kMouseCategoryHandlerFields[4].direct,
    runtime::kMouseCategoryHandlerFields[5].stored,
    runtime::kMouseCategoryHandlerFields[5].direct,
    runtime::kMouseCategoryHandlerFields[6].stored,
    runtime::kMouseCategoryHandlerFields[6].direct,
    runtime::kMouseCategoryHandlerFields[7].stored,
    runtime::kMouseCategoryHandlerFields[7].direct,
    runtime::kMouseCategoryHandlerFields[8].stored,
    runtime::kMouseCategoryHandlerFields[8].direct,
    runtime::kMouseWheelHandlerFields.stored,
    runtime::kMouseWheelHandlerFields.direct,
};
static_assert(runtime::kMouseCategoryHandlerFields.size() == 9,
              "metadata_field::kNames spells nine mouse-category handler "
              "pairs; extend both when the canonical list grows");

using runtime::InternedLuaFieldKeyOrdinal;
inline constexpr int kParentName =
    InternedLuaFieldKeyOrdinal(kNames, "__ow_parent_name");
inline constexpr int kParent = InternedLuaFieldKeyOrdinal(kNames, "__ow_parent");
inline constexpr int kFrameLevel = InternedLuaFieldKeyOrdinal(
    kNames, openwow::ui::game::frame_api::kLuaFrameLevelField);
inline constexpr int kFrameStrata = InternedLuaFieldKeyOrdinal(
    kNames, openwow::ui::game::frame_api::kLuaFrameStrataField);
inline constexpr int kDepth = InternedLuaFieldKeyOrdinal(kNames, "__ow_depth");
inline constexpr int kWidth = InternedLuaFieldKeyOrdinal(kNames, "__ow_width");
inline constexpr int kHeight = InternedLuaFieldKeyOrdinal(kNames, "__ow_height");
inline constexpr int kScale = InternedLuaFieldKeyOrdinal(kNames, "__ow_scale");
inline constexpr int kMouseEnabled =
    InternedLuaFieldKeyOrdinal(kNames, "__ow_mouse_enabled");
inline constexpr int kEnableMouse =
    InternedLuaFieldKeyOrdinal(kNames, "__ow_enableMouse");
inline constexpr int kType = InternedLuaFieldKeyOrdinal(kNames, "__ow_type");
inline constexpr int kVisible = InternedLuaFieldKeyOrdinal(kNames, "__ow_visible");
inline constexpr int kAlpha = InternedLuaFieldKeyOrdinal(kNames, "__ow_alpha");
inline constexpr int kDrawLayerEnabled =
    InternedLuaFieldKeyOrdinal(kNames, "__ow_draw_layer_enabled");
inline constexpr int kMouseWheelEnabled =
    InternedLuaFieldKeyOrdinal(kNames, "__ow_mousewheel_enabled");
inline constexpr int kKeyboardEnabled =
    InternedLuaFieldKeyOrdinal(kNames, "__ow_keyboard_enabled");
inline constexpr int kEnableKeyboard =
    InternedLuaFieldKeyOrdinal(kNames, "__ow_enableKeyboard");

struct HandlerOrdinals {
  int stored;
  int direct;
};

inline constexpr HandlerOrdinals kMouseWheelHandler{
    .stored = InternedLuaFieldKeyOrdinal(kNames,
                                         runtime::kMouseWheelHandlerFields.stored),
    .direct = InternedLuaFieldKeyOrdinal(kNames,
                                         runtime::kMouseWheelHandlerFields.direct),
};

struct Source {
  int table;
  int keys;
};

inline void Get(lua_State* L, const Source& source, const int key) {
  runtime::GetInternedLuaBlockField(L, source.table, source.keys, key);
}
inline void RawGet(lua_State* L, const Source& source, const int key) {
  runtime::RawGetInternedLuaBlockField(L, source.table, source.keys, key);
}

}

class ScopedUiScissor {
 public:
  ScopedUiScissor(openwow::render::ui::UiRenderer* renderer,
                  const std::optional<openwow::ui::UiPaintRect>& clip)
      : renderer_(clip.has_value() ? renderer : nullptr) {
    if (renderer_ != nullptr) {
      renderer_->SetScissor(clip->x, clip->y, clip->width, clip->height);
    }
  }
  ~ScopedUiScissor() {
    if (renderer_ != nullptr) renderer_->ClearScissor();
  }
  ScopedUiScissor(const ScopedUiScissor&) = delete;
  ScopedUiScissor& operator=(const ScopedUiScissor&) = delete;

 private:
  openwow::render::ui::UiRenderer* renderer_;
};

[[nodiscard]] UiFrame::RuntimeKind ClassifyFrameRuntimeKind(
    const std::string_view kind) noexcept {
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "texture")) {
    return UiFrame::RuntimeKind::Texture;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "fontstring")) {
    return UiFrame::RuntimeKind::FontString;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "editbox")) {
    return UiFrame::RuntimeKind::EditBox;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "questpoiframe")) {
    return UiFrame::RuntimeKind::QuestPoiFrame;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "statusbar")) {
    return UiFrame::RuntimeKind::StatusBar;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "slider")) {
    return UiFrame::RuntimeKind::Slider;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "scrollingmessageframe")) {
    return UiFrame::RuntimeKind::ScrollingMessageFrame;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "cooldown")) {
    return UiFrame::RuntimeKind::Cooldown;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "minimap")) {
    return UiFrame::RuntimeKind::Minimap;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "model")) {
    return UiFrame::RuntimeKind::Model;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "playermodel")) {
    return UiFrame::RuntimeKind::PlayerModel;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "dressupmodel")) {
    return UiFrame::RuntimeKind::DressUpModel;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "tabardmodel")) {
    return UiFrame::RuntimeKind::TabardModel;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "messageframe")) {
    return UiFrame::RuntimeKind::MessageFrame;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "simplehtml")) {
    return UiFrame::RuntimeKind::SimpleHtml;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "colorselect")) {
    return UiFrame::RuntimeKind::ColorSelect;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "movieframe")) {
    return UiFrame::RuntimeKind::MovieFrame;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(kind, "line")) {
    return UiFrame::RuntimeKind::Line;
  }
  return UiFrame::RuntimeKind::Other;
}

static bool LuaFrameUsesMouseCategory(lua_State* L,
                                      const metadata_field::Source& source,
                                      std::string_view kind);
static bool LuaFrameUsesMouseWheelCategory(
    lua_State* L, const metadata_field::Source& source);
static bool LuaFrameUsesKeyboardCategory(
    lua_State* L, const metadata_field::Source& source);
static bool SyncFrameRuntimeMetadataFromLua(
    lua_State* L, const metadata_field::Source& source,
    openwow::ui::framexml::UiFrame* frame);

std::uint8_t GetLuaFrameAlphaByteOrDefault(lua_State* L, int index,
                                           std::uint8_t fallback) {
  index = lua_absindex(L, index);
  GetInternedLuaField(L, index, "__ow_alpha");
  double alpha = openwow::ui::game::NormalizeFrameAlphaByte(fallback);
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
}

std::uint8_t GetLuaFontStringAlphaByteOrDefault(lua_State* L, int index,
                                                std::uint8_t fallback) {
  index = lua_absindex(L, index);
  double alpha = openwow::ui::game::NormalizeFrameAlphaByte(fallback);

  GetInternedLuaField(L, index, "__ow_text_a");
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
  }
  lua_pop(L, 1);

  GetInternedLuaField(L, index, "__ow_alpha");
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
}

bool SyncFrameRuntimeMetadataFromLua(
    lua_State* L, const int frame_index,
    openwow::ui::framexml::UiFrame* const frame) {
  if (L == nullptr || frame == nullptr || lua_istable(L, frame_index) == 0) {
    return false;
  }

  const metadata_field::Source source{
      .table = lua_absindex(L, frame_index),
      .keys = runtime::PushInternedLuaFieldKeyBlock(
          L, metadata_field::kNames,
          static_cast<int>(std::size(metadata_field::kNames))),
  };
  const bool kind_changed = SyncFrameRuntimeMetadataFromLua(L, source, frame);
  lua_settop(L, source.keys - 1);
  return kind_changed;
}

static bool ReadLuaWidgetShownState(lua_State* L,
                                    const metadata_field::Source& source) {
  metadata_field::Get(L, source, metadata_field::kVisible);
  const bool shown = lua_isboolean(L, -1) == 0 || lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return shown;
}

static std::uint8_t ReadLuaFrameAlphaByteOrDefault(
    lua_State* L, const metadata_field::Source& source,
    const std::uint8_t fallback) {
  metadata_field::Get(L, source, metadata_field::kAlpha);
  double alpha = openwow::ui::game::NormalizeFrameAlphaByte(fallback);
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
}

static bool SyncFrameRuntimeMetadataFromLua(
    lua_State* L, const metadata_field::Source& source,
    openwow::ui::framexml::UiFrame* const frame) {
  bool kind_changed = false;
  metadata_field::Get(L, source, metadata_field::kType);
  if (const char* type_name = lua_tostring(L, -1);
      type_name != nullptr && type_name[0] != '\0' &&
      frame->kind != type_name) {

    frame->kind = type_name;
    kind_changed = true;
  }
  lua_pop(L, 1);
  frame->runtime_kind = ClassifyFrameRuntimeKind(frame->kind);
  frame->visible = ReadLuaWidgetShownState(L, source);
  frame->color_a = static_cast<float>(
      openwow::ui::game::NormalizeFrameAlphaByte(ReadLuaFrameAlphaByteOrDefault(
          L, source,
          openwow::ui::game::QuantizeFrameAlphaByteTruncated(frame->color_a))));

  metadata_field::Get(L, source, metadata_field::kDrawLayerEnabled);
  frame->runtime_draw_layer_enabled =
      lua_isboolean(L, -1) == 0 || lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);

  frame->runtime_uses_mouse =
      LuaFrameUsesMouseCategory(L, source, frame->kind);
  frame->runtime_uses_mouse_wheel = LuaFrameUsesMouseWheelCategory(L, source);
  frame->runtime_uses_keyboard = LuaFrameUsesKeyboardCategory(L, source);
  frame->runtime_state_initialized = true;
  return kind_changed;
}

void SyncRetainedBackdropColors(lua_State* L, const int owner_index,
                                const std::string& owner_key,
                                runtime::FrameStore* const frames) {
  if (L == nullptr || frames == nullptr || owner_key.empty()) {
    return;
  }

  const auto colors =
      openwow::ui::game::frame_api::ReadFrameBackdropColors(L, owner_index);
  if (!colors.has_value()) {
    return;
  }

  struct Color {
    float r{1.0F};
    float g{1.0F};
    float b{1.0F};
    float a{1.0F};
  } background{colors->background[0], colors->background[1],
               colors->background[2], colors->background[3]},
      border{colors->border[0], colors->border[1], colors->border[2],
             colors->border[3]};

  std::string piece_name;
  const auto apply_color = [&](const Color& color) {
    const auto handle = frames->HandleOf(piece_name);
    auto* const frame = frames->FindFrame(handle);
    if (frame == nullptr) {
      return;
    }
    frame->color_r = color.r;
    frame->color_g = color.g;
    frame->color_b = color.b;
    frame->color_a = color.a;
    frame->has_vertex_color = true;

    const auto ref = frames->FindLuaRef(handle);
    if (!ref.has_value()) {
      return;
    }
    const int stack_top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, *ref);
    if (lua_istable(L, -1) != 0) {
      using runtime::TextureRenderStateField;
      runtime::SetTextureRenderStateNumber(
          L, -1, TextureRenderStateField::kVertexColorR, color.r);
      runtime::SetTextureRenderStateNumber(
          L, -1, TextureRenderStateField::kVertexColorG, color.g);
      runtime::SetTextureRenderStateNumber(
          L, -1, TextureRenderStateField::kVertexColorB, color.b);
      runtime::SetTextureRenderStateNumber(
          L, -1, TextureRenderStateField::kVertexColorA, color.a);
    }
    lua_settop(L, stack_top);
  };

  piece_name = owner_key;
  piece_name += ".__BackdropBackground";
  apply_color(background);
  static constexpr std::array<std::string_view, 8> kBorderSuffixes{{
      "TopLeft",
      "TopRight",
      "BottomLeft",
      "BottomRight",
      "Top",
      "Bottom",
      "Left",
      "Right",
  }};
  for (const std::string_view suffix : kBorderSuffixes) {
    piece_name = owner_key;
    piece_name += ".__BackdropBorder";
    piece_name += suffix;
    apply_color(border);
  }
}

static std::uint8_t QuantizeNormalizedColorByte(float value) {
  const float clamped = std::clamp(value, 0.0f, 1.0f);
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(clamped * 255.0f), 0l, 255l));
}

bool GetLuaBooleanField(lua_State* L, int index, const char* field_name) {
  index = lua_absindex(L, index);
  RawGetInternedLuaField(L, index, field_name);
  const bool value = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return value;
}

std::optional<std::string> GetLuaStringField(lua_State* L, int index,
                                             const char* field_name) {
  index = lua_absindex(L, index);
  RawGetInternedLuaField(L, index, field_name);
  std::optional<std::string> value;
  if (const char* text = lua_tostring(L, -1); text != nullptr) {
    value = std::string(text);
  }
  lua_pop(L, 1);
  return value;
}

std::uint32_t PackTextAbgrStraight(const float red, const float green,
                                   const float blue, const float alpha) {
  return (static_cast<std::uint32_t>(QuantizeNormalizedColorByte(alpha))
          << 24) |
         (static_cast<std::uint32_t>(QuantizeNormalizedColorByte(blue)) << 16) |
         (static_cast<std::uint32_t>(QuantizeNormalizedColorByte(green)) << 8) |
         static_cast<std::uint32_t>(QuantizeNormalizedColorByte(red));
}

[[nodiscard]] std::string ResolveFrameXmlText(
    lua_State* state, const std::string_view authored_text) {
  const std::string localized =
      openwow::game::ResolveLocalizedGlobalString(state, authored_text);
  return localized.empty() ? std::string(authored_text) : localized;
}

std::uint32_t PackStraightTextureAbgr(const float red, const float green,
                                      const float blue, const float color_alpha,
                                      const float inherited_alpha) {
  const float effective_alpha =
      std::clamp(color_alpha * inherited_alpha, 0.0f, 1.0f);
  const auto r = QuantizeNormalizedColorByte(red);
  const auto g = QuantizeNormalizedColorByte(green);
  const auto b = QuantizeNormalizedColorByte(blue);
  const auto a = static_cast<std::uint8_t>(effective_alpha * 255.0f);
  return (static_cast<std::uint32_t>(a) << 24) |
         (static_cast<std::uint32_t>(b) << 16) |
         (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r);
}

namespace texture_field {

constexpr const char* const kTextureFieldNames[] = {
    "__ow_texture",
    "__ow_texture_cleared",
    "__ow_portrait_unit",
    "__ow_portrait_guid",
    "__ow_tc_ul_x",
    "__ow_tc_ul_y",
    "__ow_tc_ll_x",
    "__ow_tc_ll_y",
    "__ow_tc_ur_x",
    "__ow_tc_ur_y",
    "__ow_tc_lr_x",
    "__ow_tc_lr_y",
    "__ow_horiz_tile",
    "__ow_vert_tile",
    "__ow_vc_r",
    "__ow_vc_g",
    "__ow_vc_b",
    "__ow_vc_a",
    "__ow_tex_r",
    "__ow_tex_g",
    "__ow_tex_b",
    "__ow_tex_a",
    "__ow_gradient_orient",
    "__ow_grad_min_r",
    "__ow_grad_min_g",
    "__ow_grad_min_b",
    "__ow_grad_min_a",
    "__ow_grad_max_r",
    "__ow_grad_max_g",
    "__ow_grad_max_b",
    "__ow_grad_max_a",
    "__ow_blend",
    "__ow_desat",
    "__ow_tabard_rt_source_texture",
    "__ow_tabard_rt_name",
    "__ow_tabard_rt_width",
    "__ow_tabard_rt_height",
    "__ow_tabard_rt_pitch",
    "__ow_tabard_rt_force_white_rgb",
    "__ow_tabard_rt_copy_source_alpha",
};

inline constexpr int kTexture =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_texture");
inline constexpr int kTextureCleared =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_texture_cleared");
inline constexpr int kPortraitUnit =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_portrait_unit");
inline constexpr int kPortraitGuid =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_portrait_guid");
inline constexpr int kTexCoordUpperLeftX =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tc_ul_x");
inline constexpr int kTexCoordUpperLeftY =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tc_ul_y");
inline constexpr int kTexCoordLowerLeftX =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tc_ll_x");
inline constexpr int kTexCoordLowerLeftY =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tc_ll_y");
inline constexpr int kTexCoordUpperRightX =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tc_ur_x");
inline constexpr int kTexCoordUpperRightY =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tc_ur_y");
inline constexpr int kTexCoordLowerRightX =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tc_lr_x");
inline constexpr int kTexCoordLowerRightY =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tc_lr_y");
inline constexpr int kHorizontalTile =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_horiz_tile");
inline constexpr int kVerticalTile =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_vert_tile");
inline constexpr int kVertexColorR =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_vc_r");
inline constexpr int kVertexColorG =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_vc_g");
inline constexpr int kVertexColorB =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_vc_b");
inline constexpr int kVertexColorA =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_vc_a");
inline constexpr int kSolidColorR =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tex_r");
inline constexpr int kSolidColorG =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tex_g");
inline constexpr int kSolidColorB =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tex_b");
inline constexpr int kSolidColorA =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tex_a");
inline constexpr int kGradientOrientation =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_gradient_orient");
inline constexpr int kGradientMinR =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_grad_min_r");
inline constexpr int kGradientMinG =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_grad_min_g");
inline constexpr int kGradientMinB =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_grad_min_b");
inline constexpr int kGradientMinA =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_grad_min_a");
inline constexpr int kGradientMaxR =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_grad_max_r");
inline constexpr int kGradientMaxG =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_grad_max_g");
inline constexpr int kGradientMaxB =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_grad_max_b");
inline constexpr int kGradientMaxA =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_grad_max_a");
inline constexpr int kBlendMode =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_blend");
inline constexpr int kDesaturated =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_desat");
inline constexpr int kTabardSourceTexture =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tabard_rt_source_texture");
inline constexpr int kTabardRenderTargetName =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tabard_rt_name");
inline constexpr int kTabardWidth =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tabard_rt_width");
inline constexpr int kTabardHeight =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tabard_rt_height");
inline constexpr int kTabardPitch =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tabard_rt_pitch");
inline constexpr int kTabardForceWhiteRgb =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tabard_rt_force_white_rgb");
inline constexpr int kTabardCopySourceAlpha =
    runtime::InternedLuaFieldKeyOrdinal(kTextureFieldNames, "__ow_tabard_rt_copy_source_alpha");

struct Source {
  int table;
  int keys;
};

void Push(lua_State* L, const Source& source, const int key) {
  runtime::RawGetInternedLuaBlockField(L, source.table, source.keys,
                                       static_cast<int>(key));
}

[[nodiscard]] bool ReadNumber(lua_State* L, const Source& source, const int key,
                              double* const out_value) {
  Push(L, source, key);
  const bool has_value = lua_isnumber(L, -1) != 0;
  if (has_value) {
    *out_value = lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return has_value;
}

[[nodiscard]] bool ReadBoolean(lua_State* L, const Source& source,
                               const int key) {
  Push(L, source, key);
  const bool value = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return value;
}

[[nodiscard]] std::optional<bool> ReadBooleanIfPresent(lua_State* L,
                                                       const Source& source,
                                                       const int key) {
  Push(L, source, key);
  std::optional<bool> value;
  if (lua_isboolean(L, -1) != 0) {
    value = lua_toboolean(L, -1) != 0;
  }
  lua_pop(L, 1);
  return value;
}

[[nodiscard]] bool ReadInteger(lua_State* L, const Source& source,
                               const int key, int* const out_value) {
  Push(L, source, key);
  const bool has_value = lua_isinteger(L, -1) != 0;
  if (has_value) {
    *out_value = static_cast<int>(lua_tointeger(L, -1));
  }
  lua_pop(L, 1);
  return has_value;
}

[[nodiscard]] bool ReadStringInto(lua_State* L, const Source& source,
                                  const int key, std::string* const out_value) {
  Push(L, source, key);
  const char* const text = lua_tostring(L, -1);
  if (text != nullptr) {
    out_value->assign(text);
  }
  lua_pop(L, 1);
  return text != nullptr;
}

[[nodiscard]] std::optional<std::string> ReadString(lua_State* L,
                                                    const Source& source,
                                                    const int key) {
  Push(L, source, key);
  std::optional<std::string> value;
  if (const char* const text = lua_tostring(L, -1); text != nullptr) {
    value = std::string(text);
  }
  lua_pop(L, 1);
  return value;
}

[[nodiscard]] bool HasString(lua_State* L, const Source& source,
                             const int key) {
  Push(L, source, key);
  const bool present = lua_tostring(L, -1) != nullptr;
  lua_pop(L, 1);
  return present;
}

}

static bool TryGetLuaTextureUvQuad(lua_State* L,
                                   const texture_field::Source& source,
                                   TextureQuadUv* out_quad) {
  if (out_quad == nullptr) {
    return false;
  }

  double upper_left_u = 0.0;
  double upper_left_v = 0.0;
  double lower_left_u = 0.0;
  double lower_left_v = 0.0;
  double upper_right_u = 0.0;
  double upper_right_v = 0.0;
  double lower_right_u = 0.0;
  double lower_right_v = 0.0;

  using texture_field::ReadNumber;
  if (!ReadNumber(L, source, texture_field::kTexCoordUpperLeftX,
                  &upper_left_u) ||
      !ReadNumber(L, source, texture_field::kTexCoordUpperLeftY,
                  &upper_left_v) ||
      !ReadNumber(L, source, texture_field::kTexCoordLowerLeftX,
                  &lower_left_u) ||
      !ReadNumber(L, source, texture_field::kTexCoordLowerLeftY,
                  &lower_left_v) ||
      !ReadNumber(L, source, texture_field::kTexCoordUpperRightX,
                  &upper_right_u) ||
      !ReadNumber(L, source, texture_field::kTexCoordUpperRightY,
                  &upper_right_v) ||
      !ReadNumber(L, source, texture_field::kTexCoordLowerRightX,
                  &lower_right_u) ||
      !ReadNumber(L, source, texture_field::kTexCoordLowerRightY,
                  &lower_right_v)) {
    return false;
  }

  *out_quad = {
      .upper_left = {static_cast<float>(upper_left_u),
                     static_cast<float>(upper_left_v)},
      .lower_left = {static_cast<float>(lower_left_u),
                     static_cast<float>(lower_left_v)},
      .upper_right = {static_cast<float>(upper_right_u),
                      static_cast<float>(upper_right_v)},
      .lower_right = {static_cast<float>(lower_right_u),
                      static_cast<float>(lower_right_v)},
  };
  return true;
}

std::optional<std::string> ReadLuaFrameReferenceKey(lua_State* L,
                                                    int value_index) {
  value_index = lua_absindex(L, value_index);
  if (lua_isstring(L, value_index) != 0) {
    if (const char* name = lua_tostring(L, value_index);
        name != nullptr && name[0] != '\0') {
      return std::string(name);
    }
    return std::nullopt;
  }

  if (lua_istable(L, value_index) == 0) {
    return std::nullopt;
  }

  if (const auto key = GetLuaStringField(
          L, value_index,
          openwow::ui::game::frame_api::kLuaFrameRuntimeKeyField);
      key.has_value() && !key->empty()) {
    return key;
  }

  if (const auto name = GetLuaStringField(L, value_index, "__ow_name");
      name.has_value() && !name->empty()) {
    return name;
  }
  return std::nullopt;
}

namespace local_gradient {
using GradientColor = openwow::ui::widgets::TextureGradientColor;
using Orientation = openwow::ui::widgets::TextureGradientOrientation;

struct TextureGradientDefinition {
  bool enabled = false;
  Orientation orientation = Orientation::Horizontal;
  GradientColor min_color{};
  GradientColor max_color{};

  [[nodiscard]] std::array<GradientColor, 4> ToUiRendererCornerColors() const {
    std::array<GradientColor, 4> colors{};
    if (orientation == Orientation::Horizontal) {
      colors[0] = min_color;
      colors[1] = max_color;
      colors[2] = max_color;
      colors[3] = min_color;
    } else {
      colors[0] = max_color;
      colors[1] = max_color;
      colors[2] = min_color;
      colors[3] = min_color;
    }
    return colors;
  }
};
}

using lua_projection::TextureRenderState;

struct LuaGradientColorFields {
  int r;
  int g;
  int b;
  int a;
};

constexpr LuaGradientColorFields kLuaGradientMinFields{
    texture_field::kGradientMinR, texture_field::kGradientMinG,
    texture_field::kGradientMinB, texture_field::kGradientMinA};
constexpr LuaGradientColorFields kLuaGradientMaxFields{
    texture_field::kGradientMaxR, texture_field::kGradientMaxG,
    texture_field::kGradientMaxB, texture_field::kGradientMaxA};

static void ApplyTextureGradientDefinition(
    const local_gradient::TextureGradientDefinition& gradient,
    std::array<TextureRenderState::GradientColor, 4>* out_colors) {
  if (out_colors == nullptr || !gradient.enabled) {
    return;
  }

  *out_colors = gradient.ToUiRendererCornerColors();
}

static void ApplyFrameTextureDefaultsExceptPath(
    const openwow::ui::framexml::UiFrame& frame, TextureRenderState* state) {
  const auto coordinates =
      openwow::ui::framexml::EffectiveTextureCoordinates(frame);
  state->uv_quad = {
      .upper_left = {coordinates.upper_left.u, coordinates.upper_left.v},
      .lower_left = {coordinates.lower_left.u, coordinates.lower_left.v},
      .upper_right = {coordinates.upper_right.u, coordinates.upper_right.v},
      .lower_right = {coordinates.lower_right.u, coordinates.lower_right.v},
  };
  state->has_custom_uv_quad =
      openwow::ui::framexml::TextureCoordinatesWereSpecified(frame);
  state->tile_x = frame.tile_x;
  state->tile_y = frame.tile_y;
  state->color_r = frame.color_r;
  state->color_g = frame.color_g;
  state->color_b = frame.color_b;
  state->color_a = frame.texture_alpha.value_or(frame.color_a);

  const std::string_view alpha_mode = frame.alpha_mode;
  if (openwow::text::EqualsIgnoreCaseAscii(alpha_mode, "DISABLE")) {
    state->blend = openwow::render::ui::BlendMode::kOpaque;
  } else if (openwow::text::EqualsIgnoreCaseAscii(alpha_mode, "ALPHAKEY")) {
    state->blend = openwow::render::ui::BlendMode::kAlphaKey;
  } else if (openwow::text::EqualsIgnoreCaseAscii(alpha_mode, "ADD")) {
    state->blend = openwow::render::ui::BlendMode::kAdditive;
  } else if (openwow::text::EqualsIgnoreCaseAscii(alpha_mode, "MOD")) {
    state->blend = openwow::render::ui::BlendMode::kModulate;
  }

  state->solid_color_texture = frame.file.empty() && frame.has_vertex_color;
  state->has_gradient = frame.gradient.enabled;
  if (frame.gradient.enabled) {
    const auto colors = frame.gradient.ToUiRendererCornerColors();
    for (std::size_t index = 0; index < colors.size(); ++index) {
      state->gradient_colors[index] = {
          colors[index].r,
          colors[index].g,
          colors[index].b,
          colors[index].a,
      };
    }
  }
}

void ApplyFrameTextureDefaults(const openwow::ui::framexml::UiFrame& frame,
                               TextureRenderState* state) {
  if (state == nullptr) {
    return;
  }

  state->texture_path = frame.file;
  ApplyFrameTextureDefaultsExceptPath(frame, state);
}

static float ReadLuaQuantizedColorField(lua_State* L,
                                        const texture_field::Source& source,
                                        const int key,
                                        const float fallback) {
  double value = fallback;
  (void)texture_field::ReadNumber(L, source, key, &value);
  return static_cast<float>(
             QuantizeNormalizedColorByte(static_cast<float>(value))) /
         255.0f;
}

static TextureRenderState::GradientColor ReadLuaGradientColor(
    lua_State* L, const texture_field::Source& source,
    const LuaGradientColorFields& fields) {
  return {
      .r = ReadLuaQuantizedColorField(L, source, fields.r, 0.0f),
      .g = ReadLuaQuantizedColorField(L, source, fields.g, 0.0f),
      .b = ReadLuaQuantizedColorField(L, source, fields.b, 0.0f),
      .a = ReadLuaQuantizedColorField(L, source, fields.a, 1.0f),
  };
}

static bool TryBuildLuaTextureGradient(
    lua_State* L, const texture_field::Source& source,
    std::array<TextureRenderState::GradientColor, 4>* out_colors) {
  if (out_colors == nullptr) {
    return false;
  }

  texture_field::Push(L, source, texture_field::kGradientOrientation);
  const char* const orientation_text = lua_tostring(L, -1);
  const bool horizontal =
      orientation_text != nullptr &&
      openwow::text::EqualsIgnoreCaseAscii(std::string_view(orientation_text),
                                           "HORIZONTAL");
  const bool vertical =
      orientation_text != nullptr &&
      openwow::text::EqualsIgnoreCaseAscii(std::string_view(orientation_text),
                                           "VERTICAL");
  lua_pop(L, 1);
  if (!horizontal && !vertical) {
    return false;
  }

  local_gradient::TextureGradientDefinition gradient;
  gradient.enabled = true;
  gradient.orientation = horizontal ? local_gradient::Orientation::Horizontal
                                    : local_gradient::Orientation::Vertical;
  gradient.min_color = ReadLuaGradientColor(L, source, kLuaGradientMinFields);
  gradient.max_color = ReadLuaGradientColor(L, source, kLuaGradientMaxFields);
  ApplyTextureGradientDefinition(gradient, out_colors);
  return true;
}

static std::optional<openwow::game::TabardEmblemRenderTargetDescriptor>
TryGetTabardEmblemRenderTargetDescriptor(
    lua_State* L, const texture_field::Source& source) {

  int width = 0;
  int height = 0;
  int pitch = 0;
  if (!texture_field::HasString(L, source,
                                texture_field::kTabardSourceTexture) ||
      !texture_field::HasString(L, source,
                                texture_field::kTabardRenderTargetName) ||
      !texture_field::ReadInteger(L, source, texture_field::kTabardWidth,
                                  &width) ||
      !texture_field::ReadInteger(L, source, texture_field::kTabardHeight,
                                  &height) ||
      !texture_field::ReadInteger(L, source, texture_field::kTabardPitch,
                                  &pitch)) {
    return std::nullopt;
  }

  openwow::game::TabardEmblemRenderTargetDescriptor descriptor;
  descriptor.sourceTexturePath =
      texture_field::ReadString(L, source, texture_field::kTabardSourceTexture)
          .value_or(std::string{});
  descriptor.renderTargetName =
      texture_field::ReadString(L, source,
                                texture_field::kTabardRenderTargetName)
          .value_or(std::string{});
  descriptor.width =
      static_cast<std::uint32_t>(std::max<lua_Integer>(0, width));
  descriptor.height =
      static_cast<std::uint32_t>(std::max<lua_Integer>(0, height));
  descriptor.pitch =
      static_cast<std::uint32_t>(std::max<lua_Integer>(0, pitch));
  descriptor.forceWhiteRgb =
      texture_field::ReadBoolean(L, source, texture_field::kTabardForceWhiteRgb);
  descriptor.copySourceAlpha = texture_field::ReadBoolean(
      L, source, texture_field::kTabardCopySourceAlpha);
  if (descriptor.width == 0 || descriptor.height == 0 ||
      descriptor.pitch == 0) {
    return std::nullopt;
  }

  return descriptor;
}

TextureRenderState BuildTextureRenderState(
    lua_State* L, const int table_index,
    const openwow::ui::framexml::UiFrame& frame,
    openwow::game::WorldSession* session,
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::render::PortraitRenderer* portraits,
    std::uint8_t* portrait_view_id, const std::uint16_t portrait_view_limit) {
  TextureRenderState state;
  BuildTextureRenderStateInto(L, table_index, frame, session, vfs, portraits,
                              portrait_view_id, portrait_view_limit, &state);
  return state;
}

static TextureRenderState& ResetTextureRenderStateForRegion(
    const openwow::ui::framexml::UiFrame& frame, TextureRenderState* out_state) {
  std::string reusable_texture_path = std::move(out_state->texture_path);
  reusable_texture_path.clear();
  *out_state = TextureRenderState{};
  out_state->texture_path = std::move(reusable_texture_path);
  ApplyFrameTextureDefaultsExceptPath(frame, out_state);
  return *out_state;
}

static void BindTexturePortraitFromTokens(
    const std::optional<std::string>& portrait_unit,
    const std::optional<std::string>& portrait_guid,
    openwow::game::WorldSession* session,
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::render::PortraitRenderer* portraits,
    std::uint8_t* portrait_view_id, const std::uint16_t portrait_view_limit,
    TextureRenderState& state) {
  if (session == nullptr || vfs == nullptr || portraits == nullptr ||
      portrait_view_id == nullptr) {
    return;
  }
  const auto bind_dynamic_portrait =
      [&](const openwow::game::WorldObject* object) {
        if (object == nullptr || !object->IsUnit()) {
          return;
        }

        const auto binding =
            portraits->Acquire(object->GetGuid(), object->GetDisplayId(),
                               object->GetPrimaryM2InstanceId(),
                               *portrait_view_id, portrait_view_limit);
        if (!binding.texture.has_value()) {
          return;
        }

        state.dynamic_texture = binding.texture->handle;
        state.dynamic_texture_width = binding.texture->width;
        state.dynamic_texture_height = binding.texture->height;
        state.dynamic_texture_is_render_target = true;
        state.texture_path.clear();
      };

  if (portrait_unit.has_value()) {
    bind_dynamic_portrait(detail::ResolveUnit(session, *portrait_unit));
  } else if (portrait_guid.has_value() && !portrait_guid->empty()) {
    const auto raw_guid = static_cast<std::uint64_t>(
        std::strtoull(portrait_guid->c_str(), nullptr, 10));
    if (raw_guid != 0) {
      bind_dynamic_portrait(
          session->objects().Get(openwow::game::ObjectGuid(raw_guid)));
    }
  }
}

void BuildTextureRenderStateFromLuaFieldsInto(
    lua_State* L, const int table_index,
    const openwow::ui::framexml::UiFrame& frame,
    openwow::game::WorldSession* session,
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::render::PortraitRenderer* portraits,
    std::uint8_t* portrait_view_id, const std::uint16_t portrait_view_limit,
    TextureRenderState* out_state) {
  if (out_state == nullptr) {
    return;
  }
  TextureRenderState& state =
      ResetTextureRenderStateForRegion(frame, out_state);

  const texture_field::Source source{
      .table = lua_absindex(L, table_index),
      .keys = runtime::PushInternedLuaFieldKeyBlock(
          L, texture_field::kTextureFieldNames,
          static_cast<int>(std::size(texture_field::kTextureFieldNames))),
  };

  struct KeyBlockPop {
    lua_State* lua;
    int restore_top;
    ~KeyBlockPop() { lua_settop(lua, restore_top); }
  } const key_block_pop{L, source.keys - 1};

  if (texture_field::ReadStringInto(L, source, texture_field::kTexture,
                                    &state.texture_path)) {
    state.clear_texture = false;
  } else if (texture_field::ReadBoolean(L, source,
                                        texture_field::kTextureCleared)) {
    state.texture_path.clear();
    state.clear_texture = true;
  } else {

    state.texture_path = frame.file;
    BindTexturePortraitFromTokens(
        texture_field::ReadString(L, source, texture_field::kPortraitUnit),
        texture_field::ReadString(L, source, texture_field::kPortraitGuid),
        session, vfs, portraits, portrait_view_id, portrait_view_limit,
        state);
  }

  TextureQuadUv custom_uv_quad{};
  if (TryGetLuaTextureUvQuad(L, source, &custom_uv_quad)) {
    state.uv_quad = custom_uv_quad;
    state.has_custom_uv_quad = true;
  }

  if (const auto tile_x_override = texture_field::ReadBooleanIfPresent(
          L, source, texture_field::kHorizontalTile);
      tile_x_override.has_value()) {
    state.tile_x = *tile_x_override;
  }
  if (const auto tile_y_override = texture_field::ReadBooleanIfPresent(
          L, source, texture_field::kVerticalTile);
      tile_y_override.has_value()) {
    state.tile_y = *tile_y_override;
  }

  double component = 0.0;
  if (texture_field::ReadNumber(L, source, texture_field::kVertexColorR,
                                &component)) {
    state.color_r = static_cast<float>(component);
  }
  if (texture_field::ReadNumber(L, source, texture_field::kVertexColorG,
                                &component)) {
    state.color_g = static_cast<float>(component);
  }
  if (texture_field::ReadNumber(L, source, texture_field::kVertexColorB,
                                &component)) {
    state.color_b = static_cast<float>(component);
  }
  if (texture_field::ReadNumber(L, source, texture_field::kVertexColorA,
                                &component)) {
    state.color_a = static_cast<float>(component);
  }

  if (state.texture_path.empty() && !state.clear_texture) {
    if (texture_field::ReadNumber(L, source, texture_field::kSolidColorR,
                                  &component)) {
      state.color_r = static_cast<float>(component);
      state.solid_color_texture = true;
    }
    if (texture_field::ReadNumber(L, source, texture_field::kSolidColorG,
                                  &component)) {
      state.color_g = static_cast<float>(component);
      state.solid_color_texture = true;
    }
    if (texture_field::ReadNumber(L, source, texture_field::kSolidColorB,
                                  &component)) {
      state.color_b = static_cast<float>(component);
      state.solid_color_texture = true;
    }
    if (texture_field::ReadNumber(L, source, texture_field::kSolidColorA,
                                  &component)) {
      state.color_a = static_cast<float>(component);
      state.solid_color_texture = true;
    }
  }

  std::array<TextureRenderState::GradientColor, 4> lua_gradient_colors{};
  if (TryBuildLuaTextureGradient(L, source, &lua_gradient_colors)) {
    state.gradient_colors = lua_gradient_colors;
    state.has_gradient = true;
  }

  texture_field::Push(L, source, texture_field::kBlendMode);
  if (const char* const blend_mode = lua_tostring(L, -1);
      blend_mode != nullptr) {
    const std::string_view mode{blend_mode};
    if (openwow::text::EqualsIgnoreCaseAscii(mode, "DISABLE")) {
      state.blend = openwow::render::ui::BlendMode::kOpaque;
    } else if (openwow::text::EqualsIgnoreCaseAscii(mode, "ALPHAKEY")) {
      state.blend = openwow::render::ui::BlendMode::kAlphaKey;
    } else if (openwow::text::EqualsIgnoreCaseAscii(mode, "ADD")) {
      state.blend = openwow::render::ui::BlendMode::kAdditive;
    } else if (openwow::text::EqualsIgnoreCaseAscii(mode, "MOD")) {
      state.blend = openwow::render::ui::BlendMode::kModulate;
    } else {
      state.blend = openwow::render::ui::BlendMode::kAlpha;
    }
  }
  lua_pop(L, 1);
  state.desaturated =
      texture_field::ReadBoolean(L, source, texture_field::kDesaturated);

  state.tabard_emblem_render_target =
      TryGetTabardEmblemRenderTargetDescriptor(L, source);
  state.dynamic_texture_is_render_target =
      state.dynamic_texture_is_render_target ||
      state.tabard_emblem_render_target.has_value();
}

static TextureRenderState::GradientColor QuantizeSourceGradientColor(
    const std::array<std::optional<double>, 4>& components) {
  const auto quantize = [](const std::optional<double>& component,
                           const float fallback) {
    const double value = component.value_or(static_cast<double>(fallback));
    return static_cast<float>(
               QuantizeNormalizedColorByte(static_cast<float>(value))) /
           255.0f;
  };
  return {
      .r = quantize(components[0], 0.0f),
      .g = quantize(components[1], 0.0f),
      .b = quantize(components[2], 0.0f),
      .a = quantize(components[3], 1.0f),
  };
}

void BuildTextureRenderStateInto(
    lua_State* L, const int table_index,
    const openwow::ui::framexml::UiFrame& frame,
    openwow::game::WorldSession* session,
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::render::PortraitRenderer* portraits,
    std::uint8_t* portrait_view_id, const std::uint16_t portrait_view_limit,
    TextureRenderState* out_state) {
  if (out_state == nullptr) {
    return;
  }
  TextureRenderState& state =
      ResetTextureRenderStateForRegion(frame, out_state);

  const runtime::TextureRenderStateSource* const source =
      runtime::FindTextureRenderStateSource(L, table_index);
  if (source == nullptr) {
    state.texture_path = frame.file;
    return;
  }

  if (source->texture.has_value()) {
    state.texture_path = *source->texture;
    state.clear_texture = false;
  } else if (source->texture_cleared) {
    state.texture_path.clear();
    state.clear_texture = true;
  } else {
    state.texture_path = frame.file;
    BindTexturePortraitFromTokens(source->portrait_unit, source->portrait_guid,
                                  session, vfs, portraits, portrait_view_id,
                                  portrait_view_limit, state);
  }

  if (source->has_tex_coord_quad) {
    const auto& quad = source->tex_coord_quad;
    state.uv_quad = {
        .upper_left = {static_cast<float>(quad[0]), static_cast<float>(quad[1])},
        .lower_left = {static_cast<float>(quad[2]), static_cast<float>(quad[3])},
        .upper_right = {static_cast<float>(quad[4]), static_cast<float>(quad[5])},
        .lower_right = {static_cast<float>(quad[6]), static_cast<float>(quad[7])},
    };
    state.has_custom_uv_quad = true;
  }

  if (source->horizontal_tile.has_value()) {
    state.tile_x = *source->horizontal_tile;
  }
  if (source->vertical_tile.has_value()) {
    state.tile_y = *source->vertical_tile;
  }

  if (source->vertex_color[0].has_value()) {
    state.color_r = static_cast<float>(*source->vertex_color[0]);
  }
  if (source->vertex_color[1].has_value()) {
    state.color_g = static_cast<float>(*source->vertex_color[1]);
  }
  if (source->vertex_color[2].has_value()) {
    state.color_b = static_cast<float>(*source->vertex_color[2]);
  }
  if (source->vertex_color[3].has_value()) {
    state.color_a = static_cast<float>(*source->vertex_color[3]);
  }

  if (state.texture_path.empty() && !state.clear_texture) {
    if (source->solid_color[0].has_value()) {
      state.color_r = static_cast<float>(*source->solid_color[0]);
      state.solid_color_texture = true;
    }
    if (source->solid_color[1].has_value()) {
      state.color_g = static_cast<float>(*source->solid_color[1]);
      state.solid_color_texture = true;
    }
    if (source->solid_color[2].has_value()) {
      state.color_b = static_cast<float>(*source->solid_color[2]);
      state.solid_color_texture = true;
    }
    if (source->solid_color[3].has_value()) {
      state.color_a = static_cast<float>(*source->solid_color[3]);
      state.solid_color_texture = true;
    }
  }

  if (source->gradient_orientation.has_value()) {
    local_gradient::TextureGradientDefinition gradient;
    gradient.enabled = true;
    gradient.orientation = *source->gradient_orientation;
    gradient.min_color = QuantizeSourceGradientColor(source->gradient_min);
    gradient.max_color = QuantizeSourceGradientColor(source->gradient_max);
    ApplyTextureGradientDefinition(gradient, &state.gradient_colors);
    state.has_gradient = true;
  }

  if (source->blend.has_value()) {
    state.blend = *source->blend;
  }
  state.desaturated = source->desaturated;

  if (source->tabard.has_value()) {

    const auto& tabard = *source->tabard;
    openwow::game::TabardEmblemRenderTargetDescriptor descriptor;
    descriptor.sourceTexturePath = tabard.source_texture;
    descriptor.renderTargetName = tabard.render_target_name;
    descriptor.width =
        static_cast<std::uint32_t>(std::max<std::int32_t>(0, tabard.width));
    descriptor.height =
        static_cast<std::uint32_t>(std::max<std::int32_t>(0, tabard.height));
    descriptor.pitch =
        static_cast<std::uint32_t>(std::max<std::int32_t>(0, tabard.pitch));
    descriptor.forceWhiteRgb = tabard.force_white_rgb;
    descriptor.copySourceAlpha = tabard.copy_source_alpha;
    if (descriptor.width != 0 && descriptor.height != 0 &&
        descriptor.pitch != 0) {
      state.tabard_emblem_render_target = std::move(descriptor);
    }
  }
  state.dynamic_texture_is_render_target =
      state.dynamic_texture_is_render_target ||
      state.tabard_emblem_render_target.has_value();
}

bool ShouldRenderNativeTextureRegion(
    lua_State* L, const openwow::ui::framexml::UiFrame& frame,
    const int region_ref, const runtime::FrameStore& frames,
    const std::uint64_t region_handle, const std::string& mouseover_frame) {
  using TextureRole = openwow::ui::framexml::UiFrame::TextureRole;
  if (frame.texture_role == TextureRole::Normal) return true;

  const char* slot = frame_api::NativeTextureSlotField(frame);

  const auto owner_ref = frames.FindLuaRef(frames.ParentHandleOf(region_handle));
  if (L == nullptr || slot == nullptr || region_ref == LUA_NOREF ||
      !owner_ref.has_value()) {
    return false;
  }

  const int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, *owner_ref);
  if (lua_istable(L, -1) == 0) {
    lua_settop(L, top);
    return false;
  }
  const int owner_index = lua_absindex(L, -1);
  lua_rawgeti(L, LUA_REGISTRYINDEX, region_ref);
  if (lua_istable(L, -1) == 0) {
    lua_settop(L, top);
    return false;
  }
  const int region_index = lua_absindex(L, -1);
  GetInternedLuaField(L, owner_index, slot);
  const bool owns_region = lua_rawequal(L, -1, region_index) != 0;
  lua_pop(L, 1);
  if (!owns_region) {
    lua_settop(L, top);
    return false;
  }

  RawGetInternedLuaField(L, owner_index, "__ow_btn_state");
  const char* const button_state_text = lua_tostring(L, -1);
  const std::string_view state{button_state_text != nullptr ? button_state_text
                                                            : "NORMAL"};
  bool disabled = openwow::text::EqualsIgnoreCaseAscii(state, "DISABLED");
  const bool pushed = openwow::text::EqualsIgnoreCaseAscii(state, "PUSHED");
  lua_pop(L, 1);
  GetInternedLuaField(L, owner_index, "__ow_btn_enabled");
  if (lua_isboolean(L, -1) != 0 && lua_toboolean(L, -1) == 0) disabled = true;
  lua_pop(L, 1);

  bool visible = false;
  switch (frame.texture_role) {
    case TextureRole::ButtonNormal:
      visible = !disabled && !pushed;
      break;
    case TextureRole::ButtonPushed:
      visible = !disabled && pushed;
      break;
    case TextureRole::ButtonDisabled:
      visible = disabled;
      break;
    case TextureRole::ButtonHighlight:
      visible = !disabled &&
                (mouseover_frame == frame.parent ||
                 GetLuaBooleanField(L, owner_index, "__ow_btn_hl_locked"));
      break;
    case TextureRole::CheckButtonChecked:
      visible = !disabled && GetLuaBooleanField(L, owner_index, "__ow_checked");
      break;
    case TextureRole::CheckButtonDisabledChecked:
      visible = disabled && GetLuaBooleanField(L, owner_index, "__ow_checked");
      break;
    case TextureRole::SliderThumb:
    case TextureRole::StatusBarFill:
    case TextureRole::ColorSelectWheel:
    case TextureRole::ColorSelectWheelThumb:
    case TextureRole::ColorSelectValue:
    case TextureRole::ColorSelectValueThumb:
    case TextureRole::Normal:
      visible = true;
      break;
  }
  lua_settop(L, top);
  return visible;
}

namespace {
constexpr float kScaleWriteEpsilon = 0.00000023841858f;
}

static bool ReadLuaOptionalBooleanField(lua_State* L,
                                        const metadata_field::Source& source,
                                        const int key, bool* const out_value) {
  metadata_field::Get(L, source, key);
  const bool has_value = lua_isboolean(L, -1) != 0;
  if (has_value) {
    *out_value = lua_toboolean(L, -1) != 0;
  }
  lua_pop(L, 1);
  return has_value;
}

static bool LuaFrameHasHandler(lua_State* L,
                               const metadata_field::Source& source,
                               const metadata_field::HandlerOrdinals& handler) {
  metadata_field::Get(L, source, handler.stored);
  const bool stored_handler = lua_isfunction(L, -1) != 0;
  lua_pop(L, 1);
  if (stored_handler) {
    return true;
  }

  metadata_field::Get(L, source, handler.direct);
  const bool direct_handler = lua_isfunction(L, -1) != 0;
  lua_pop(L, 1);
  return direct_handler;
}

static bool LuaFrameUsesMouseCategory(lua_State* L,
                                      const metadata_field::Source& source,
                                      const std::string_view kind) {
  bool explicit_value = false;
  if (ReadLuaOptionalBooleanField(L, source, metadata_field::kMouseEnabled,
                                  &explicit_value) ||
      ReadLuaOptionalBooleanField(L, source, metadata_field::kEnableMouse,
                                  &explicit_value)) {
    return explicit_value;
  }

  return openwow::ui::framexml::StockConstructorEnablesMouse(kind);
}

static bool LuaFrameUsesMouseWheelCategory(
    lua_State* L, const metadata_field::Source& source) {
  bool explicit_value = false;
  if (ReadLuaOptionalBooleanField(L, source, metadata_field::kMouseWheelEnabled,
                                  &explicit_value)) {
    return explicit_value;
  }

  return LuaFrameHasHandler(L, source, metadata_field::kMouseWheelHandler);
}

static bool LuaFrameUsesKeyboardCategory(
    lua_State* L, const metadata_field::Source& source) {
  bool explicit_value = false;
  if (ReadLuaOptionalBooleanField(L, source, metadata_field::kKeyboardEnabled,
                                  &explicit_value) ||
      ReadLuaOptionalBooleanField(L, source, metadata_field::kEnableKeyboard,
                                  &explicit_value)) {
    return explicit_value;
  }
  return false;
}

static std::string FormatFrameStackLuaValueLabel(lua_State* L,
                                                 int value_index) {
  value_index = lua_absindex(L, value_index);
  const int value_type = lua_type(L, value_index);
  const char* type_name = lua_typename(L, value_type);
  const void* value_pointer = lua_topointer(L, value_index);

  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), "%s: %p",
                type_name != nullptr ? type_name : "nil", value_pointer);
  return buffer;
}

static bool LuaValueReferencesFrameThis(lua_State* L, int table_index,
                                        const void* this_pointer) {
  if (this_pointer == nullptr || lua_istable(L, table_index) == 0) {
    return false;
  }

  table_index = lua_absindex(L, table_index);
  lua_rawgeti(L, table_index, 0);
  const bool matches = lua_type(L, -1) == LUA_TLIGHTUSERDATA &&
                       lua_touserdata(L, -1) == this_pointer;
  lua_pop(L, 1);
  return matches;
}

static std::string BuildFrameStackEntryLabel(lua_State* L,
                                             const int frame_index) {
  const int absolute_index = lua_absindex(L, frame_index);
  const char* frame_name =
      openwow::ui::BorrowRawLuaStringField(L, absolute_index, "__ow_name");
  if (frame_name == nullptr || frame_name[0] == '\0') {
    return FormatFrameStackLuaValueLabel(L, absolute_index);
  }

  const void* this_pointer =
      openwow::ui::game::detail::GetLuaScriptObjectThisPointer(L,
                                                               absolute_index);

  lua_getglobal(L, frame_name);
  const bool global_matches_frame =
      lua_istable(L, -1) != 0 &&
      LuaValueReferencesFrameThis(L, -1, this_pointer);

  std::string label;
  if (global_matches_frame) {
    label = frame_name;
  } else if (lua_istable(L, -1) != 0) {
    label = FormatFrameStackLuaValueLabel(L, absolute_index);
    label.append(" (").append(frame_name).append(")");
  } else {
    label = FormatFrameStackLuaValueLabel(L, -1);
    label.append(" (").append(frame_name).append(")");
  }

  lua_pop(L, 1);
  return label;
}

}

using namespace lua_projection;

void GameUIManager::NotifyFrameInputCategoryMutation(
    const std::string& frame_name, const bool reindex_only) {
  lua_State* const lua_ = lua_state();
  if (lua_ == nullptr || frame_name.empty()) {
    return;
  }

  const bool focused_was_effectively_visible =
      frame_input_router_.FocusedFrameIsEffectivelyVisible();
  bool retained_visibility_changed = false;

  const auto handle = frame_store_.HandleOf(frame_name);
  const auto ref = frame_store_.FindLuaRef(handle);
  if (ref.has_value()) {
    const int stack_top = lua_gettop(lua_);
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
    if (lua_istable(lua_, -1) != 0) {
      auto* const frame_ptr = frame_store_.FindFrame(handle);
      if (frame_ptr == nullptr) {
        lua_settop(lua_, stack_top);
        return;
      }
      auto& frame = *frame_ptr;

      const metadata_field::Source source{
          .table = lua_absindex(lua_, -1),
          .keys = runtime::PushInternedLuaFieldKeyBlock(
              lua_, metadata_field::kNames,
              static_cast<int>(std::size(metadata_field::kNames))),
      };

      std::string old_parent;
      bool parent_changed = false;
      bool strata_changed = false;
      const int old_level = frame.frame_level;
      const float old_depth = frame.depth;
      const float old_scale = frame.scale;
      const bool old_visible = frame.visible;
      const bool old_draw_layer_enabled = frame.runtime_draw_layer_enabled;
      const bool old_uses_mouse = frame.runtime_uses_mouse;
      const bool old_uses_mouse_wheel = frame.runtime_uses_mouse_wheel;
      const bool old_uses_keyboard = frame.runtime_uses_keyboard;

      const auto replace_parent = [&](std::string&& parent) {
        old_parent = std::move(frame.parent);
        frame.parent = std::move(parent);
        parent_changed = true;
      };
      metadata_field::RawGet(lua_, source, metadata_field::kParentName);
      const bool has_parent_name = [&] {
        const char* const text = lua_tostring(lua_, -1);
        if (text == nullptr) return false;
        if (frame.parent != text) replace_parent(std::string(text));
        return true;
      }();
      lua_pop(lua_, 1);
      if (!has_parent_name) {
        metadata_field::Get(lua_, source, metadata_field::kParent);
        if (auto parent_ref_key = ReadLuaFrameReferenceKey(lua_, -1);
            parent_ref_key.has_value()) {
          if (frame.parent != *parent_ref_key) {
            replace_parent(std::move(*parent_ref_key));
          }
        } else if (!frame.parent.empty()) {
          replace_parent(std::string());
        }
        lua_pop(lua_, 1);
      }

      metadata_field::RawGet(lua_, source, metadata_field::kFrameLevel);
      if (lua_isinteger(lua_, -1) != 0) {
        frame.frame_level = static_cast<int>(lua_tointeger(lua_, -1));
      }
      lua_pop(lua_, 1);

      metadata_field::RawGet(lua_, source, metadata_field::kFrameStrata);
      if (const char* const strata = lua_tostring(lua_, -1);
          strata != nullptr && *strata != '\0' && frame.frame_strata != strata) {
        frame.frame_strata.assign(strata);
        strata_changed = true;
      }
      lua_pop(lua_, 1);

      metadata_field::Get(lua_, source, metadata_field::kDepth);
      if (lua_isnumber(lua_, -1) != 0) {
        frame.depth = static_cast<float>(lua_tonumber(lua_, -1));
      }
      lua_pop(lua_, 1);
      const auto read_raw_number_into = [&](const int key, auto* const out) {
        metadata_field::RawGet(lua_, source, key);
        if (lua_isnumber(lua_, -1) != 0) {
          *out = static_cast<float>(lua_tonumber(lua_, -1));
        }
        lua_pop(lua_, 1);
      };
      read_raw_number_into(metadata_field::kWidth, &frame.width);
      read_raw_number_into(metadata_field::kHeight, &frame.height);
      read_raw_number_into(metadata_field::kScale, &frame.scale);

      frame.enable_mouse = false;
      (void)ReadLuaOptionalBooleanField(lua_, source,
                                        metadata_field::kMouseEnabled,
                                        &frame.enable_mouse);
      (void)ReadLuaOptionalBooleanField(lua_, source,
                                        metadata_field::kEnableMouse,
                                        &frame.enable_mouse);
      const bool kind_changed =
          SyncFrameRuntimeMetadataFromLua(lua_, source, &frame);
      SyncRetainedBackdropColors(lua_, source.table, frame_name, &frame_store_);
      retained_visibility_changed =
          old_visible != frame.visible ||
          old_draw_layer_enabled != frame.runtime_draw_layer_enabled;

      if (parent_changed || kind_changed) {
        frame_traversal_index_.InvalidateHierarchy();
      } else {
        if (strata_changed || old_level != frame.frame_level) {
          frame_traversal_index_.InvalidateFrameOrder(
              frame_name,
              runtime::FrameTraversalIndex::OrderInvalidationKind::kOrderKey);
        }
        if (std::fabs(old_depth - frame.depth) >= kScaleWriteEpsilon ||
            std::fabs(old_scale - frame.scale) >= kScaleWriteEpsilon ||
            retained_visibility_changed ||
            old_uses_mouse != frame.runtime_uses_mouse ||
            old_uses_mouse_wheel != frame.runtime_uses_mouse_wheel ||
            old_uses_keyboard != frame.runtime_uses_keyboard) {
          frame_traversal_index_.InvalidateFrameOrder(
              frame_name,
              runtime::FrameTraversalIndex::OrderInvalidationKind::kInherited);
        }
      }
      if (kind_changed) {

        frame_store_.InvalidateClassificationIndex();
      }
      if (parent_changed) {
        frame_store_.NotifyHierarchyMutation(frame_name, old_parent);

        retained_layout_.ReindexDependencies(frame_name);
      }
    }
    lua_settop(lua_, stack_top);
  }

  frame_traversal_index_.InvalidateHitTest();

  if (!reindex_only) {
    retained_layout_.QueueLuaMutation(frame_name);
  }

  frame_input_router_.ReconcileFrameInputMutation(
      frame_name, focused_was_effectively_visible);
}

bool GameUIManager::BuildFrameStackSnapshot(
    const bool show_hidden, TooltipFrameStackSnapshot* const out_snapshot) {
  lua_State* const lua_ = lua_state();
  if (out_snapshot == nullptr) {
    return false;
  }

  out_snapshot->cursor_x = 0.0f;
  out_snapshot->cursor_y = 0.0f;
  out_snapshot->entries.clear();

  if (!is_initialized() || lua_ == nullptr) {
    return false;
  }

  retained_layout_.SyncTrackedFramesFromLua();
  retained_layout_.SolveIfDirty();
  if (frame_traversal_index_.order_dirty()) {
    frame_traversal_index_.Rebuild(root_scale(), screen_height());
  }

  const auto [mouse_x, mouse_y] =
      openwow::input::InputManager::Get().GetMousePosition();
  if (screen_height() > 0.0f) {
    const float pixels_to_script = 768.0f / screen_height();
    out_snapshot->cursor_x = static_cast<float>(mouse_x) * pixels_to_script;
    out_snapshot->cursor_y =
        (screen_height() - static_cast<float>(mouse_y)) * pixels_to_script;
  }

  const auto stack = frame_traversal_index_.FrameStackAt(
      static_cast<float>(mouse_x), static_cast<float>(mouse_y), show_hidden,
      screen_height());
  out_snapshot->entries.reserve(stack.size());
  for (const auto& candidate : stack) {
    const int stack_top = lua_gettop(lua_);
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, candidate.lua_ref);
    if (lua_istable(lua_, -1) == 0) {
      lua_settop(lua_, stack_top);
      continue;
    }

    const int frame_index = lua_absindex(lua_, -1);
    if (!openwow::ui::game::detail::IsFrameLikeLookupObjectType(
            openwow::ui::game::detail::GetLuaFrameLookupObjectType(
                lua_, frame_index))) {
      lua_settop(lua_, stack_top);
      continue;
    }

    out_snapshot->entries.push_back(TooltipFrameStackEntry{
        .label = BuildFrameStackEntryLabel(lua_, frame_index),
        .strata = candidate.strata,
        .level = candidate.level,
        .mouse_enabled = candidate.mouse_enabled,
        .visible = candidate.visible,
    });
    lua_settop(lua_, stack_top);
  }

  return true;
}

}
