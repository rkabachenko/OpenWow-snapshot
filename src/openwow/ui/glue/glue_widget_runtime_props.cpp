#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace openwow::ui::glue {

using openwow::text::ToLowerAscii;
using openwow::text::Trim;

namespace {

constexpr float kScaleWriteEpsilon = 0.00000023841858f;
constexpr double kSliderWriteEpsilon = 0.00000023841858;
constexpr double kSliderMinimumStep = 0.00000011920929;
constexpr std::uint32_t kGlueInputCharMask = 1u << 0;
constexpr std::uint32_t kGlueInputKeyboardMask = 1u << 1;
constexpr std::uint32_t kGlueInputMouseMask = 1u << 2;
constexpr std::uint32_t kGlueInputMouseWheelMask = 1u << 3;
constexpr std::uint32_t kGlueInputJoystickMask = 1u << 4;
constexpr std::uint32_t kGlueInputKeyboardApiMask =
    kGlueInputCharMask | kGlueInputKeyboardMask;

bool IsGlueFrameLikeWidget(const GlueWidgetState& widget) {
  return !openwow::text::EqualsIgnoreCaseAscii(widget.kind, "FontString") &&
         !openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Texture") &&
         !openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Line");
}

double QuantizeSliderValue(const double value, const double minimum,
                           const double maximum, const double step) {
  double quantized = std::clamp(value, minimum, maximum);
  if (step > 0.0) {
    const double offset = quantized - minimum;
    const double biased = offset + (offset <= 0.0 ? -0.5 * step
                                                  : 0.5 * step);
    quantized = minimum + std::trunc(biased / step) * step;
    quantized = std::clamp(quantized, minimum, maximum);
  }
  return quantized;
}

const std::string& EffectiveGlueFrameStrata(const GlueWidgetState& widget) {
  static const std::string kMedium = "MEDIUM";
  return widget.frame_strata.empty() ? kMedium : widget.frame_strata;
}

struct GlueWidgetRect {
  int left{0};
  int top{0};
  int width{0};
  int height{0};

  [[nodiscard]] int right() const noexcept { return left + width; }
  [[nodiscard]] int bottom() const noexcept { return top + height; }
};

bool GlueWidgetRectsOverlap(const GlueWidgetRect& lhs, const GlueWidgetRect& rhs) {
  return lhs.left < rhs.right() && lhs.right() > rhs.left &&
         lhs.top < rhs.bottom() && lhs.bottom() > rhs.top;
}

bool AnimationFloatMatches(const float lhs, const float rhs) {
  return std::fabs(lhs - rhs) < kScaleWriteEpsilon;
}

bool AnimationAlphaMatches(const std::optional<float>& lhs,
                           const std::optional<float>& rhs) {
  if (lhs.has_value() != rhs.has_value()) {
    return false;
  }
  return !lhs.has_value() || AnimationFloatMatches(*lhs, *rhs);
}

}

void GlueWidgetRuntime::SetGlobalTransitionFactor(float t) {
  global_transition_factor_ = std::clamp(t, 0.0f, 1.0f);
}

float GlueWidgetRuntime::GlobalTransitionFactor() const {
  return global_transition_factor_;
}

void GlueWidgetRuntime::SetGlobalTransitionOverlayVisible(bool visible) {
  global_transition_overlay_visible_ = visible;
}

bool GlueWidgetRuntime::GlobalTransitionOverlayVisible() const {
  return global_transition_overlay_visible_;
}

int GlueWidgetRuntime::GetFrameLevel(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return 0;
  }
  return it->second.frame_level;
}

std::string GlueWidgetRuntime::GetButtonState(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr && !props->button_state.empty()) {
    return props->button_state;
  }
  return "NORMAL";
}

void GlueWidgetRuntime::SetButtonState(const std::string& name, const std::string& state) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    const auto next = Trim(state);
    if (props->button_state == next ||
        (props->button_state.empty() && next == "NORMAL")) {
      return;
    }
    props->button_state = next;
  }
}

