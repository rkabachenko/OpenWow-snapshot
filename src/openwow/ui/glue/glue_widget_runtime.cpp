#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/ui/framexml/framexml_parser_detail.h"
#include "openwow/ui/ui_coordinate_space.h"
#include "openwow/ui/ui_paint_order.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace openwow::ui::glue {

using openwow::text::ToLowerAscii;
using openwow::text::Trim;

namespace {

constexpr std::string_view kAnonymousGlueWidgetPrefix = "__ow_anonymous_glue_frame_";

struct QuantizedBackdropColor {
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};
};

float NormalizeBackdropByte(int value) {
  return static_cast<float>(value) / 255.0F;
}

float QuantizeBackdropComponent(float value) {
  const float clamped = std::clamp(value, 0.0F, 1.0F);
  const int quantized =
      std::clamp(static_cast<int>(clamped * 255.0F + 0.5F), 0, 255);
  return NormalizeBackdropByte(quantized);
}

std::uint8_t QuantizeAlphaByteTruncated(float value) {
  const float clamped = std::clamp(value, 0.0F, 1.0F);
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(clamped * 255.0F), 0, 255));
}

float NormalizeAlphaByte(std::uint8_t value) {
  return static_cast<float>(value) / 255.0F;
}

QuantizedBackdropColor QuantizeBackdropColor(float r, float g, float b,
                                             float a) {
  QuantizedBackdropColor color;
  color.red = QuantizeBackdropComponent(r);
  color.green = QuantizeBackdropComponent(g);
  color.blue = QuantizeBackdropComponent(b);
  color.alpha = QuantizeBackdropComponent(a);
  return color;
}

void QuantizeBackdropSpecColors(
    openwow::ui::framexml::detail::BackdropSpec* spec) {
  if (spec == nullptr) {
    return;
  }

  spec->bg_color_r = QuantizeBackdropComponent(spec->bg_color_r);
  spec->bg_color_g = QuantizeBackdropComponent(spec->bg_color_g);
  spec->bg_color_b = QuantizeBackdropComponent(spec->bg_color_b);
  spec->bg_color_a = QuantizeBackdropComponent(spec->bg_color_a);
  spec->border_color_r = QuantizeBackdropComponent(spec->border_color_r);
  spec->border_color_g = QuantizeBackdropComponent(spec->border_color_g);
  spec->border_color_b = QuantizeBackdropComponent(spec->border_color_b);
  spec->border_color_a = QuantizeBackdropComponent(spec->border_color_a);
}

bool IsDefaultTextureCoordQuad(const openwow::ui::framexml::UiTextureCoordQuad& quad) {
  return quad.upper_left.u == 0.0F && quad.upper_left.v == 0.0F &&
         quad.lower_left.u == 0.0F && quad.lower_left.v == 1.0F &&
         quad.upper_right.u == 1.0F && quad.upper_right.v == 0.0F &&
         quad.lower_right.u == 1.0F && quad.lower_right.v == 1.0F;
}

bool ChildNameMatchesSuffix(const std::string& child,
                            const std::string& parent,
                            std::string_view suffix) {
  return child.size() == parent.size() + suffix.size() &&
         child.compare(0, parent.size(), parent) == 0 &&
         child.compare(parent.size(), suffix.size(), suffix) == 0;
}

struct GlueWidgetHitRect {
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};
};

GlueWidgetHitRect ResolveGlueWidgetHitRect(
    const GlueWidgetState& widget,
    const openwow::ui::DevicePixelsPerUiUnit scale) {
  const auto inset = [&](const float ui_units) {
    return widget.has_hit_rect_insets
               ? static_cast<int>(
                     openwow::ui::UiUnitsToDevicePixels(ui_units, scale))
               : 0;
  };
  return GlueWidgetHitRect{
      .left = widget.x + inset(widget.hit_rect_inset_left),
      .top = widget.y + inset(widget.hit_rect_inset_top),
      .right = widget.x + widget.width - inset(widget.hit_rect_inset_right),
      .bottom = widget.y + widget.height - inset(widget.hit_rect_inset_bottom),
  };
}

}

std::string GlueEditBoxTextRegionKey(const std::string_view owner_name) {
  std::string key(owner_name);
  key += ".__EditBoxText";
  return key;
}

std::string GlueWidgetRuntime::RegisterAnonymousWidget(GlueWidgetState widget) {
  const std::string key = AllocateUniqueWidgetKey({});
  widget.name = key;
  widget.lua_name.clear();
  widget.publish_to_lua = false;
  RegisterWidget(std::move(widget));
  return key;
}

std::string GlueWidgetRuntime::AllocateUniqueWidgetKey(
    const std::string_view preferred_name) {
  if (!preferred_name.empty() &&
      widgets_.find(std::string(preferred_name)) == widgets_.end()) {
    return std::string(preferred_name);
  }

  std::string key;
  do {
    key = std::string(kAnonymousGlueWidgetPrefix) +
          std::to_string(next_anonymous_widget_id_++);
  } while (widgets_.find(key) != widgets_.end());
  return key;
}

bool GlueWidgetRuntime::IsAnonymousWidgetKey(std::string_view name) {
  return name.starts_with(kAnonymousGlueWidgetPrefix);
}

