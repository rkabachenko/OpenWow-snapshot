#pragma once

#include "openwow/ui/framexml/texture_role.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/ui/widgets/status_bar_definition.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::xml {
struct XMLNode;
}

namespace openwow::ui::framexml {

enum class TextureSlice : std::uint8_t {
  kNone = 0,

  kBackdropTopLeft,
  kBackdropTop,
  kBackdropTopRight,
  kBackdropLeft,
  kBackdropRight,
  kBackdropBottomLeft,
  kBackdropBottom,
  kBackdropBottomRight,
};

struct UiAnchor {
  std::string point;
  std::string relative_to;
  bool relative_to_explicit{false};
  std::string relative_point;
  float x{0.0f};
  float y{0.0f};
  std::uint32_t flags{0};
};

struct ScriptHandler {
  std::string event;
  std::string body;
  std::string function;

  std::shared_ptr<std::uint8_t> cache_key{
      std::make_shared<std::uint8_t>(0)};

  int declaring_taint_source{0};
};

struct UiPathControlPoint {
  std::string name;
  std::string inherits;
  std::string parent_key;
  std::optional<float> offset_x;
  std::optional<float> offset_y;
};

struct UiAnimation {
  std::string type;
  std::string name;

  std::optional<float> duration;
  std::optional<float> start_delay;
  std::optional<float> end_delay;
  std::optional<int> order;
  std::string smoothing;
  std::optional<float> max_framerate;
  std::string parent_key;
  std::string inherits;

  std::optional<float> change;
  std::optional<float> from_alpha;
  std::optional<float> to_alpha;

  std::optional<float> from_scale_x;
  std::optional<float> from_scale_y;
  std::optional<float> to_scale_x;
  std::optional<float> to_scale_y;
  std::optional<float> stock_scale_x;
  std::optional<float> stock_scale_y;

  std::optional<float> offset_x;
  std::optional<float> offset_y;

  std::optional<float> degrees;
  std::optional<float> radians;

  std::string origin_point;
  std::optional<float> origin_x;
  std::optional<float> origin_y;
  std::vector<UiPathControlPoint> control_points;

  std::optional<std::string> curve_type;
  std::vector<ScriptHandler> script_handlers;
};

struct UiAnimationGroup {
  std::string name;
  std::string inherits;
  std::string parent_key;
  std::string looping;
  float initial_offset_x{0.0F};
  float initial_offset_y{0.0F};
  std::vector<ScriptHandler> script_handlers;
  std::vector<UiAnimation> animations;
};

enum class TextureGradientOrientation : std::uint8_t {
  kHorizontal = 0,
  kVertical = 1,
};

struct TextureGradientColor {
  float r{0.0f};
  float g{0.0f};
  float b{0.0f};
  float a{0.0f};
};

struct UiColor {
  float r{1.0f};
  float g{1.0f};
  float b{1.0f};
  float a{1.0f};
};

inline float BackdropCtorDefaultEdgeSizePixels() {
  constexpr float kBackdropCtorDefaultNormalizedEdgeSize = 0.025F;
  return openwow::ui::RemoveCachedUiHorizontalStretch(
      openwow::ui::GetCachedUiAspectScaleKx() * 1024.0F *
      kBackdropCtorDefaultNormalizedEdgeSize);
}

struct BackdropSpec {
  std::string bg_file;
  std::string edge_file;
  bool tile{false};
  float inset_left{0.0F};
  float inset_right{0.0F};
  float inset_top{0.0F};
  float inset_bottom{0.0F};
  float tile_size{0.0F};
  float edge_size{BackdropCtorDefaultEdgeSizePixels()};
  std::string alpha_mode;
  float bg_color_r{1.0F};
  float bg_color_g{1.0F};
  float bg_color_b{1.0F};
  float bg_color_a{1.0F};
  bool has_bg_color{false};
  float border_color_r{1.0F};
  float border_color_g{1.0F};
  float border_color_b{1.0F};
  float border_color_a{1.0F};
  bool has_border_color{false};
};

struct TextureGradient {
  bool enabled{false};
  TextureGradientOrientation orientation{TextureGradientOrientation::kHorizontal};
  TextureGradientColor min_color;
  TextureGradientColor max_color;