void GlueWidgetRuntime::SetDisabledTextColor(
    const std::string& name,
    const openwow::ui::framexml::UiColor& color) {
  const auto widget = widgets_.find(name);
  if (widget == widgets_.end()) return;
  const auto kind = ToLowerAscii(widget->second.kind);
  if (kind != "button" && kind != "checkbutton") return;
  widget->second.button_disabled_color = color;
  if (auto frame = layout_frames_by_name_.find(name);
      frame != layout_frames_by_name_.end()) {
    frame->second.button_disabled_color = color;
  }
}

void GlueWidgetRuntime::Raise(const std::string& name) {
  if (name.empty()) {
    return;
  }

  auto target_it = widgets_.find(name);
  if (target_it == widgets_.end() || !IsGlueFrameLikeWidget(target_it->second)) {
    return;
  }

  while (!target_it->second.toplevel) {
    if (target_it->second.parent.empty()) {
      return;
    }

    target_it = widgets_.find(target_it->second.parent);
    if (target_it == widgets_.end() || !IsGlueFrameLikeWidget(target_it->second)) {
      return;
    }
  }

  const std::string target_name = target_it->first;
  const std::string target_strata = EffectiveGlueFrameStrata(target_it->second);
  const int target_level = target_it->second.frame_level;
  const GlueWidgetRect target_rect{
      .left = target_it->second.x,
      .top = target_it->second.y,
      .width = target_it->second.width,
      .height = target_it->second.height,
  };

  bool has_intersecting_visible_peer = false;
  for (const auto& [other_name, widget] : widgets_) {
    if (other_name == target_name || !IsGlueFrameLikeWidget(widget) ||
        !IsVisible(other_name) || EffectiveGlueFrameStrata(widget) != target_strata ||
        widget.frame_level < target_level) {
      continue;
    }

    std::string current_parent = widget.parent;
    bool descendant_of_target = false;
    while (!current_parent.empty()) {
      if (current_parent == target_name) {
        descendant_of_target = true;
        break;
      }

      const auto parent_it = widgets_.find(current_parent);
      if (parent_it == widgets_.end()) {
        break;
      }
      current_parent = parent_it->second.parent;
    }
    if (descendant_of_target) {
      continue;
    }

    const GlueWidgetRect other_rect{
        .left = widget.x,
        .top = widget.y,
        .width = widget.width,
        .height = widget.height,
    };
    if (GlueWidgetRectsOverlap(target_rect, other_rect)) {
      has_intersecting_visible_peer = true;
      break;
    }
  }

  if (!has_intersecting_visible_peer) {
    return;
  }

  std::vector<int> occupied_levels;
  occupied_levels.reserve(widgets_.size());
  for (const auto& [other_name, widget] : widgets_) {
    if (!IsGlueFrameLikeWidget(widget) || !IsVisible(other_name) ||
        EffectiveGlueFrameStrata(widget) != target_strata) {
      continue;
    }
    occupied_levels.push_back(widget.frame_level);
  }
  if (occupied_levels.empty()) {
    return;
  }

  std::sort(occupied_levels.begin(), occupied_levels.end());
  occupied_levels.erase(std::unique(occupied_levels.begin(), occupied_levels.end()),
                        occupied_levels.end());

  for (auto& [other_name, widget] : widgets_) {
    if (!IsGlueFrameLikeWidget(widget) || !IsVisible(other_name) ||
        EffectiveGlueFrameStrata(widget) != target_strata) {
      continue;
    }

    const auto level_it =
        std::lower_bound(occupied_levels.begin(), occupied_levels.end(), widget.frame_level);
    if (level_it == occupied_levels.end() || *level_it != widget.frame_level) {
      continue;
    }

    widget.frame_level = static_cast<int>(level_it - occupied_levels.begin());
    if (auto lf = layout_frames_by_name_.find(other_name); lf != layout_frames_by_name_.end()) {
      lf->second.frame_level = widget.frame_level;
    }
  }

  SetFrameLevel(target_name, static_cast<int>(occupied_levels.size()));
  deferred_hit_test_refresh_ = true;
}

void GlueWidgetRuntime::SetClampedToScreen(const std::string& name,
                                           bool clamped) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  it->second.clamped_to_screen = clamped;
}

bool GlueWidgetRuntime::IsClampedToScreen(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return false;
  }
  return it->second.clamped_to_screen;
}