void GlueWidgetRuntime::RegisterWidget(GlueWidgetState widget) {
  if (widget.name.empty()) {
    return;
  }

  const std::string name = widget.name;
  const auto previous = widgets_.find(name);
  const bool is_new_widget = previous == widgets_.end();
  const bool was_font_string =
      !is_new_widget &&
      openwow::text::EqualsIgnoreCaseAscii(previous->second.kind, "FontString");
  const std::string old_parent = is_new_widget ? std::string() : previous->second.parent;
  const std::string old_inherits = is_new_widget ? std::string() : previous->second.inherits;
  const auto old_region_role =
      is_new_widget
          ? openwow::ui::framexml::UiFrame::RegionRole::Normal
          : previous->second.region_role;
  auto [widget_it, _] = widgets_.insert_or_assign(name, std::move(widget));
  if (is_new_widget) {
    widget_registration_order_.push_back(name);
    widget_names_lower_.insert(ToLowerAscii(name));
  }
  auto& stored = widget_it->second;
  if (old_region_role != openwow::ui::framexml::UiFrame::RegionRole::Normal &&
      !old_parent.empty()) {
    if (const auto binding = owned_text_region_by_widget_.find(old_parent);
        binding != owned_text_region_by_widget_.end() &&
        binding->second == name) {
      owned_text_region_by_widget_.erase(binding);
    }
  }
  if (stored.region_role !=
          openwow::ui::framexml::UiFrame::RegionRole::Normal &&
      !stored.parent.empty()) {
    owned_text_region_by_widget_.insert_or_assign(stored.parent, name);
  }
  ReindexVisibilityRelationships(name, old_parent, old_inherits);
  if (openwow::text::EqualsIgnoreCaseAscii(stored.kind, "FontString")) {
    MarkFontStringMetricsDirty(name);
  } else if (was_font_string) {
    ForgetFontStringMetrics(name);
  }
  const bool rect_is_default =
      stored.tex_left == 0.0F && stored.tex_right == 1.0F &&
      stored.tex_top == 0.0F && stored.tex_bottom == 1.0F;
  if (IsDefaultTextureCoordQuad(stored.tex_coords) && !rect_is_default) {
    stored.tex_coords = openwow::ui::framexml::UiTextureCoordQuad::FromRect(
        stored.tex_left, stored.tex_right, stored.tex_top, stored.tex_bottom);
  }
  stored.alpha_byte = QuantizeAlphaByteTruncated(stored.alpha);
  stored.alpha = NormalizeAlphaByte(stored.alpha_byte);

  if (!stored.virtual_template) {
    const auto kind_lower = ToLowerAscii(stored.kind);
    if (kind_lower == "statusbar") {
      auto* props = GetProps(stored.name);
      if (props != nullptr && !props->status_bar.has_value()) {
        auto& status_bar = props->status_bar.emplace();
        if (stored.status_bar.has_value()) {
          const auto& definition = *stored.status_bar;
          if (definition.minimum.has_value() &&
              definition.maximum.has_value() &&
              openwow::ui::widgets::ValidateStatusBarRange(
                  *definition.minimum, *definition.maximum) ==
                  openwow::ui::widgets::StatusBarRangeError::None) {
            (void)status_bar.SetRange(*definition.minimum,
                                      *definition.maximum);
            if (definition.default_value.has_value()) {
              (void)status_bar.SetValue(*definition.default_value);
            }
          }
        }
      }
    }
    if (kind_lower == "model" || kind_lower == "modelffx") {
      const bool had_props = widget_props_.find(stored.name) != widget_props_.end();
      if (auto* props = GetProps(stored.name); props != nullptr) {
        if (!had_props) {
          props->glow = (kind_lower == "modelffx") ? 0.3F : 0.0F;
        }
      }
      ApplyModelWidgetStateToProps(stored);
    }
    if (kind_lower == "modelffx") {
      auto model_it = model_ffx_widgets_.find(stored.name);
      if (model_it == model_ffx_widgets_.end()) {
        model_it = model_ffx_widgets_
                       .insert_or_assign(stored.name, std::make_shared<GlueModelFFXWidget>(stored.name))
                       .first;
      }
      if (!stored.model_file.empty()) {
        stored.model_file = model_it->second->SetModelFile(stored.model_file);
      }
    }
  }
  if (is_new_widget) {
    const auto kind_lower = ToLowerAscii(stored.kind);
    const bool inherently_mouse_enabled =
        kind_lower == "button" || kind_lower == "checkbutton" ||
        kind_lower == "editbox" || kind_lower == "slider";
    SetMouseEnabled(stored.name, stored.mouse_enabled || inherently_mouse_enabled);
  }

  auto it = layout_frames_by_name_.find(stored.name);
  if (it == layout_frames_by_name_.end()) {
    openwow::ui::framexml::UiFrame frame;
    frame.kind = stored.kind;
    frame.name = stored.name;
    frame.lua_name = stored.lua_name;
    frame.publish_to_lua = stored.publish_to_lua;
    frame.region_role = stored.region_role;
    frame.parent = stored.parent;
    frame.inherits = stored.inherits;
    frame.file = stored.texture_file.empty() ? stored.model_file : stored.texture_file;
    frame.font_style = stored.font_style;
    frame.justify_h = stored.justify_h;
    frame.justify_v = stored.justify_v;
    frame.word_wrap = stored.word_wrap;
    frame.non_space_wrap = stored.non_space_wrap;
    frame.indented_word_wrap = stored.indented_word_wrap;
    frame.text = stored.text;
    frame.password = stored.password;
    frame.max_letters = stored.max_letters;
    frame.auto_focus = stored.auto_focus;
    frame.text_inset_left = stored.text_inset_left;
    frame.text_inset_right = stored.text_inset_right;
    frame.text_inset_top = stored.text_inset_top;
    frame.text_inset_bottom = stored.text_inset_bottom;
    frame.has_text_insets = stored.has_text_insets;
    frame.alpha_mode = stored.alpha_mode;
    frame.draw_layer = stored.draw_layer;
    frame.draw_sublevel = stored.draw_sublevel;
    frame.frame_strata = stored.frame_strata;
    frame.frame_level = stored.frame_level;
    frame.protected_frame = stored.protected_frame;
    frame.enable_mouse = stored.mouse_enabled;
    if (stored.width > 0) frame.width = stored.width;
    if (stored.height > 0) frame.height = stored.height;
    frame.color_r = stored.color_r;
    frame.color_g = stored.color_g;
    frame.color_b = stored.color_b;
    frame.color_a = stored.color_a;
    frame.button_normal_font_style = stored.button_normal_font_style;
    frame.button_disabled_font_style = stored.button_disabled_font_style;
    frame.button_highlight_font_style = stored.button_highlight_font_style;
    frame.button_normal_color = stored.button_normal_color;
    frame.button_disabled_color = stored.button_disabled_color;
    frame.button_highlight_color = stored.button_highlight_color;
    frame.status_bar = stored.status_bar;
    frame.color_wheel_texture_file = stored.color_wheel_texture_file;
    frame.color_wheel_thumb_texture_file = stored.color_wheel_thumb_texture_file;
    frame.color_value_texture_file = stored.color_value_texture_file;
    frame.color_value_thumb_texture_file = stored.color_value_thumb_texture_file;
    frame.fog_r = stored.fog_r;
    frame.fog_g = stored.fog_g;
    frame.fog_b = stored.fog_b;
    frame.has_fog_color = stored.has_fog_color;
    frame.fog_near = stored.fog_near;
    frame.has_fog_near = stored.has_fog_near;
    frame.fog_far = stored.fog_far;
    frame.has_fog_far = stored.has_fog_far;
    frame.glow = stored.glow;
    frame.has_glow = stored.has_glow;
    frame.model_scale = stored.model_scale;
    frame.has_model_scale = stored.has_model_scale;
    frame.model_sequence = stored.model_sequence;
    frame.has_model_sequence = stored.has_model_sequence;
    frame.model_camera = stored.model_camera;
    frame.has_model_camera = stored.has_model_camera;
    frame.model_sequence_time_ms = stored.model_sequence_time_ms;
    frame.has_model_sequence_time = stored.has_model_sequence_time;
    frame.model_x = stored.model_x;
    frame.model_y = stored.model_y;
    frame.model_z = stored.model_z;
    frame.has_model_position = stored.has_model_position;
    frame.model_facing_rad = stored.model_facing_rad;
    frame.has_model_facing = stored.has_model_facing;
    frame.tile_x = stored.tile_x;
    frame.tile_y = stored.tile_y;
    frame.tile_size_x = stored.tile_size_x;
    frame.tile_size_y = stored.tile_size_y;
    frame.slice = stored.slice;
    frame.slice_edge_size_px = stored.slice_edge_size_px;
    frame.visible = stored.visible;
    frame.virtual_template = stored.virtual_template;
    frame.scroll_child_content = stored.scroll_child_content;
    frame.hit_rect_inset_left = stored.hit_rect_inset_left;
    frame.hit_rect_inset_right = stored.hit_rect_inset_right;
    frame.hit_rect_inset_top = stored.hit_rect_inset_top;
    frame.hit_rect_inset_bottom = stored.hit_rect_inset_bottom;
    frame.has_hit_rect_insets = stored.has_hit_rect_insets;
    frame.backdrop = stored.backdrop;

    if (const auto* props = FindProps(stored.name); props != nullptr) {
      frame.scale = props->scale;
    }
    layout_frames_by_name_.insert_or_assign(stored.name, std::move(frame));
  }

  const bool needs_editbox_text_region =
      is_new_widget && !stored.virtual_template &&
      ToLowerAscii(stored.kind) == "editbox" &&
      owned_text_region_by_widget_.find(stored.name) ==
          owned_text_region_by_widget_.end();
  const std::string editbox_name =
      needs_editbox_text_region ? stored.name : std::string();
  const std::string editbox_text =
      needs_editbox_text_region ? stored.text : std::string();
  const std::string editbox_font =
      needs_editbox_text_region ? stored.font_style : std::string();
  const std::string editbox_strata =
      needs_editbox_text_region ? stored.frame_strata : std::string();
  const int editbox_level = needs_editbox_text_region ? stored.frame_level : 0;

  const bool editbox_visible = needs_editbox_text_region;
  const bool has_text_insets =
      needs_editbox_text_region && stored.has_text_insets;
  const float inset_left =
      needs_editbox_text_region ? stored.text_inset_left : 0.0F;
  const float inset_right =
      needs_editbox_text_region ? stored.text_inset_right : 0.0F;
  const float inset_top =
      needs_editbox_text_region ? stored.text_inset_top : 0.0F;
  const float inset_bottom =
      needs_editbox_text_region ? stored.text_inset_bottom : 0.0F;
  deferred_hit_test_refresh_ = true;
  ++visibility_revision_;
  MarkVisibleWidgetsDirty();

  if (needs_editbox_text_region) {
    const std::string text_key = GlueEditBoxTextRegionKey(editbox_name);
    openwow::ui::framexml::UiFrame text_frame;
    text_frame.kind = "FontString";
    text_frame.name = text_key;
    text_frame.publish_to_lua = false;
    text_frame.region_role =
        openwow::ui::framexml::UiFrame::RegionRole::EditBoxText;
    text_frame.parent = editbox_name;
    text_frame.font_style = editbox_font;
    text_frame.draw_layer = "OVERLAY";
    text_frame.frame_strata = editbox_strata;
    text_frame.frame_level = editbox_level;
    text_frame.visible = editbox_visible;
    text_frame.scroll_child_content = stored.scroll_child_content;
    if (has_text_insets) {
      text_frame.anchors = {
          openwow::ui::framexml::UiAnchor{
              .point = "TOPLEFT",
              .relative_to = editbox_name,
              .relative_point = "TOPLEFT",
              .x = static_cast<float>(inset_left),
              .y = static_cast<float>(-inset_top),
          },
          openwow::ui::framexml::UiAnchor{
              .point = "BOTTOMRIGHT",
              .relative_to = editbox_name,
              .relative_point = "BOTTOMRIGHT",
              .x = static_cast<float>(-inset_right),
              .y = static_cast<float>(inset_bottom),
          },
      };
    } else {
      text_frame.set_all_points = true;
    }
    layout_frames_by_name_.insert_or_assign(text_key, std::move(text_frame));
    RegisterWidget({
        .name = text_key,
        .kind = "FontString",
        .parent = editbox_name,
        .font_style = editbox_font,
        .draw_layer = "OVERLAY",
        .frame_strata = editbox_strata,
        .frame_level = editbox_level,
        .visible = editbox_visible,
        .scroll_child_content = stored.scroll_child_content,
        .publish_to_lua = false,
        .region_role =
            openwow::ui::framexml::UiFrame::RegionRole::EditBoxText,
    });
    if (!editbox_text.empty()) {
      SetText(editbox_name, editbox_text);
    }
  }
}