  [[nodiscard]] std::array<TextureGradientColor, 4> ToUiRendererCornerColors() const {
    std::array<TextureGradientColor, 4> colors{};
    if (orientation == TextureGradientOrientation::kHorizontal) {
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

struct UiTextureCoordPoint {
  float u{0.0f};
  float v{0.0f};
};

struct UiTextureCoordQuad {
  UiTextureCoordPoint upper_left{0.0f, 0.0f};
  UiTextureCoordPoint lower_left{0.0f, 1.0f};
  UiTextureCoordPoint upper_right{1.0f, 0.0f};
  UiTextureCoordPoint lower_right{1.0f, 1.0f};

  [[nodiscard]] static constexpr UiTextureCoordQuad FromRect(float left, float right,
                                                             float top, float bottom) noexcept {
    UiTextureCoordQuad quad;
    quad.upper_left = {left, top};
    quad.lower_left = {left, bottom};
    quad.upper_right = {right, top};
    quad.lower_right = {right, bottom};
    return quad;
  }

  [[nodiscard]] constexpr std::array<UiTextureCoordPoint, 4> ToUiRendererOrder() const noexcept {
    return {upper_left, upper_right, lower_right, lower_left};
  }

  [[nodiscard]] constexpr bool IsAxisAlignedRect() const noexcept {
    return upper_left.u == lower_left.u && upper_right.u == lower_right.u &&
           upper_left.v == upper_right.v && lower_left.v == lower_right.v;
  }
};

struct UiFrame {

  enum class RuntimeKind : std::uint8_t {
    Unknown,
    Other,
    Texture,
    FontString,
    EditBox,
    QuestPoiFrame,
    StatusBar,
    Slider,
    ScrollingMessageFrame,
    Cooldown,
    Minimap,
    Model,
    PlayerModel,
    DressUpModel,
    TabardModel,
    MessageFrame,
    SimpleHtml,
    ColorSelect,
    MovieFrame,
    Line,
  };

  enum class RegionRole : std::uint8_t {
    Normal,
    ButtonText,
    EditBoxText,
    MessageFontDefinition,

    EditBoxCaret,
    EditBoxHighlight,
  };

  using TextureRole = openwow::ui::framexml::TextureRole;

  struct InitialAttribute {
    std::string name;
    std::string type;
    std::string value;
  };

  std::string kind;

  std::shared_ptr<const openwow::ui::xml::XMLNode> authored_xml;
  RuntimeKind runtime_kind{RuntimeKind::Unknown};

  bool runtime_state_initialized{false};
  bool runtime_draw_layer_enabled{true};
  bool runtime_uses_mouse{false};
  bool runtime_uses_mouse_wheel{false};
  bool runtime_uses_keyboard{false};
  float runtime_horizontal_scroll{0.0F};
  float runtime_vertical_scroll{0.0F};

  std::string name;

  std::string lua_name;
  bool publish_to_lua{true};

  [[nodiscard]] std::string_view LuaName() const noexcept {
    if (!publish_to_lua) {
      return {};
    }
    return lua_name.empty() ? std::string_view{name} : std::string_view{lua_name};
  }
  RegionRole region_role{RegionRole::Normal};
  TextureRole texture_role{TextureRole::Normal};
  bool top_level{false};
  bool toplevel{false};
  bool toplevel_explicit{false};
  int id{0};
  bool has_id{false};
  std::string parent;
  std::vector<std::string> parent_keys;
  std::string inherits;
  std::string file;

  std::string quest_poi_fill_texture;
  std::string quest_poi_border_texture;

  bool password{false};
  int max_letters{-1};
  std::optional<bool> auto_focus;

  float text_inset_left{0.0F};
  float text_inset_right{0.0F};
  float text_inset_top{0.0F};
  float text_inset_bottom{0.0F};
  bool has_text_insets{false};

  std::string font_style;
  std::string font_reference;
  bool has_font_height{false};
  std::string font_outline;
  std::optional<bool> font_monochrome;
  std::string justify_h;
  std::string justify_v;
  std::optional<bool> word_wrap;
  std::optional<bool> non_space_wrap;
  std::optional<bool> indented_word_wrap;
  std::string text;
  bool has_text_spacing{false};
  float text_spacing_stored{0.0f};
  float text_height_stored{0.0f};
  bool has_text_color{false};
  bool has_text_shadow{false};
  float text_shadow_r{0.0f};
  float text_shadow_g{0.0f};
  float text_shadow_b{0.0f};
  float text_shadow_a{1.0f};
  float text_shadow_x{0.0f};
  float text_shadow_y{0.0f};
  int max_lines{0};

  std::string message_font_style;
  std::optional<bool> message_fading;
  std::optional<float> message_display_duration;
  std::optional<float> message_fade_duration;
  std::optional<int> message_max_lines;
  std::string message_insert_mode;
  std::string alpha_mode;
  std::string draw_layer;
  int draw_sublevel{0};
  std::string frame_strata;
  int frame_level{0};
  bool has_frame_level{false};
  float depth{0.0F};
  std::optional<float> width;
  std::optional<float> height;

  bool font_intrinsic_width{false};
  bool font_intrinsic_height{false};

  std::optional<float> texture_natural_width;
  std::optional<float> texture_natural_height;

  std::string texture_natural_size_path;

  std::optional<float> model_natural_width;
  std::optional<float> model_natural_height;

  std::string model_natural_size_path;

  std::optional<float> rel_width;
  std::optional<float> rel_height;

  float layout_offset_x{0.0F};
  float layout_offset_y{0.0F};
  bool visible{true};
  bool visibility_explicit{false};
  bool virtual_template{false};

  bool scroll_child_content{false};
  std::size_t scroll_child_membership_count{0u};
  bool protected_frame{false};
  bool protected_explicit{false};

  std::optional<bool> movable;
  std::optional<bool> resizable;
  bool set_all_points{false};
  bool set_all_points_explicit{false};
  std::optional<bool> clamped_to_screen;

  std::optional<bool> cooldown_reverse;
  std::optional<bool> cooldown_draw_edge;
  float clamp_rect_inset_left{0.0F};
  float clamp_rect_inset_right{0.0F};
  float clamp_rect_inset_top{0.0F};
  float clamp_rect_inset_bottom{0.0F};

  float hit_rect_inset_left{0.0F};
  float hit_rect_inset_right{0.0F};
  float hit_rect_inset_top{0.0F};
  float hit_rect_inset_bottom{0.0F};
  bool has_hit_rect_insets{false};
  bool user_placed{false};
  bool dont_save_position{false};
  float tex_left{0.0F};
  float tex_right{1.0F};
  float tex_top{0.0F};
  float tex_bottom{1.0F};
  UiTextureCoordQuad tex_coords{};

  bool has_tex_coords{false};
  float color_r{1.0F};
  float color_g{1.0F};
  float color_b{1.0F};
  float color_a{1.0F};

  bool has_vertex_color{false};

  std::optional<float> texture_alpha;
  std::string button_normal_font_style;
  std::string button_disabled_font_style;
  std::string button_highlight_font_style;
  std::optional<UiColor> button_normal_color;
  std::optional<UiColor> button_disabled_color;
  std::optional<UiColor> button_highlight_color;
  std::optional<openwow::ui::widgets::StatusBarDefinition> status_bar;
  std::string color_wheel_texture_file;
  std::string color_wheel_thumb_texture_file;
  std::string color_value_texture_file;
  std::string color_value_thumb_texture_file;
  TextureGradient gradient;
  float fog_r{0.0F};
  float fog_g{0.0F};
  float fog_b{0.0F};
  bool has_fog_color{false};
  float fog_near{0.0F};
  bool has_fog_near{false};
  float fog_far{0.0F};
  bool has_fog_far{false};
  float glow{0.0F};
  bool has_glow{false};
  float model_scale{1.0F};
  bool has_model_scale{false};
  int model_sequence{0};
  bool has_model_sequence{false};
  int model_camera{0};
  bool has_model_camera{false};
  std::uint32_t model_sequence_time_ms{0};
  bool has_model_sequence_time{false};
  float model_x{0.0F};
  float model_y{0.0F};
  float model_z{0.0F};
  bool has_model_position{false};
  float model_facing_rad{0.0F};
  bool has_model_facing{false};

  bool tile_x{false};
  bool tile_y{false};

  bool tile_x_explicit{false};
  bool tile_y_explicit{false};
  int tile_size_x{0};
  int tile_size_y{0};

  TextureSlice slice{TextureSlice::kNone};
  int slice_edge_size_px{0};
  float scale{1.0F};
  std::vector<UiAnchor> anchors;
  std::vector<InitialAttribute> initial_attributes;

  std::vector<ScriptHandler> script_handlers;

  std::vector<UiAnimationGroup> animation_groups;

  bool enable_mouse{false};
  bool enable_mouse_explicit{false};

  bool enable_keyboard{false};
  bool enable_keyboard_explicit{false};

  std::optional<BackdropSpec> backdrop;
};

[[nodiscard]] inline UiFrame::RuntimeKind DeriveUiFrameRuntimeKind(
    const std::string& kind) noexcept {
  using Kind = UiFrame::RuntimeKind;
  switch (openwow::ui::widgets::ScriptObjectTypeFromName(kind)) {
    case openwow::ui::widgets::ScriptObjectType::Texture:
      return Kind::Texture;
    case openwow::ui::widgets::ScriptObjectType::FontString:
      return Kind::FontString;
    case openwow::ui::widgets::ScriptObjectType::Line:
      return Kind::Line;
    case openwow::ui::widgets::ScriptObjectType::EditBox:
      return Kind::EditBox;
    case openwow::ui::widgets::ScriptObjectType::StatusBar:
      return Kind::StatusBar;
    case openwow::ui::widgets::ScriptObjectType::Slider:
      return Kind::Slider;
    case openwow::ui::widgets::ScriptObjectType::ScrollingMessageFrame:
      return Kind::ScrollingMessageFrame;
    case openwow::ui::widgets::ScriptObjectType::Cooldown:
      return Kind::Cooldown;
    case openwow::ui::widgets::ScriptObjectType::Minimap:
      return Kind::Minimap;
    case openwow::ui::widgets::ScriptObjectType::Model:
      return Kind::Model;
    case openwow::ui::widgets::ScriptObjectType::PlayerModel:
      return Kind::PlayerModel;
    case openwow::ui::widgets::ScriptObjectType::DressUpModel:
      return Kind::DressUpModel;
    case openwow::ui::widgets::ScriptObjectType::TabardModel:
      return Kind::TabardModel;
    case openwow::ui::widgets::ScriptObjectType::MessageFrame:
      return Kind::MessageFrame;
    case openwow::ui::widgets::ScriptObjectType::SimpleHTML:
      return Kind::SimpleHtml;
    case openwow::ui::widgets::ScriptObjectType::ColorSelect:
      return Kind::ColorSelect;
    case openwow::ui::widgets::ScriptObjectType::MovieFrame:
      return Kind::MovieFrame;
    case openwow::ui::widgets::ScriptObjectType::QuestPOIFrame:
      return Kind::QuestPoiFrame;
    default:
      return Kind::Other;
  }
}

inline void StampXmlScriptProvenance(UiFrame& frame, const int source) {
  for (auto& handler : frame.script_handlers) {
    handler.declaring_taint_source = source;
  }
  for (auto& group : frame.animation_groups) {
    for (auto& handler : group.script_handlers) {
      handler.declaring_taint_source = source;
    }
    for (auto& animation : group.animations) {
      for (auto& handler : animation.script_handlers) {
        handler.declaring_taint_source = source;
      }
    }
  }
}

[[nodiscard]] inline bool StockConstructorEnablesMouse(
    const std::string_view kind) noexcept {
  using openwow::ui::widgets::ScriptObjectType;
  switch (openwow::ui::widgets::ScriptObjectTypeFromName(kind)) {
    case ScriptObjectType::Button:
    case ScriptObjectType::CheckButton:
    case ScriptObjectType::ColorSelect:
    case ScriptObjectType::EditBox:
    case ScriptObjectType::Slider:
    case ScriptObjectType::Minimap:
    case ScriptObjectType::WorldFrame:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] inline bool IsSimpleModelRuntimeKind(
    const UiFrame::RuntimeKind kind) noexcept {
  return kind == UiFrame::RuntimeKind::Model ||
         kind == UiFrame::RuntimeKind::PlayerModel ||
         kind == UiFrame::RuntimeKind::DressUpModel ||
         kind == UiFrame::RuntimeKind::TabardModel;
}

[[nodiscard]] inline bool IsDefaultTextureCoordinateQuad(
    const UiTextureCoordQuad& quad) noexcept {
  return quad.upper_left.u == 0.0F && quad.upper_left.v == 0.0F &&
         quad.lower_left.u == 0.0F && quad.lower_left.v == 1.0F &&
         quad.upper_right.u == 1.0F && quad.upper_right.v == 0.0F &&
         quad.lower_right.u == 1.0F && quad.lower_right.v == 1.0F;
}

[[nodiscard]] inline bool TextureCoordinatesWereSpecified(const UiFrame& frame) noexcept {
  return frame.has_tex_coords || frame.tex_left != 0.0F || frame.tex_right != 1.0F ||
         frame.tex_top != 0.0F || frame.tex_bottom != 1.0F ||
         !IsDefaultTextureCoordinateQuad(frame.tex_coords);
}

[[nodiscard]] inline UiTextureCoordQuad EffectiveTextureCoordinates(
    const UiFrame& frame) noexcept {
  if (frame.has_tex_coords || !IsDefaultTextureCoordinateQuad(frame.tex_coords)) {
    return frame.tex_coords;
  }
  return UiTextureCoordQuad::FromRect(frame.tex_left, frame.tex_right,
                                      frame.tex_top, frame.tex_bottom);
}

[[nodiscard]] inline bool TextureColorWasSpecified(const UiFrame& frame) noexcept {
  return frame.has_vertex_color || frame.color_r != 1.0F || frame.color_g != 1.0F ||
         frame.color_b != 1.0F || frame.color_a != 1.0F;
}

}