void GlueWidgetRuntime::SetMovable(const std::string& name,
                                   const bool movable) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  it->second.movable = movable;
}

bool GlueWidgetRuntime::IsMovable(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return false;
  }
  return it->second.movable;
}

void GlueWidgetRuntime::SetResizable(const std::string& name,
                                     const bool resizable) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  it->second.resizable = resizable;
}

bool GlueWidgetRuntime::IsResizable(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return false;
  }
  return it->second.resizable;
}

void GlueWidgetRuntime::SetToplevel(const std::string& name,
                                    const bool toplevel) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  it->second.toplevel = toplevel;
}

bool GlueWidgetRuntime::IsToplevel(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return false;
  }
  return it->second.toplevel;
}

void GlueWidgetRuntime::SetUserPlaced(const std::string& name,
                                      const bool user_placed) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  it->second.user_placed = user_placed;
}

bool GlueWidgetRuntime::IsUserPlaced(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return false;
  }
  return it->second.user_placed;
}

void GlueWidgetRuntime::SetInputCategoryMask(const std::string& name,
                                             const std::uint32_t mask,
                                             const bool enabled) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    if (enabled) {
      props->input_category_mask |= mask;
    } else {
      props->input_category_mask &= ~mask;
    }
  }
}

bool GlueWidgetRuntime::HasInputCategoryMask(const std::string& name,
                                             const std::uint32_t mask) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return (props->input_category_mask & mask) != 0;
  }
  return false;
}

void GlueWidgetRuntime::SetMouseEnabled(const std::string& name,
                                        const bool enabled) {
  SetInputCategoryMask(name, kGlueInputMouseMask, enabled);
}

bool GlueWidgetRuntime::IsMouseEnabled(const std::string& name) const {
  return HasInputCategoryMask(name, kGlueInputMouseMask);
}

void GlueWidgetRuntime::SetKeyboardEnabled(const std::string& name,
                                           const bool enabled) {
  SetInputCategoryMask(name, kGlueInputKeyboardApiMask, enabled);
}

bool GlueWidgetRuntime::IsKeyboardEnabled(const std::string& name) const {
  return HasInputCategoryMask(name, kGlueInputKeyboardApiMask);
}

void GlueWidgetRuntime::SetMouseWheelEnabled(const std::string& name,
                                             const bool enabled) {
  SetInputCategoryMask(name, kGlueInputMouseWheelMask, enabled);
}

bool GlueWidgetRuntime::IsMouseWheelEnabled(const std::string& name) const {
  return HasInputCategoryMask(name, kGlueInputMouseWheelMask);
}

void GlueWidgetRuntime::SetJoystickEnabled(const std::string& name,
                                           const bool enabled) {
  SetInputCategoryMask(name, kGlueInputJoystickMask, enabled);
}

bool GlueWidgetRuntime::IsJoystickEnabled(const std::string& name) const {
  return HasInputCategoryMask(name, kGlueInputJoystickMask);
}

float GlueWidgetRuntime::GetScale(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->scale;
  }
  return 1.0F;
}

float GlueWidgetRuntime::GetEffectiveScale(const std::string& name) const {

  float effective = root_scale_;
  std::string current = name;

  constexpr int kMaxDepth = 64;
  for (int depth = 0; depth < kMaxDepth && !current.empty(); ++depth) {
    if (const auto* props = FindProps(current); props != nullptr) {
      effective *= props->scale;
    }

    const auto it = widgets_.find(current);
    if (it == widgets_.end()) {
      break;
    }
    current = it->second.parent;
  }
  return effective;
}

void GlueWidgetRuntime::RefreshAnimatedGeometryRoot(
    const std::string& name, const WidgetScriptProps& props) {
  const bool geometric_transform =
      std::fabs(props.animation_translation_x) >= kScaleWriteEpsilon ||
      std::fabs(props.animation_translation_y) >= kScaleWriteEpsilon ||
      std::fabs(props.animation_scale_x - 1.0F) >= kScaleWriteEpsilon ||
      std::fabs(props.animation_scale_y - 1.0F) >= kScaleWriteEpsilon ||
      std::fabs(props.animation_rotation_radians) >= kScaleWriteEpsilon;
  const bool was_dynamic = animated_geometry_roots_.contains(name);
  if (geometric_transform == was_dynamic) {
    return;
  }
  if (geometric_transform) {
    animated_geometry_roots_.insert(name);
  } else {
    animated_geometry_roots_.erase(name);
  }

  hit_test_spatial_cache_dirty_ = true;
}