std::shared_ptr<GlueModelFFXWidget> GlueWidgetRuntime::ResolveModelFFXWidget(const std::string& name) const {
  if (name.empty()) return nullptr;
  const auto it = model_ffx_widgets_.find(name);
  if (it == model_ffx_widgets_.end()) return nullptr;
  return it->second;
}

GlueWidgetRuntime::WidgetScriptProps* GlueWidgetRuntime::GetProps(const std::string& name) {
  if (name.empty()) {
    return nullptr;
  }
  return &widget_props_[name];
}

const GlueWidgetRuntime::WidgetScriptProps* GlueWidgetRuntime::FindProps(const std::string& name) const {
  if (name.empty()) {
    return nullptr;
  }
  const auto it = widget_props_.find(name);
  if (it == widget_props_.end()) {
    return nullptr;
  }
  return &it->second;
}

void GlueWidgetRuntime::ApplyModelWidgetStateToProps(const GlueWidgetState& stored) {
  const auto kind_lower = ToLowerAscii(stored.kind);
  if (kind_lower != "model" && kind_lower != "modelffx") {
    return;
  }
  auto* props = GetProps(stored.name);
  if (props == nullptr) {
    return;
  }

  if (stored.has_fog_color) {
    props->fog_r = stored.fog_r;
    props->fog_g = stored.fog_g;
    props->fog_b = stored.fog_b;
    props->fog_enabled = true;
  }
  if (stored.has_fog_near) {
    props->fog_near = stored.fog_near;
    props->fog_enabled = true;
  }
  if (stored.has_fog_far) {
    props->fog_far = stored.fog_far;
    props->fog_enabled = true;
  }
  if (kind_lower == "modelffx" && stored.has_glow) {
    props->glow = stored.glow;
  }
  if (stored.has_model_scale) {
    props->model_scale = stored.model_scale;
  }
  if (stored.has_model_sequence) {
    props->model_sequence = stored.model_sequence;
  }
  if (stored.has_model_camera) {
    props->model_camera = stored.model_camera;
  }
  if (stored.has_model_sequence_time) {
    props->model_sequence_time_override = true;
    props->model_sequence_time_ms = stored.model_sequence_time_ms;
  }
  if (stored.has_model_position) {
    props->model_x = stored.model_x;
    props->model_y = stored.model_y;
    props->model_z = stored.model_z;
  }
  if (stored.has_model_facing) {
    props->facing_rad = stored.model_facing_rad;
  }
}

void GlueWidgetRuntime::Show(const std::string& name) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.Show: unknown widget: " + name);
    return;
  }
  if (it->second.visible) {
    return;
  }
  it->second.visible = true;
  ++visibility_revision_;
  deferred_hit_test_refresh_ = true;
  MarkVisibleWidgetsDirty();
}

void GlueWidgetRuntime::Hide(const std::string& name) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.Hide: unknown widget: " + name);
    return;
  }
  if (!it->second.visible) {
    return;
  }
  it->second.visible = false;
  ++visibility_revision_;
  deferred_hit_test_refresh_ = true;
  MarkVisibleWidgetsDirty();
}

bool GlueWidgetRuntime::ButtonTextureStateAllowsVisible(
    const GlueWidgetState& widget) const {
  if (!openwow::text::EqualsIgnoreCaseAscii(widget.kind, "texture") ||
      widget.parent.empty()) {
    return true;
  }

  const auto parent_it = widgets_.find(widget.parent);
  if (parent_it == widgets_.end() ||
      (!openwow::text::EqualsIgnoreCaseAscii(parent_it->second.kind, "button") &&
       !openwow::text::EqualsIgnoreCaseAscii(parent_it->second.kind,
                                             "checkbutton"))) {
    return true;
  }

  const auto& parent = parent_it->second;
  const auto* props = FindProps(parent.name);
  const bool pushed =
      props != nullptr && openwow::text::EqualsIgnoreCaseAscii(
                              props->button_state, "pushed");
  const bool highlighted =
      props != nullptr && (props->hovered || props->highlight_locked);
  const bool checked = props != nullptr && props->checked;

  if (ChildNameMatchesSuffix(widget.name, parent.name, "NormalTexture")) {
    return parent.enabled && !pushed;
  }
  if (ChildNameMatchesSuffix(widget.name, parent.name, "PushedTexture")) {
    return parent.enabled && pushed;
  }
  if (ChildNameMatchesSuffix(widget.name, parent.name, "DisabledTexture")) {
    return !parent.enabled;
  }
  if (ChildNameMatchesSuffix(widget.name, parent.name, "HighlightTexture")) {
    return parent.enabled && highlighted;
  }
  if (ChildNameMatchesSuffix(widget.name, parent.name, "CheckedTexture")) {
    return parent.enabled && checked;
  }
  if (ChildNameMatchesSuffix(widget.name, parent.name,
                             "DisabledCheckedTexture")) {
    return !parent.enabled && checked;
  }
  return true;
}

bool GlueWidgetRuntime::IsVisible(const std::string& name) const {
  if (const auto override = lifecycle_visibility_overrides_.find(name);
      override != lifecycle_visibility_overrides_.end()) {
    return override->second;
  }
  return IsVisibleIgnoringLifecycleOverride(name);
}

bool GlueWidgetRuntime::IsVisibleIgnoringLifecycleOverride(
    const std::string& name) const {
  if (name.empty()) {
    return false;
  }

  auto it = widgets_.find(name);
  for (int depth = 0; depth < 64 && it != widgets_.end(); ++depth) {
    const auto& widget = it->second;
    if (!widget.visible) {
      return false;
    }
    if (!ButtonTextureStateAllowsVisible(widget)) {
      return false;
    }

    if (!widget.parent.empty()) {
      it = widgets_.find(widget.parent);
      continue;
    }

    if (!widget.inherits.empty()) {
      const auto templ_it = widgets_.find(widget.inherits);
      if (templ_it != widgets_.end() && !templ_it->second.virtual_template) {
        it = templ_it;
        continue;
      }
    }

    return true;
  }

  return false;
}

void GlueWidgetRuntime::SetLifecycleVisibilityOverride(
    const std::string& name, const bool visible) {
  if (!name.empty() && widgets_.find(name) != widgets_.end()) {
    lifecycle_visibility_overrides_.insert_or_assign(name, visible);
  }
}

void GlueWidgetRuntime::ClearLifecycleVisibilityOverride(
    const std::string& name) {
  lifecycle_visibility_overrides_.erase(name);
}

void GlueWidgetRuntime::ClearLifecycleVisibilityOverrides() {
  lifecycle_visibility_overrides_.clear();
}

std::vector<std::string> GlueWidgetRuntime::VisibilitySubtreeNames(
    const std::string& name) const {
  std::vector<std::string> result;
  if (name.empty() || widgets_.find(name) == widgets_.end()) {
    return result;
  }

  std::unordered_set<std::string> seen;
  seen.reserve(32);
  result.push_back(name);
  seen.insert(name);
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto append = [&](const auto& index) {
      const auto it = index.find(result[i]);
      if (it == index.end()) {
        return;
      }
      for (const auto& dependent : it->second) {
        if (seen.insert(dependent).second) {
          result.push_back(dependent);
        }
      }
    };
    append(children_by_parent_);
    append(inheritors_by_template_);
  }
  return result;
}

std::vector<std::string> GlueWidgetRuntime::ShownDescendantNames(
    const std::string& name) const {
  std::vector<std::string> result;
  if (name.empty() || widgets_.find(name) == widgets_.end()) {
    return result;
  }

  const auto root_children = children_by_parent_.find(name);
  if (root_children == children_by_parent_.end()) {
    return result;
  }

  std::vector<std::string> pending = root_children->second;
  std::unordered_set<std::string> seen;
  seen.reserve(pending.size() + 1);
  seen.insert(name);
  for (std::size_t index = 0; index < pending.size(); ++index) {
    const auto& child_name = pending[index];
    if (!seen.insert(child_name).second) {
      continue;
    }
    const auto child = widgets_.find(child_name);
    if (child == widgets_.end() || child->second.virtual_template ||
        !child->second.visible) {
      continue;
    }

    result.push_back(child_name);
    const auto grandchildren = children_by_parent_.find(child_name);
    if (grandchildren != children_by_parent_.end()) {
      pending.insert(pending.end(), grandchildren->second.begin(),
                     grandchildren->second.end());
    }
  }
  return result;
}

void GlueWidgetRuntime::MarkFontStringMetricsDirty(const std::string& name) {
  const auto widget = widgets_.find(name);
  if (widget == widgets_.end() ||
      !openwow::text::EqualsIgnoreCaseAscii(widget->second.kind, "FontString")) {
    return;
  }

  auto& state = font_string_metric_state_[name];
  ++state.revision;
  state.latest.valid = false;
  state.intrinsic.valid = false;
  if (dirty_font_string_metric_names_set_.insert(name).second) {
    dirty_font_string_metric_names_.push_back(name);
  }
}

void GlueWidgetRuntime::MarkFontStringMetricsDirtyInSubtree(
    const std::string& name) {
  if (name.empty()) {
    return;
  }
  std::vector<std::string> pending{name};
  std::unordered_set<std::string> seen;
  seen.reserve(32);
  for (std::size_t index = 0; index < pending.size(); ++index) {
    const auto current = pending[index];
    if (!seen.insert(current).second) {
      continue;
    }
    MarkFontStringMetricsDirty(current);
    const auto children = children_by_parent_.find(current);
    if (children != children_by_parent_.end()) {
      pending.insert(pending.end(), children->second.begin(),
                     children->second.end());
    }
  }
}

void GlueWidgetRuntime::MarkAllFontStringMetricsDirty() {
  for (auto& [name, state] : font_string_metric_state_) {
    ++state.revision;
    state.latest.valid = false;
    state.intrinsic.valid = false;
    if (dirty_font_string_metric_names_set_.insert(name).second) {
      dirty_font_string_metric_names_.push_back(name);
    }
  }
}

void GlueWidgetRuntime::ForgetFontStringMetrics(const std::string& name) {
  font_string_metric_state_.erase(name);
  dirty_font_string_metric_names_set_.erase(name);
  dirty_font_string_metric_names_.erase(
      std::remove(dirty_font_string_metric_names_.begin(),
                  dirty_font_string_metric_names_.end(), name),
      dirty_font_string_metric_names_.end());
}

std::vector<std::string>
GlueWidgetRuntime::ConsumeDirtyFontStringMetricNames() {
  auto names = std::move(dirty_font_string_metric_names_);
  dirty_font_string_metric_names_.clear();
  dirty_font_string_metric_names_set_.clear();
  return names;
}

std::optional<GlueTextExtent> GlueWidgetRuntime::GetCachedTextExtent(
    const std::string& name, const std::uint64_t request_key) const {
  const auto state = font_string_metric_state_.find(name);
  if (state == font_string_metric_state_.end()) {
    return std::nullopt;
  }
  const auto& cached = state->second.latest;
  if (!cached.valid || cached.revision != state->second.revision ||
      cached.request_key != request_key) {
    return std::nullopt;
  }
  return cached.extent;
}

std::optional<GlueTextExtent>
GlueWidgetRuntime::GetCachedIntrinsicTextExtent(
    const std::string& name) const {
  const auto state = font_string_metric_state_.find(name);
  if (state == font_string_metric_state_.end()) {
    return std::nullopt;
  }
  const auto& cached = state->second.intrinsic;
  if (!cached.valid || cached.revision != state->second.revision) {
    return std::nullopt;
  }
  return cached.extent;
}

void GlueWidgetRuntime::CacheTextExtent(
    const std::string& name, const std::uint64_t request_key,
    const GlueTextExtent extent,
    const bool intrinsic_layout, const bool laid_out_now) {
  auto state = font_string_metric_state_.find(name);
  if (state == font_string_metric_state_.end()) {
    return;
  }

  CachedTextExtent entry{
      .revision = state->second.revision,
      .request_key = request_key,
      .extent = extent,
      .valid = true,
  };
  state->second.latest = entry;
  if (intrinsic_layout) {
    state->second.intrinsic = entry;
  }
  if (laid_out_now) {
    ++state->second.layout_count;
  }
}

std::uint64_t GlueWidgetRuntime::TextLayoutCount(
    const std::string& name) const {
  const auto state = font_string_metric_state_.find(name);
  return state != font_string_metric_state_.end()
             ? state->second.layout_count
             : 0;
}

void GlueWidgetRuntime::ReindexVisibilityRelationships(
    const std::string& name, const std::string& old_parent,
    const std::string& old_inherits) {
  const auto remove = [&](auto& index, const std::string& key) {
    if (key.empty()) {
      return;
    }
    const auto it = index.find(key);
    if (it == index.end()) {
      return;
    }
    auto& values = it->second;
    values.erase(std::remove(values.begin(), values.end(), name), values.end());
    if (values.empty()) {
      index.erase(it);
    }
  };
  remove(children_by_parent_, old_parent);
  remove(inheritors_by_template_, old_inherits);

  const auto widget_it = widgets_.find(name);
  if (widget_it == widgets_.end()) {
    return;
  }
  const auto add = [&](auto& index, const std::string& key) {
    if (key.empty()) {
      return;
    }
    auto& values = index[key];
    if (std::find(values.begin(), values.end(), name) == values.end()) {
      values.push_back(name);
    }
  };
  add(children_by_parent_, widget_it->second.parent);
  add(inheritors_by_template_, widget_it->second.inherits);
}

bool GlueWidgetRuntime::IsShown(const std::string& name) const {

  if (name.empty()) return false;
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) return false;
  return it->second.visible;
}

bool GlueWidgetRuntime::IsVirtualTemplate(const std::string& name) const {
  if (name.empty()) {
    return false;
  }
  const auto it = widgets_.find(name);
  return it != widgets_.end() && it->second.virtual_template;
}

void GlueWidgetRuntime::SetId(const std::string& name, int id) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  it->second.id = id;
}

int GlueWidgetRuntime::GetId(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return 0;
  }
  return it->second.id;
}

std::optional<GlueWidgetState> GlueWidgetRuntime::GetWidget(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string GlueWidgetRuntime::NearestLuaName(
    const std::string& runtime_key) const {
  std::string current = runtime_key;
  for (int depth = 0; depth < 64 && !current.empty(); ++depth) {
    if (openwow::text::EqualsIgnoreCaseAscii(current, "UIParent")) {
      return "UIParent";
    }
    const auto widget = widgets_.find(current);
    if (widget == widgets_.end()) {
      return current;
    }
    if (!widget->second.LuaName().empty()) {
      return std::string(widget->second.LuaName());
    }
    current = widget->second.parent;
  }
  return {};
}

std::optional<GlueWidgetState> GlueWidgetRuntime::GetResolvedWidget(
    const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return std::nullopt;
  }
  GlueWidgetState resolved;
  ApplyAnimationTranslation(it->second, &resolved);
  if (resolved.region_role ==
          openwow::ui::framexml::UiFrame::RegionRole::ButtonText &&
      !resolved.parent.empty()) {
    const auto owner = widgets_.find(resolved.parent);
    if (owner != widgets_.end()) {
      const auto* props = FindProps(resolved.parent);
      const bool disabled =
          !owner->second.enabled ||
          (props != nullptr && openwow::text::EqualsIgnoreCaseAscii(
                                   props->button_state, "DISABLED"));
      if (disabled && owner->second.button_disabled_color.has_value()) {
        const auto& color = *owner->second.button_disabled_color;
        resolved.color_r = color.r;
        resolved.color_g = color.g;
        resolved.color_b = color.b;
        resolved.color_a = color.a;
        resolved.has_vertex_color = true;
      }
    }
  }
  return resolved;
}