void GlueWidgetRuntime::SetAnimationTranslation(const std::string& name,
                                                const float x_pixels,
                                                const float y_pixels) {
  auto* props = GetProps(name);
  if (props == nullptr) {
    return;
  }
  if (std::fabs(props->animation_translation_x - x_pixels) < kScaleWriteEpsilon &&
      std::fabs(props->animation_translation_y - y_pixels) < kScaleWriteEpsilon) {
    return;
  }
  props->animation_translation_x = x_pixels;
  props->animation_translation_y = y_pixels;
  RefreshAnimatedGeometryRoot(name, *props);
  deferred_hit_test_refresh_ = true;
}

void GlueWidgetRuntime::SetAnimationTransform(
    const std::string& name, const float x_pixels, const float y_pixels,
    const float scale_x, const float scale_y, const float rotation_radians,
    std::optional<float> alpha, const float alpha_change) {
  auto* props = GetProps(name);
  if (props == nullptr) {
    return;
  }

  const float next_scale_x = std::max(scale_x, 0.001f);
  const float next_scale_y = std::max(scale_y, 0.001f);
  const std::optional<float> next_alpha =
      alpha.has_value()
          ? std::optional<float>{std::clamp(*alpha, 0.0f, 1.0f)}
          : std::nullopt;
  const float next_alpha_change = std::clamp(alpha_change, -1.0f, 1.0f);
  if (AnimationFloatMatches(props->animation_translation_x, x_pixels) &&
      AnimationFloatMatches(props->animation_translation_y, y_pixels) &&
      AnimationFloatMatches(props->animation_scale_x, next_scale_x) &&
      AnimationFloatMatches(props->animation_scale_y, next_scale_y) &&
      AnimationFloatMatches(props->animation_rotation_radians,
                            rotation_radians) &&
      AnimationAlphaMatches(props->animation_alpha, next_alpha) &&
      AnimationFloatMatches(props->animation_alpha_change, next_alpha_change)) {
    return;
  }

  props->animation_translation_x = x_pixels;
  props->animation_translation_y = y_pixels;
  props->animation_scale_x = next_scale_x;
  props->animation_scale_y = next_scale_y;
  props->animation_rotation_radians = rotation_radians;
  props->animation_alpha = next_alpha;
  props->animation_alpha_change = next_alpha_change;
  RefreshAnimatedGeometryRoot(name, *props);
}

void GlueWidgetRuntime::SetScale(const std::string& name, float scale) {
  if (name.empty() || scale == 0.0f) {
    return;
  }

  auto* props = GetProps(name);
  const float current_scale = props != nullptr ? props->scale : GetScale(name);
  if (std::fabs(current_scale - scale) < kScaleWriteEpsilon) {
    return;
  }

  if (props != nullptr) {
    props->scale = scale;
  }
  if (auto layout_frame = layout_frames_by_name_.find(name);
      layout_frame != layout_frames_by_name_.end()) {
    layout_frame->second.scale = scale;
  }
  MarkFontStringMetricsDirtyInSubtree(name);
  layout_dirty_ = true;
  deferred_hit_test_refresh_ = true;
  MarkVisibleWidgetsDirty();
}

void GlueWidgetRuntime::SetDepth(const std::string& name, float depth) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  if (std::fabs(it->second.depth - depth) < kScaleWriteEpsilon) {
    return;
  }
  it->second.depth = depth;
  MarkVisibleWidgetsDirty();
}