GlueWidgetPresentation GlueWidgetRuntime::ResolveScrollPresentation(
    const GlueWidgetState& resolved_widget) const {
  GlueWidgetPresentation result{.widget = resolved_widget};
  if (!resolved_widget.scroll_child_content) {
    return result;
  }

  std::vector<GlueWidgetState> scroll_ancestors;
  std::string ancestor_name = resolved_widget.parent;
  for (std::size_t depth = 0; !ancestor_name.empty() && depth < 256; ++depth) {
    const auto ancestor = GetResolvedWidget(ancestor_name);
    if (!ancestor.has_value()) {
      break;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(ancestor->kind,
                                             "ScrollFrame")) {
      scroll_ancestors.push_back(*ancestor);
    }
    ancestor_name = ancestor->parent;
  }
  std::reverse(scroll_ancestors.begin(), scroll_ancestors.end());
  std::vector<openwow::ui::UiScrollClipNode> clip_nodes;
  clip_nodes.reserve(scroll_ancestors.size());
  for (const auto& scroll : scroll_ancestors) {
    clip_nodes.push_back({
        .viewport = {static_cast<float>(scroll.x),
                     static_cast<float>(scroll.y),
                     static_cast<float>(scroll.width),
                     static_cast<float>(scroll.height)},
        .horizontal_scroll_pixels =
            static_cast<float>(GetHorizontalScroll(scroll.name)),
        .vertical_scroll_pixels =
            static_cast<float>(GetVerticalScroll(scroll.name)),
    });
  }
  const auto presentation = openwow::ui::BuildUiScrollPresentation(clip_nodes);
  result.clipped_out = presentation.clipped_out;
  result.offset_x = static_cast<int>(std::lround(presentation.offset_x));
  result.offset_y = static_cast<int>(std::lround(presentation.offset_y));
  if (presentation.clip.has_value()) {
    result.clip = GlueWidgetClipRect{
        static_cast<int>(std::lround(presentation.clip->x)),
        static_cast<int>(std::lround(presentation.clip->y)),
        static_cast<int>(std::lround(presentation.clip->width)),
        static_cast<int>(std::lround(presentation.clip->height)),
    };
  }
  if (!result.clipped_out) {
    result.widget.x += result.offset_x;
    result.widget.y += result.offset_y;
  }
  return result;
}

bool GlueWidgetRuntime::HasResolvedLayout(const std::string& name) const {
  return resolved_layout_widgets_.contains(name);
}

const openwow::ui::framexml::UiFrame* GlueWidgetRuntime::GetLayoutFrameDefinition(
    const std::string& name) const {
  const auto it = layout_frames_by_name_.find(name);
  if (it == layout_frames_by_name_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::string GlueWidgetRuntime::TemplateSourceName(const std::string& name) const {
  if (name.empty()) {
    return {};
  }
  const auto it = template_source_by_instance_.find(name);
  if (it == template_source_by_instance_.end()) {
    return {};
  }
  return it->second;
}

void GlueWidgetRuntime::RebuildHitTestSpatialCache() const {

  constexpr double kCellSize = 64.0;
  constexpr std::size_t kMaxCellsPerRecord = 4096u;
  const auto cell_coordinate = [](const int value) {
    return static_cast<std::int32_t>(std::clamp(
        std::floor(static_cast<double>(value) / kCellSize),
        static_cast<double>(std::numeric_limits<std::int32_t>::min()),
        static_cast<double>(std::numeric_limits<std::int32_t>::max())));
  };
  const auto cell_key = [](const std::int32_t x, const std::int32_t y) {
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32u) |
        static_cast<std::uint32_t>(y));
  };

  const auto& ordered = VisibleWidgetPointersInRenderOrder();
  hit_test_spatial_records_.clear();
  hit_test_spatial_buckets_.clear();
  hit_test_dynamic_records_.clear();
  hit_test_spatial_records_.reserve(ordered.size());

  const auto has_dynamic_geometry = [&](const GlueWidgetState& widget) {
    const GlueWidgetState* current = &widget;
    std::array<const GlueWidgetState*, 64> visited{};
    std::size_t visited_count = 0;
    while (current != nullptr && visited_count < visited.size()) {
      if (std::find(visited.begin(), visited.begin() + visited_count,
                    current) != visited.begin() + visited_count) {
        return true;
      }
      visited[visited_count++] = current;
      if (animated_geometry_roots_.contains(current->name)) {
        return true;
      }
      if (current != &widget && openwow::text::EqualsIgnoreCaseAscii(
                                    current->kind, "ScrollFrame")) {
        return true;
      }
      if (current->parent.empty()) {
        break;
      }
      const auto parent = widgets_.find(current->parent);
      current = parent != widgets_.end() ? &parent->second : nullptr;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Texture") &&
        !widget.parent.empty() &&
        (ChildNameMatchesSuffix(widget.name, widget.parent, "Thumb") ||
         ChildNameMatchesSuffix(widget.name, widget.parent,
                                "ThumbTexture"))) {
      const auto parent = widgets_.find(widget.parent);
      return parent != widgets_.end() &&
             openwow::text::EqualsIgnoreCaseAscii(parent->second.kind,
                                                  "Slider");
    }
    return false;
  };

  for (auto iterator = ordered.rbegin(); iterator != ordered.rend();
       ++iterator) {
    const GlueWidgetState* const widget = *iterator;
    if (widget == nullptr || widget->virtual_template || widget->width <= 0 ||
        widget->height <= 0) {
      continue;
    }
    const auto hit_rect = ResolveGlueWidgetHitRect(
        *widget, openwow::ui::ResolveDevicePixelsPerUiUnit(
                     static_cast<float>(viewport_height_),
                     GetEffectiveScale(widget->name)));
    const int left = hit_rect.left;
    const int right = hit_rect.right;
    const int top = hit_rect.top;
    const int bottom = hit_rect.bottom;
    if (right <= left || bottom <= top) {
      continue;
    }

    const std::size_t record_index = hit_test_spatial_records_.size();
    hit_test_spatial_records_.push_back({widget, left, top, right, bottom});
    if (has_dynamic_geometry(*widget)) {
      hit_test_dynamic_records_.push_back(record_index);
      continue;
    }
    const std::int32_t min_x = cell_coordinate(left);
    const std::int32_t max_x = cell_coordinate(right - 1);
    const std::int32_t min_y = cell_coordinate(top);
    const std::int32_t max_y = cell_coordinate(bottom - 1);
    const std::uint64_t cells_x =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(max_x) -
                                   static_cast<std::int64_t>(min_x)) +
        1u;
    const std::uint64_t cells_y =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(max_y) -
                                   static_cast<std::int64_t>(min_y)) +
        1u;
    if (cells_x > kMaxCellsPerRecord || cells_y > kMaxCellsPerRecord ||
        cells_x * cells_y > kMaxCellsPerRecord) {
      hit_test_dynamic_records_.push_back(record_index);
      continue;
    }
    for (std::int64_t y = min_y; y <= max_y; ++y) {
      for (std::int64_t x = min_x; x <= max_x; ++x) {
        hit_test_spatial_buckets_[cell_key(static_cast<std::int32_t>(x),
                                           static_cast<std::int32_t>(y))]
            .push_back(record_index);
      }
    }
  }

  ++interaction_performance_counters_.hit_test_index_rebuilds;
  interaction_performance_counters_.hit_test_index_entries =
      hit_test_spatial_records_.size();
  hit_test_spatial_cache_dirty_ = false;
}

bool GlueWidgetRuntime::HasWidget(const std::string& name) const {
  return widgets_.contains(name);
}