void GlueWidgetRuntime::SetEffectiveDepth(const std::string& name, float effective_depth) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }

  double parent_depth = 0.0;
  std::string current = it->second.parent;
  constexpr int kMaxDepth = 64;
  for (int depth = 0; depth < kMaxDepth && !current.empty(); ++depth) {
    const auto parent_it = widgets_.find(current);
    if (parent_it == widgets_.end()) {
      break;
    }
    parent_depth += static_cast<double>(parent_it->second.depth);
    current = parent_it->second.parent;
  }

  const float local_depth = effective_depth - static_cast<float>(parent_depth);
  if (std::fabs(it->second.depth - local_depth) < kScaleWriteEpsilon) {
    return;
  }
  it->second.depth = local_depth;
  MarkVisibleWidgetsDirty();
}

float GlueWidgetRuntime::GetDepth(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return 0.0F;
  }
  return it->second.depth;
}

double GlueWidgetRuntime::GetEffectiveDepth(const std::string& name) const {
  double effective_depth = 0.0;
  std::string current = name;
  constexpr int kMaxDepth = 64;
  for (int depth = 0; depth < kMaxDepth && !current.empty(); ++depth) {
    const auto it = widgets_.find(current);
    if (it == widgets_.end()) {
      break;
    }

    effective_depth += static_cast<double>(it->second.depth);
    current = it->second.parent;
  }
  return effective_depth;
}

void GlueWidgetRuntime::SetIgnoreDepth(const std::string& name, bool ignore_depth) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  it->second.ignore_depth = ignore_depth;
}

bool GlueWidgetRuntime::IsIgnoringDepth(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return false;
  }
  return it->second.ignore_depth;
}

void GlueWidgetRuntime::RegisterForClicks(const std::string& name, std::uint64_t click_mask) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    props->click_mask = click_mask;
  }
}

bool GlueWidgetRuntime::IsClickRegistered(const std::string& name,
                                          const std::uint32_t button_flag,
                                          const bool is_down) const {
  if (button_flag == 0u) {
    return false;
  }
  const auto* props = FindProps(name);
  const std::uint64_t registrations = props != nullptr ? props->click_mask : 1u;
  const std::uint64_t phase_flag =
      is_down ? (static_cast<std::uint64_t>(button_flag) << 32u) : button_flag;
  return (registrations & phase_flag) != 0u;
}

void GlueWidgetRuntime::LockHighlight(const std::string& name, bool locked) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    if (props->highlight_locked == locked) {
      return;
    }
    props->highlight_locked = locked;
  }
}

bool GlueWidgetRuntime::HighlightLocked(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->highlight_locked;
  }
  return false;
}

void GlueWidgetRuntime::SetHovered(const std::string& name, bool hovered) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    if (props->hovered == hovered) {
      return;
    }
    props->hovered = hovered;
  }
}

bool GlueWidgetRuntime::Hovered(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->hovered;
  }
  return false;
}

void GlueWidgetRuntime::SetChecked(const std::string& name, bool checked) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    if (props->checked == checked) {
      return;
    }
    props->checked = checked;
  }
}

bool GlueWidgetRuntime::Checked(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->checked;
  }
  return false;
}

std::pair<double, double> GlueWidgetRuntime::GetMinMaxValues(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return {props->min_value, props->max_value};
  }
  return {0.0, 0.0};
}

void GlueWidgetRuntime::SetMinMaxValues(
    const std::string& name, const double min_value, const double max_value,
    const bool requantize_current_value) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    if (props->slider_range_set &&
        std::fabs(props->min_value - min_value) < kSliderWriteEpsilon &&
        std::fabs(props->max_value - max_value) < kSliderWriteEpsilon) {
      return;
    }
    props->min_value = min_value;
    props->max_value = max_value;
    props->slider_range_set = true;
    if (requantize_current_value && props->slider_value_set) {
      props->value = QuantizeSliderValue(props->value, min_value, max_value,
                                         props->value_step);
    }
  }
}

bool GlueWidgetRuntime::HasSliderRange(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->slider_range_set;
  }
  return false;
}

double GlueWidgetRuntime::GetValue(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->value;
  }
  return 0.0;
}

bool GlueWidgetRuntime::HasSliderValue(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->slider_value_set;
  }
  return false;
}

bool GlueWidgetRuntime::IsStatusBar(const std::string& name) const {
  const auto widget = widgets_.find(name);
  return widget != widgets_.end() &&
         openwow::text::EqualsIgnoreCaseAscii(widget->second.kind,
                                              "StatusBar");
}

openwow::ui::widgets::StatusBarValueSnapshot
GlueWidgetRuntime::GetStatusBarValueSnapshot(const std::string& name) const {
  const auto* props = FindProps(name);
  return props != nullptr && props->status_bar.has_value()
             ? props->status_bar->Snapshot()
             : openwow::ui::widgets::StatusBarValueSnapshot{};
}

openwow::ui::widgets::StatusBarRangeChange
GlueWidgetRuntime::SetStatusBarRange(const std::string& name,
                                     const float minimum,
                                     const float maximum) {
  auto* props = GetProps(name);
  if (props == nullptr) {
    return {};
  }
  if (!props->status_bar.has_value()) {
    props->status_bar.emplace();
  }
  return props->status_bar->SetRange(minimum, maximum);
}

bool GlueWidgetRuntime::SetStatusBarValue(const std::string& name,
                                          const float value) {
  auto* props = GetProps(name);
  return props != nullptr && props->status_bar.has_value() &&
         props->status_bar->SetValue(value);
}

void GlueWidgetRuntime::SetValue(const std::string& name, double value) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {

    if (!props->slider_range_set) {
      return;
    }
    const double next = QuantizeSliderValue(
        value, props->min_value, props->max_value, props->value_step);
    if (props->slider_value_set &&
        std::fabs(next - props->value) < kSliderWriteEpsilon) {
      return;
    }
    props->value = next;
    props->slider_value_set = true;
  }
}

double GlueWidgetRuntime::GetValueStep(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->value_step;
  }
  return 0.0;
}

void GlueWidgetRuntime::SetValueStep(const std::string& name, double value_step) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    const double next_step = std::max(value_step, kSliderMinimumStep);
    if (std::fabs(next_step - props->value_step) < kSliderWriteEpsilon) {
      return;
    }
    props->value_step = next_step;
    if (props->slider_range_set && props->slider_value_set) {
      props->value = QuantizeSliderValue(
          props->value, props->min_value, props->max_value, next_step);
    }
  }
}

double GlueWidgetRuntime::GetVerticalScroll(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->vertical_scroll;
  }
  return 0.0;
}

void GlueWidgetRuntime::SetVerticalScroll(const std::string& name, double offset) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {

    if (std::fabs(offset - props->vertical_scroll) < 9.5367432e-7) {
      return;
    }
    props->vertical_scroll = offset;
    deferred_hit_test_refresh_ = true;
  }
}

double GlueWidgetRuntime::GetVerticalScrollRange(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->vertical_scroll_range;
  }
  return 0.0;
}

void GlueWidgetRuntime::SetVerticalScrollRange(const std::string& name, double range) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    const double next = std::max(0.0, range);
    if (std::fabs(next - props->vertical_scroll_range) <
        kSliderWriteEpsilon) {
      return;
    }
    props->vertical_scroll_range = next;
    QueueScrollRangeChangedEvent(name);
  }
}

double GlueWidgetRuntime::GetHorizontalScroll(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->horizontal_scroll;
  }
  return 0.0;
}

void GlueWidgetRuntime::SetHorizontalScroll(const std::string& name,
                                            const double offset) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {

    if (std::fabs(offset - props->horizontal_scroll) < 9.5367432e-7) {
      return;
    }
    props->horizontal_scroll = offset;
    deferred_hit_test_refresh_ = true;
  }
}

double GlueWidgetRuntime::GetHorizontalScrollRange(
    const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->horizontal_scroll_range;
  }
  return 0.0;
}

void GlueWidgetRuntime::SetHorizontalScrollRange(const std::string& name,
                                                 const double range) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    const double next = std::max(0.0, range);
    if (std::fabs(next - props->horizontal_scroll_range) <
        kSliderWriteEpsilon) {
      return;
    }
    props->horizontal_scroll_range = next;
    QueueScrollRangeChangedEvent(name);
  }
}