std::optional<GlueWidgetState> GlueWidgetRuntime::HitTestTopmostWidget(
    const int x, const int y, const HitTestMode mode) const {
  const auto contains_point = [&](const GlueWidgetState& widget) {
    if (widget.width <= 0 || widget.height <= 0) {
      return false;
    }
    const auto rect = ResolveGlueWidgetHitRect(
        widget, openwow::ui::ResolveDevicePixelsPerUiUnit(
                    static_cast<float>(viewport_height_),
                    GetEffectiveScale(widget.name)));
    return rect.right > rect.left && rect.bottom > rect.top &&
           x >= rect.left && x < rect.right && y >= rect.top &&
           y < rect.bottom;
  };
  const auto resolve_candidate = [&](const GlueWidgetState& widget)
      -> std::optional<GlueWidgetState> {
    if (mode == HitTestMode::kInteractive &&
        (!widget.enabled || !IsMouseEnabled(widget.name))) {
      return std::nullopt;
    }
    std::string mouse_target;
    if (mode == HitTestMode::kMouseTarget) {
      mouse_target = ResolveMouseTargetName(widget.name);
      if (mouse_target.empty()) {
        return std::nullopt;
      }
    }
    const auto presentation = ResolveScrollPresentation(widget);
    if (presentation.clipped_out || !contains_point(presentation.widget)) {
      return std::nullopt;
    }
    if (presentation.clip.has_value()) {
      const auto& clip = *presentation.clip;
      if (x < clip.x || x >= clip.x + clip.width || y < clip.y ||
          y >= clip.y + clip.height) {
        return std::nullopt;
      }
    }
    if (mode != HitTestMode::kMouseTarget) {
      return presentation.widget;
    }

    if (mouse_target != widget.name &&
        !WidgetHitRectContainsPoint(mouse_target, x, y)) {
      return std::nullopt;
    }

    return GetResolvedWidget(mouse_target);
  };

  if (hit_test_spatial_cache_dirty_ || visible_widgets_cache_dirty_) {
    RebuildHitTestSpatialCache();
  }
  interaction_performance_counters_.last_hit_test_candidates = 0;
  constexpr double kCellSize = 64.0;
  const auto cell_coordinate = [](const int value) {
    return static_cast<std::int32_t>(std::clamp(
        std::floor(static_cast<double>(value) / kCellSize),
        static_cast<double>(std::numeric_limits<std::int32_t>::min()),
        static_cast<double>(std::numeric_limits<std::int32_t>::max())));
  };
  const auto cell_key = [](const std::int32_t cell_x,
                           const std::int32_t cell_y) {
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell_x))
         << 32u) |
        static_cast<std::uint32_t>(cell_y));
  };
  static const std::vector<std::size_t> kEmptyBucket;
  const auto bucket = hit_test_spatial_buckets_.find(
      cell_key(cell_coordinate(x), cell_coordinate(y)));
  const auto& fixed_records =
      bucket != hit_test_spatial_buckets_.end() ? bucket->second
                                                 : kEmptyBucket;
  std::size_t fixed_cursor = 0;
  std::size_t dynamic_cursor = 0;
  while (fixed_cursor < fixed_records.size() ||
         dynamic_cursor < hit_test_dynamic_records_.size()) {
    std::size_t record_index = 0;
    if (dynamic_cursor >= hit_test_dynamic_records_.size() ||
        (fixed_cursor < fixed_records.size() &&
         fixed_records[fixed_cursor] <
             hit_test_dynamic_records_[dynamic_cursor])) {
      record_index = fixed_records[fixed_cursor++];
    } else {
      record_index = hit_test_dynamic_records_[dynamic_cursor++];
    }
    if (record_index >= hit_test_spatial_records_.size()) {
      continue;
    }
    const auto* widget = hit_test_spatial_records_[record_index].widget;
    ++interaction_performance_counters_.last_hit_test_candidates;
    if (widget == nullptr || widget->virtual_template ||
        !ButtonTextureStateAllowsVisible(*widget)) {
      continue;
    }
    GlueWidgetState resolved;
    ApplyAnimationTranslation(*widget, &resolved);
    if (auto candidate = resolve_candidate(resolved); candidate.has_value()) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::optional<GlueWidgetState> GlueWidgetRuntime::HitTestTopmostVisibleWidget(
    const int x, const int y) const {
  return HitTestTopmostWidget(x, y, HitTestMode::kVisible);
}

std::optional<GlueWidgetState> GlueWidgetRuntime::HitTestTopmostInteractiveWidget(
    const int x, const int y) const {
  return HitTestTopmostWidget(x, y, HitTestMode::kInteractive);
}

std::optional<GlueWidgetState> GlueWidgetRuntime::HitTestMouseTarget(
    const int x, const int y) const {
  return HitTestTopmostWidget(x, y, HitTestMode::kMouseTarget);
}

std::string GlueWidgetRuntime::ResolveMouseTargetName(
    const std::string& name) const {

  constexpr int kMaxDepth = 64;
  std::string current = name;
  for (int depth = 0; depth < kMaxDepth && !current.empty(); ++depth) {
    const auto it = widgets_.find(current);
    if (it == widgets_.end()) {
      break;
    }
    if (IsMouseEnabled(current)) {
      return current;
    }
    current = it->second.parent;
  }
  return {};
}

bool GlueWidgetRuntime::WidgetHitRectContainsPoint(const std::string& name,
                                                   const int x,
                                                   const int y) const {
  const auto resolved = GetResolvedWidget(name);
  if (!resolved.has_value() || resolved->width <= 0 || resolved->height <= 0) {
    return false;
  }
  const auto rect = ResolveGlueWidgetHitRect(
      *resolved, openwow::ui::ResolveDevicePixelsPerUiUnit(
                     static_cast<float>(viewport_height_),
                     GetEffectiveScale(name)));
  return rect.right > rect.left && rect.bottom > rect.top && x >= rect.left &&
         x < rect.right && y >= rect.top && y < rect.bottom;
}

bool GlueWidgetRuntime::WidgetContainsInputPoint(const std::string& name,
                                                 const int x,
                                                 const int y) const {
  const auto resolved = GetResolvedWidget(name);
  if (!resolved.has_value() || !resolved->enabled || resolved->width <= 0 ||
      resolved->height <= 0 || !IsVisible(name)) {
    return false;
  }
  const auto presentation = ResolveScrollPresentation(*resolved);
  if (presentation.clipped_out) {
    return false;
  }
  const auto& widget = presentation.widget;

  const auto rect = ResolveGlueWidgetHitRect(
      widget, openwow::ui::ResolveDevicePixelsPerUiUnit(
                  static_cast<float>(viewport_height_), GetEffectiveScale(name)));
  if (rect.right <= rect.left || rect.bottom <= rect.top || x < rect.left ||
      x >= rect.right || y < rect.top || y >= rect.bottom) {
    return false;
  }
  if (presentation.clip.has_value()) {
    const auto& clip = *presentation.clip;
    return x >= clip.x && x < clip.x + clip.width && y >= clip.y &&
           y < clip.y + clip.height;
  }
  return true;
}

std::vector<std::string> GlueWidgetRuntime::WidgetNames() const {
  std::vector<std::string> names;
  names.reserve(widgets_.size());
  for (const auto& [name, widget] : widgets_) {
    (void)widget;
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::vector<std::string> GlueWidgetRuntime::WidgetNamesInRegistrationOrder() const {
  std::vector<std::string> names;
  names.reserve(widget_registration_order_.size());
  for (const auto& name : widget_registration_order_) {
    if (widgets_.find(name) != widgets_.end()) {
      names.push_back(name);
    }
  }
  return names;
}

std::vector<std::string> GlueWidgetRuntime::WidgetNamesInSourceOrder() const {
  std::vector<std::string> out;
  out.reserve(widgets_.size());
  std::unordered_set<std::string> seen;
  seen.reserve(widgets_.size() * 2);
  for (const auto& name : source_widget_order_) {
    if (widgets_.find(name) == widgets_.end()) {
      continue;
    }
    if (!seen.insert(name).second) {
      continue;
    }
    out.push_back(name);
  }
  for (const auto& name : widget_registration_order_) {
    if (widgets_.find(name) == widgets_.end()) {
      continue;
    }
    if (!seen.insert(name).second) {
      continue;
    }
    out.push_back(name);
  }
  return out;
}

void GlueWidgetRuntime::RebuildVisibleWidgetOrderCache() const {
  struct OrderedWidget {
    const GlueWidgetState* widget{nullptr};
    double effective_depth{0.0};
    std::size_t insertion_order{0};
  };

  std::vector<OrderedWidget> ordered;
  ordered.reserve(widgets_.size());
  std::unordered_set<const GlueWidgetState*> seen;
  seen.reserve(widgets_.size());

  enum class VisibilityMemo : std::uint8_t {
    kVisiting,
    kVisible,
    kHidden,
  };
  std::unordered_map<const GlueWidgetState*, VisibilityMemo> visible_cache;
  visible_cache.reserve(widgets_.size() * 2);

  std::function<bool(const GlueWidgetState*)> is_visible_memo =
      [&](const GlueWidgetState* widget) -> bool {
    if (widget == nullptr) {
      return false;
    }
    const auto [memo, inserted] =
        visible_cache.try_emplace(widget, VisibilityMemo::kVisiting);
    if (!inserted) {
      return memo->second == VisibilityMemo::kVisible;
    }

    bool result = false;
    if (widget->visible) {
      if (!widget->parent.empty()) {
        const auto parent = widgets_.find(widget->parent);
        result = parent != widgets_.end() && is_visible_memo(&parent->second);
      } else if (!widget->inherits.empty()) {
        const auto templ_it = widgets_.find(widget->inherits);
        result = templ_it != widgets_.end() && !templ_it->second.virtual_template
                     ? is_visible_memo(&templ_it->second)
                     : true;
      } else {
        result = true;
      }
    }

    memo->second = result ? VisibilityMemo::kVisible : VisibilityMemo::kHidden;
    return result;
  };

  std::size_t insertion_order = 0;
  const auto append_visible = [&](const GlueWidgetState& widget) {
    if (!seen.insert(&widget).second) {
      return;
    }
    if (widget.virtual_template) {
      return;
    }
    if (is_visible_memo(&widget)) {
      ordered.push_back(OrderedWidget{
          .widget = &widget,
          .effective_depth = GetEffectiveDepth(widget.name),
          .insertion_order = insertion_order,
      });
    }
    ++insertion_order;
  };

  for (const auto& name : widget_registration_order_) {
    const auto it = widgets_.find(name);
    if (it != widgets_.end()) {
      append_visible(it->second);
    }
  }

  for (const auto& [name, widget] : widgets_) {
    (void)name;
    append_visible(widget);
  }

  const auto is_region = [](const GlueWidgetState& widget) {
    return openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Texture") ||
           openwow::text::EqualsIgnoreCaseAscii(widget.kind, "FontString") ||
           openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Line");
  };

  std::unordered_map<const GlueWidgetState*, std::size_t> index_by_widget;
  index_by_widget.reserve(ordered.size() * 2u);
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    index_by_widget.emplace(ordered[index].widget, index);
  }

  std::vector<openwow::ui::UiPaintOrderNode> paint_nodes;
  paint_nodes.reserve(ordered.size());
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    const auto* widget = ordered[index].widget;
    const auto parent_widget = widgets_.find(widget->parent);
    const auto parent = parent_widget != widgets_.end()
                            ? index_by_widget.find(&parent_widget->second)
                            : index_by_widget.end();
    paint_nodes.push_back({
        .parent = parent != index_by_widget.end()
                      ? parent->second
                      : openwow::ui::kNoUiPaintParent,
        .insertion_order = ordered[index].insertion_order,
        .strata_order = StrataOrder(widget->frame_strata),
        .frame_level = widget->frame_level,
        .draw_layer = openwow::ui::UiDrawLayerOrder(widget->draw_layer),
        .draw_sublevel = widget->draw_sublevel,
        .effective_depth = ordered[index].effective_depth,
        .region = is_region(*widget),
    });
  }

  visible_widget_order_cache_.clear();
  visible_widget_order_cache_.reserve(ordered.size());
  for (const std::size_t index : openwow::ui::BuildUiPaintOrder(paint_nodes)) {
    visible_widget_order_cache_.push_back(ordered[index].widget);
  }
  visible_widgets_cache_dirty_ = false;
}

const std::vector<GlueWidgetState>&
GlueWidgetRuntime::VisibleWidgetsInRenderOrder() const {
  const auto& visible_widgets = VisibleWidgetPointersInRenderOrder();

  if (resolved_visible_widgets_.capacity() < visible_widgets.size()) {
    resolved_visible_widgets_.reserve(visible_widgets.size());
  }
  std::size_t resolved_count = 0;
  for (const auto* widget : visible_widgets) {
    if (widget == nullptr || widget->virtual_template ||
        !ButtonTextureStateAllowsVisible(*widget)) {
      continue;
    }
    if (resolved_count == resolved_visible_widgets_.size()) {
      resolved_visible_widgets_.emplace_back();
    }
    ApplyAnimationTranslation(
        *widget, &resolved_visible_widgets_[resolved_count]);
    ++resolved_count;
  }
  resolved_visible_widgets_.resize(resolved_count);
  return resolved_visible_widgets_;
}

const std::vector<const GlueWidgetState*>&
GlueWidgetRuntime::VisibleWidgetPointersInRenderOrder() const {
  if (visible_widgets_cache_dirty_) {
    RebuildVisibleWidgetOrderCache();
  }
  return visible_widget_order_cache_;
}

void GlueWidgetRuntime::ApplyAnimationTranslation(
    const GlueWidgetState& widget, GlueWidgetState* resolved) const {
  if (resolved == nullptr) {
    return;
  }
  *resolved = widget;

  if (openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Texture") &&
      !widget.parent.empty() &&
      (ChildNameMatchesSuffix(widget.name, widget.parent, "Thumb") ||
       ChildNameMatchesSuffix(widget.name, widget.parent, "ThumbTexture"))) {
    const auto parent_it = widgets_.find(widget.parent);
    if (parent_it != widgets_.end() &&
        openwow::text::EqualsIgnoreCaseAscii(parent_it->second.kind, "Slider")) {
      const auto& slider = parent_it->second;
      const auto [minimum, maximum] = GetMinMaxValues(slider.name);
      const double ratio = maximum > minimum
                               ? std::clamp((GetValue(slider.name) - minimum) /
                                                (maximum - minimum),
                                            0.0, 1.0)
                               : 0.0;
      const bool vertical = slider.height > slider.width;
      if (vertical) {
        resolved->x = slider.x + (slider.width - resolved->width) / 2;
        resolved->y = slider.y + static_cast<int>(std::lround(
                                  static_cast<double>(std::max(0, slider.height - resolved->height)) *
                                  ratio));
      } else {
        resolved->x = slider.x + static_cast<int>(std::lround(
                                  static_cast<double>(std::max(0, slider.width - resolved->width)) *
                                  ratio));
        resolved->y = slider.y + (slider.height - resolved->height) / 2;
      }
    }
  }

  constexpr std::size_t kMaxAnimationParentDepth = 64;
  std::array<const GlueWidgetState*, kMaxAnimationParentDepth> chain{};
  std::size_t chain_size = 0;
  const GlueWidgetState* current = &widget;
  while (current != nullptr && chain_size < chain.size()) {
    if (std::find(chain.begin(), chain.begin() + chain_size, current) !=
        chain.begin() + chain_size) {
      break;
    }
    chain[chain_size++] = current;
    if (current->parent.empty()) {
      break;
    }
    const auto parent = widgets_.find(current->parent);
    current = parent != widgets_.end() ? &parent->second : nullptr;
  }

  float center_x = static_cast<float>(resolved->x) +
                   static_cast<float>(resolved->width) * 0.5f;
  float center_y = static_cast<float>(resolved->y) +
                   static_cast<float>(resolved->height) * 0.5f;
  float resolved_width = static_cast<float>(resolved->width);
  float resolved_height = static_cast<float>(resolved->height);
  float resolved_rotation = 0.0f;

  for (std::size_t index = chain_size; index > 0; --index) {
    const auto* animated_widget = chain[index - 1];
    const auto* props = FindProps(animated_widget->name);
    if (props == nullptr) {
      continue;
    }

    const float origin_x = static_cast<float>(animated_widget->x) +
                           static_cast<float>(animated_widget->width) * 0.5f;
    const float origin_y = static_cast<float>(animated_widget->y) +
                           static_cast<float>(animated_widget->height) * 0.5f;
    float relative_x = (center_x - origin_x) * props->animation_scale_x;
    float relative_y = (center_y - origin_y) * props->animation_scale_y;
    const float c = std::cos(props->animation_rotation_radians);
    const float s = std::sin(props->animation_rotation_radians);
    const float rotated_x = relative_x * c - relative_y * s;
    const float rotated_y = relative_x * s + relative_y * c;
    center_x = origin_x + rotated_x + props->animation_translation_x;
    center_y = origin_y + rotated_y + props->animation_translation_y;
    resolved_width *= props->animation_scale_x;
    resolved_height *= props->animation_scale_y;
    resolved_rotation += props->animation_rotation_radians;
  }

  resolved->x = static_cast<int>(std::lround(center_x - resolved_width * 0.5f));
  resolved->y = static_cast<int>(std::lround(center_y - resolved_height * 0.5f));
  resolved->width = std::max(0, static_cast<int>(std::lround(resolved_width)));
  resolved->height = std::max(0, static_cast<int>(std::lround(resolved_height)));
  resolved->animation_rotation_radians = resolved_rotation;
}

void GlueWidgetRuntime::MarkVisibleWidgetsDirty() {
  ++visible_widget_order_revision_;
  visible_widgets_cache_dirty_ = true;
  hit_test_spatial_cache_dirty_ = true;
}

bool GlueWidgetRuntime::CanFocusEditBox(const std::string& name) const {
  const auto it = widgets_.find(name);
  return it != widgets_.end() && it->second.visible &&
         openwow::text::EqualsIgnoreCaseAscii(it->second.kind, "EditBox");
}

void GlueWidgetRuntime::SetEditAutoFocus(const std::string& name,
                                         const bool auto_focus) {
  const auto it = widgets_.find(name);
  if (it != widgets_.end() &&
      openwow::text::EqualsIgnoreCaseAscii(it->second.kind, "EditBox")) {
    it->second.auto_focus = auto_focus;
  }
}

bool GlueWidgetRuntime::IsEditAutoFocus(const std::string& name) const {
  const auto it = widgets_.find(name);
  return it != widgets_.end() &&
         openwow::text::EqualsIgnoreCaseAscii(it->second.kind, "EditBox") &&
         it->second.auto_focus;
}

void GlueWidgetRuntime::SetFocusedWidget(const std::string& name) {
  if (focused_widget_ == name) {
    return;
  }
  if (!focused_widget_.empty()) {
    MarkCursorDirty(focused_widget_);
  }
  focused_widget_ = name;
  if (!focused_widget_.empty()) {
    MarkCursorDirty(focused_widget_);
  }
  if (focus_owner_changed_callback_) {
    focus_owner_changed_callback_();
  }
}

const std::string& GlueWidgetRuntime::focused_widget() const noexcept {
  return focused_widget_;
}

void GlueWidgetRuntime::SetFocusOwnerChangedCallback(
    FocusOwnerChangedCallback callback) {
  focus_owner_changed_callback_ = std::move(callback);
}

void GlueWidgetRuntime::SetCachedCursorPosition(int x, int y) {
  cached_cursor_position_ = std::pair<int, int>{x, y};
  deferred_hit_test_refresh_ = true;
  deferred_hit_test_refresh_from_cursor_motion_ = true;
}

std::optional<std::pair<int, int>> GlueWidgetRuntime::cached_cursor_position() const {
  return cached_cursor_position_;
}

void GlueWidgetRuntime::MarkCursorHitTestRefresh() {

  deferred_hit_test_refresh_ = true;
}

void GlueWidgetRuntime::MarkDeferredHitTestRefresh() {
  deferred_hit_test_refresh_ = true;
  MarkVisibleWidgetsDirty();
}

bool GlueWidgetRuntime::ConsumeDeferredHitTestRefresh(bool* cursor_motion) {
  const bool needs_refresh = deferred_hit_test_refresh_;
  if (cursor_motion != nullptr) {
    *cursor_motion = deferred_hit_test_refresh_from_cursor_motion_;
  }
  deferred_hit_test_refresh_ = false;
  deferred_hit_test_refresh_from_cursor_motion_ = false;
  return needs_refresh;
}

void GlueWidgetRuntime::ClearBackdrop(const std::string& name) {
  if (name.empty()) return;

  static constexpr const char* kSuffixes[] = {
      ".__BackdropBackground",
      ".__BackdropBorderTopLeft",
      ".__BackdropBorderTop",
      ".__BackdropBorderTopRight",
      ".__BackdropBorderLeft",
      ".__BackdropBorderRight",
      ".__BackdropBorderBottomLeft",
      ".__BackdropBorderBottom",
      ".__BackdropBorderBottomRight",
  };
  bool removed_widget = false;
  for (const char* suffix : kSuffixes) {
    const std::string child = name + suffix;
    if (const auto child_it = widgets_.find(child); child_it != widgets_.end()) {
      const std::string old_parent = child_it->second.parent;
      const std::string old_inherits = child_it->second.inherits;
      widgets_.erase(child_it);
      widget_names_lower_.erase(ToLowerAscii(child));
      ReindexVisibilityRelationships(child, old_parent, old_inherits);
      ForgetFontStringMetrics(child);
      removed_widget = true;
    }
    widget_props_.erase(child);
    layout_frames_by_name_.erase(child);
  }

  auto wit = widgets_.find(name);
  if (wit != widgets_.end()) {
    wit->second.backdrop.reset();
  }
  layout_dirty_ = true;
  deferred_hit_test_refresh_ = true;
  if (removed_widget) {
    ++visibility_revision_;
    MarkVisibleWidgetsDirty();
  }
}

void GlueWidgetRuntime::SetBackdrop(
    const std::string& name,
    const openwow::ui::framexml::detail::BackdropSpec& spec) {
  if (name.empty()) return;

  auto stored_spec = spec;
  QuantizeBackdropSpecColors(&stored_spec);

  ClearBackdrop(name);

  auto sit = widgets_.find(name);
  if (sit != widgets_.end()) {
    sit->second.backdrop = stored_spec;
  }

  openwow::ui::framexml::UiFrame owner;
  owner.name = name;
  const auto it = widgets_.find(name);
  if (it != widgets_.end()) {
    owner.frame_strata = it->second.frame_strata;
    owner.frame_level = it->second.frame_level;
    owner.visible = it->second.visible;
  }

  std::unordered_map<std::string, std::size_t> index_by_name;
  std::vector<openwow::ui::framexml::UiFrame> new_frames;
  openwow::ui::framexml::detail::InjectBackdropPieces(
      stored_spec, owner, &index_by_name, &new_frames);

  for (const auto& frame : new_frames) {
    GlueWidgetState ws;
    ws.name = frame.name;
    ws.kind = frame.kind;
    ws.parent = frame.parent;
    ws.texture_file = frame.file;
    ws.draw_layer = frame.draw_layer;
    ws.draw_sublevel = frame.draw_sublevel;
    ws.frame_strata = frame.frame_strata;
    ws.frame_level = frame.frame_level;
    ws.tile_x = frame.tile_x;
    ws.tile_y = frame.tile_y;
    ws.tile_size_x = frame.tile_size_x;
    ws.tile_size_y = frame.tile_size_y;
    ws.slice = frame.slice;
    ws.slice_edge_size_px = frame.slice_edge_size_px;
    ws.visible = frame.visible;

    ws.color_r = frame.color_r;
    ws.color_g = frame.color_g;
    ws.color_b = frame.color_b;
    ws.color_a = frame.color_a;
    ws.has_vertex_color = frame.has_vertex_color;
    ws.gradient = frame.gradient;

    ws.alpha_mode = frame.alpha_mode;
    if (frame.width.has_value()) ws.width = static_cast<int>(frame.width.value());
    if (frame.height.has_value()) ws.height = static_cast<int>(frame.height.value());
    RegisterWidget(ws);

    layout_frames_by_name_.insert_or_assign(frame.name, frame);
  }
  layout_dirty_ = true;
  deferred_hit_test_refresh_ = true;
}

void GlueWidgetRuntime::SetBackdropColor(const std::string& name, float r,
                                         float g, float b, float a) {
  auto owner = widgets_.find(name);
  if (owner == widgets_.end() || !owner->second.backdrop.has_value()) {
    return;
  }

  const auto color = QuantizeBackdropColor(r, g, b, a);
  auto& backdrop = owner->second.backdrop.value();
  backdrop.bg_color_r = color.red;
  backdrop.bg_color_g = color.green;
  backdrop.bg_color_b = color.blue;
  backdrop.bg_color_a = color.alpha;
  backdrop.has_bg_color = true;

  const auto background = widgets_.find(name + ".__BackdropBackground");
  if (background == widgets_.end()) {
    return;
  }

  background->second.color_r = color.red;
  background->second.color_g = color.green;
  background->second.color_b = color.blue;
  background->second.color_a = color.alpha;
}

void GlueWidgetRuntime::SetBackdropBorderColor(const std::string& name, float r,
                                               float g, float b, float a) {
  auto owner = widgets_.find(name);
  if (owner == widgets_.end() || !owner->second.backdrop.has_value()) {
    return;
  }

  const auto color = QuantizeBackdropColor(r, g, b, a);
  auto& backdrop = owner->second.backdrop.value();
  backdrop.border_color_r = color.red;
  backdrop.border_color_g = color.green;
  backdrop.border_color_b = color.blue;
  backdrop.border_color_a = color.alpha;
  backdrop.has_border_color = true;

  static constexpr std::array<const char*, 8> kBorderPieces = {
      ".__BackdropBorderTopLeft",
      ".__BackdropBorderTop",
      ".__BackdropBorderTopRight",
      ".__BackdropBorderLeft",
      ".__BackdropBorderRight",
      ".__BackdropBorderBottomLeft",
      ".__BackdropBorderBottom",
      ".__BackdropBorderBottomRight",
  };
  for (const char* suffix : kBorderPieces) {
    const auto piece = widgets_.find(name + suffix);
    if (piece == widgets_.end()) {
      continue;
    }
    piece->second.color_r = color.red;
    piece->second.color_g = color.green;
    piece->second.color_b = color.blue;
    piece->second.color_a = color.alpha;
  }
}

int GlueWidgetRuntime::StrataOrder(const std::string& strata) {
  static constexpr std::array<const char*, 8> order = {
      "BACKGROUND",
      "LOW",
      "MEDIUM",
      "HIGH",
      "DIALOG",
      "FULLSCREEN",
      "FULLSCREEN_DIALOG",
      "TOOLTIP",
  };
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (strata == order[i]) {
      return static_cast<int>(i);
    }
  }
  return 2;
}

}