void GlueWidgetRuntime::QueueScrollRangeChangedEvent(
    const std::string& name) {
  const auto* props = FindProps(name);
  if (props == nullptr) {
    return;
  }
  const auto existing = std::find_if(
      pending_scroll_range_changed_events_.begin(),
      pending_scroll_range_changed_events_.end(),
      [&](const PendingScrollRangeChangedEvent& event) {
        return event.widget_name == name;
      });
  if (existing != pending_scroll_range_changed_events_.end()) {
    existing->horizontal_range = props->horizontal_scroll_range;
    existing->vertical_range = props->vertical_scroll_range;
    return;
  }
  pending_scroll_range_changed_events_.push_back({
      .widget_name = name,
      .horizontal_range = props->horizontal_scroll_range,
      .vertical_range = props->vertical_scroll_range,
  });
}

std::vector<GlueWidgetRuntime::PendingScrollRangeChangedEvent>
GlueWidgetRuntime::ConsumeScrollRangeChangedEvents() {
  std::vector<PendingScrollRangeChangedEvent> result;
  result.swap(pending_scroll_range_changed_events_);
  return result;
}

void GlueWidgetRuntime::SetMaxBytes(const std::string& name, int max_bytes) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    props->max_bytes = max_bytes;
  }
}

int GlueWidgetRuntime::GetMaxBytes(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->max_bytes;
  }
  return -1;
}

void GlueWidgetRuntime::SetMaxLetters(const std::string& name, int max_letters) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    props->max_letters = max_letters;
  }
}

int GlueWidgetRuntime::GetMaxLetters(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->max_letters;
  }
  return -1;
}

void GlueWidgetRuntime::SetModel(const std::string& name, const std::string& model_file) {
  if (name.empty()) {
    return;
  }

  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }

  const auto kind_lower = ToLowerAscii(it->second.kind);
  if (kind_lower == "modelffx") {
    auto obj = ResolveModelFFXWidget(name);
    if (obj) {
      const std::string normalized = obj->SetModelFile(model_file);
      it->second.model_file = normalized;
      if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
        lf->second.file = normalized;
      }
      layout_dirty_ = true;
      return;
    }
  }

  it->second.model_file = model_file;
  if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
    lf->second.file = model_file;
  }
  layout_dirty_ = true;
}

void GlueWidgetRuntime::SetSequence(const std::string& name, int sequence) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->model_sequence = sequence;
    ++props->model_sequence_revision;
  }
}

void GlueWidgetRuntime::SetSequenceTime(const std::string& name, int sequence, std::uint32_t time_ms) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->model_sequence = sequence;
    ++props->model_sequence_revision;
    props->model_sequence_time_override = true;
    props->model_sequence_time_ms = time_ms;
  }
}

int GlueWidgetRuntime::GetSequence(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->model_sequence;
  }
  return 0;
}

std::uint64_t GlueWidgetRuntime::GetSequenceRevision(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->model_sequence_revision;
  }
  return 0;
}

void GlueWidgetRuntime::SetCamera(const std::string& name, int camera_index) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->model_camera = camera_index;
  }
}

int GlueWidgetRuntime::GetCamera(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->model_camera;
  }
  return 0;
}

std::optional<std::uint32_t> GlueWidgetRuntime::ConsumeSequenceTimeMs(const std::string& name) {
  if (name.empty()) return std::nullopt;
  if (auto* props = GetProps(name); props != nullptr) {
    if (!props->model_sequence_time_override) {
      return std::nullopt;
    }
    props->model_sequence_time_override = false;
    return props->model_sequence_time_ms;
  }
  return std::nullopt;
}

void GlueWidgetRuntime::SetModelScale(const std::string& name, float scale) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->model_scale = scale;
  }
}

float GlueWidgetRuntime::GetModelScale(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->model_scale;
  }
  return 1.0f;
}

void GlueWidgetRuntime::SetModelPosition(const std::string& name,
                                         const float x,
                                         const float y,
                                         const float z) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->model_x = x;
    props->model_y = y;
    props->model_z = z;
  }
}

void GlueWidgetRuntime::GetModelPosition(const std::string& name,
                                         float& x,
                                         float& y,
                                         float& z) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    x = props->model_x;
    y = props->model_y;
    z = props->model_z;
    return;
  }
  x = 0.0f;
  y = 0.0f;
  z = 0.0f;
}

void GlueWidgetRuntime::SetFacing(const std::string& name, float facing_rad) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->facing_rad = facing_rad;
  }
}

float GlueWidgetRuntime::GetFacing(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->facing_rad;
  }
  return 0.0f;
}

void GlueWidgetRuntime::SetDesaturated(const std::string& name, bool desaturated) {
  if (name.empty()) {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    props->desaturated = desaturated;
  }
}

bool GlueWidgetRuntime::Desaturated(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->desaturated;
  }
  return false;
}

void GlueWidgetRuntime::SetFogColor(const std::string& name, float r, float g, float b) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->fog_r = r;
    props->fog_g = g;
    props->fog_b = b;
    props->fog_enabled = true;
  }
}

void GlueWidgetRuntime::SetFogNear(const std::string& name, float near_v) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->fog_near = near_v;
    props->fog_enabled = true;
  }
}

void GlueWidgetRuntime::SetFogFar(const std::string& name, float far_v) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->fog_far = far_v;
    props->fog_enabled = true;
  }
}

void GlueWidgetRuntime::GetFogColor(const std::string& name, float& r, float& g, float& b) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    r = props->fog_r;
    g = props->fog_g;
    b = props->fog_b;
  } else {
    r = 0.0f;
    g = 0.0f;
    b = 0.0f;
  }
}

float GlueWidgetRuntime::GetFogNear(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->fog_near;
  }
  return 0.0f;
}

float GlueWidgetRuntime::GetFogFar(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->fog_far;
  }
  return 0.0f;
}

void GlueWidgetRuntime::ClearFog(const std::string& name) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->fog_r = 0.0f;
    props->fog_g = 0.0f;
    props->fog_b = 0.0f;
    props->fog_near = 0.0f;
    props->fog_far = 0.0f;
    props->fog_enabled = false;
  }
}

bool GlueWidgetRuntime::IsFogEnabled(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->fog_enabled;
  }
  return false;
}

void GlueWidgetRuntime::SetGlow(const std::string& name, float glow) {
  if (name.empty()) return;
  const auto it = widgets_.find(name);
  if (it == widgets_.end() || ToLowerAscii(it->second.kind) != "modelffx") {
    return;
  }
  if (auto* props = GetProps(name); props != nullptr) {
    props->glow = glow;
  }
}

float GlueWidgetRuntime::GetGlow(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end() || ToLowerAscii(it->second.kind) != "modelffx") {
    return 0.0f;
  }
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->glow;
  }
  return 0.3f;
}

void GlueWidgetRuntime::ResetLights(const std::string& name) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    for (int i = 0; i < 2; ++i) {
      props->general_lights[i].clear();
      props->character_lights[i].clear();
      props->pet_lights[i].clear();
    }
  }
}

bool GlueWidgetRuntime::AddModelLight(const std::string& name,
                                       ModelLightCategory category,
                                       int light_type,
                                       const ModelLightEntry& entry) {
  if (name.empty()) return false;
  const int slot = (light_type == 0) ? 0 : 1;
  auto* props = GetProps(name);
  if (props == nullptr) return false;

  std::vector<ModelLightEntry>* target = nullptr;
  switch (category) {
    case ModelLightCategory::kGeneral:   target = &props->general_lights[slot]; break;
    case ModelLightCategory::kCharacter: target = &props->character_lights[slot]; break;
    case ModelLightCategory::kPet:       target = &props->pet_lights[slot]; break;
  }
  if (target == nullptr) return false;
  if (static_cast<int>(target->size()) >= kMaxLightsPerSlot) return false;
  target->push_back(entry);
  return true;
}

static const std::vector<ModelLightEntry> kEmptyLights;

const std::vector<ModelLightEntry>& GlueWidgetRuntime::GetModelLights(
    const std::string& name, ModelLightCategory category, int light_type) const {
  if (name.empty()) return kEmptyLights;
  const int slot = (light_type == 0) ? 0 : 1;
  const auto* props = FindProps(name);
  if (props == nullptr) return kEmptyLights;
  switch (category) {
    case ModelLightCategory::kGeneral:   return props->general_lights[slot];
    case ModelLightCategory::kCharacter: return props->character_lights[slot];
    case ModelLightCategory::kPet:       return props->pet_lights[slot];
  }
  return kEmptyLights;
}

}
