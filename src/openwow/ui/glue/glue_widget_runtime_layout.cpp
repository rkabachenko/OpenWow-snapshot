#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/foundation/math/float_compare.h"
#include "openwow/ui/framexml/layout_resolver.h"
#include "openwow/ui/texture_natural_size.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/ui_coordinate_space.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>
#include <vector>

namespace openwow::ui::glue {

using openwow::text::ToLowerAscii;
using openwow::text::Trim;

namespace {

bool IsGlueFrameLikeWidget(const GlueWidgetState& widget) {
  return !openwow::text::EqualsIgnoreCaseAscii(widget.kind, "FontString") &&
         !openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Texture") &&
         !openwow::text::EqualsIgnoreCaseAscii(widget.kind, "Line");
}

const std::string& EffectiveGlueFrameStrata(const GlueWidgetState& widget) {
  static const std::string kMedium = "MEDIUM";
  return widget.frame_strata.empty() ? kMedium : widget.frame_strata;
}

std::optional<int> ParseFramePointSlot(const std::string& point) {
  int slot = 0;
  if (openwow::ui::StringToFramePoint(point.c_str(), &slot) == 0) {
    return std::nullopt;
  }
  return slot;
}

int FramePointSortKey(const std::string& point) {
  const auto slot = ParseFramePointSlot(point);
  return slot.has_value() ? *slot : std::numeric_limits<int>::max();
}

bool AnchorHasFlag(const openwow::ui::framexml::UiAnchor& anchor, const std::uint32_t flag) {
  return (anchor.flags & flag) != 0u;
}

bool AnchorIsVisible(const openwow::ui::framexml::UiAnchor& anchor) {
  return !AnchorHasFlag(anchor, 0x800u);
}

bool AnchorsSharePointSlot(const openwow::ui::framexml::UiAnchor& lhs,
                           const openwow::ui::framexml::UiAnchor& rhs) {
  const auto lhs_slot = ParseFramePointSlot(lhs.point);
  const auto rhs_slot = ParseFramePointSlot(rhs.point);
  if (lhs_slot.has_value() && rhs_slot.has_value()) {
    return *lhs_slot == *rhs_slot;
  }
  return lhs.point == rhs.point;
}

bool AnchorsMatchExactly(const openwow::ui::framexml::UiAnchor& lhs,
                         const openwow::ui::framexml::UiAnchor& rhs) {
  return AnchorsSharePointSlot(lhs, rhs)
      && lhs.relative_to == rhs.relative_to
      && lhs.relative_point == rhs.relative_point
      && openwow::math::float_compare::WithinClientEpsilon(lhs.x, rhs.x)
      && openwow::math::float_compare::WithinClientEpsilon(lhs.y, rhs.y)
      && lhs.flags == rhs.flags;
}

void SortAnchorsByPointSlot(std::vector<openwow::ui::framexml::UiAnchor>* anchors) {
  std::stable_sort(anchors->begin(), anchors->end(),
                   [](const openwow::ui::framexml::UiAnchor& lhs,
                      const openwow::ui::framexml::UiAnchor& rhs) {
                     return FramePointSortKey(lhs.point) < FramePointSortKey(rhs.point);
                   });
}

std::vector<const openwow::ui::framexml::UiAnchor*> CollectAnchorsInSlotOrder(
    const std::vector<openwow::ui::framexml::UiAnchor>& anchors) {
  std::vector<const openwow::ui::framexml::UiAnchor*> ordered;
  ordered.reserve(anchors.size());
  for (const auto& anchor : anchors) {
    if (!AnchorIsVisible(anchor)) {
      continue;
    }
    ordered.push_back(&anchor);
  }
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const openwow::ui::framexml::UiAnchor* lhs,
                      const openwow::ui::framexml::UiAnchor* rhs) {
                     return FramePointSortKey(lhs->point) < FramePointSortKey(rhs->point);
                   });
  return ordered;
}

bool ExplicitDimensionMatches(const std::optional<float>& stored,
                              const bool intrinsic,
                              const float requested) {
  return !intrinsic && stored.has_value() &&
         openwow::math::float_compare::WithinClientEpsilon(*stored, requested);
}

}

[[maybe_unused]] static float ComputeConversionFactor(float viewport_width, float viewport_height) {
  return openwow::ui::ComputeUiAspectScaleStateFromViewport(
             viewport_width, viewport_height)
         .kx
      * 1024.0f;
}

void GlueWidgetRuntime::RetryPendingTextureNaturalSizes() {
  if (texture_natural_size_pending_.empty() ||
      texture_natural_size_source_ == nullptr) {
    return;
  }
  for (auto it = texture_natural_size_pending_.begin();
       it != texture_natural_size_pending_.end();) {
    if (!widgets_.contains(it->first)) {
      it = texture_natural_size_pending_.erase(it);
      continue;
    }
    if (!texture_natural_size_source_->ResolveTexturePixelSize(it->second)
             .has_value()) {
      ++it;
      continue;
    }

    layout_dirty_ = true;
    it = texture_natural_size_pending_.erase(it);
  }
}

void GlueWidgetRuntime::SyncTextureNaturalSize(
    const std::string& name, openwow::ui::framexml::UiFrame& frame) {
  const auto widget_it = widgets_.find(name);
  if (widget_it == widgets_.end()) {
    return;
  }
  const GlueWidgetState& widget = widget_it->second;

  const bool solid_colour = widget.texture_file.empty() && widget.has_vertex_color;
  const bool unknown = openwow::ui::SyncTextureNaturalSize(
      frame, texture_natural_size_source_, widget.texture_file, solid_colour);
  if (unknown) {
    texture_natural_size_pending_.insert_or_assign(name, widget.texture_file);
  } else if (!texture_natural_size_pending_.empty()) {
    texture_natural_size_pending_.erase(name);
  }
}

void GlueWidgetRuntime::ResolveLayout(int viewport_width, int viewport_height) {
  SetViewport(viewport_width, viewport_height);

  RetryPendingTextureNaturalSizes();
  if (!layout_dirty_) {
    return;
  }
  ++layout_resolve_count_;

  const auto effective_viewport = openwow::ui::ResolveUiViewportDimensions(
      static_cast<float>(viewport_width_), static_cast<float>(viewport_height_));
  const int effective_viewport_width =
      static_cast<int>(effective_viewport.width);
  const int effective_viewport_height =
      static_cast<int>(effective_viewport.height);

  {
    constexpr const char* kGlueParent = "GlueParent";
    if (auto it = layout_frames_by_name_.find(kGlueParent);
        it != layout_frames_by_name_.end()) {
      it->second.scale = 1.0f;
    }
    if (auto* props = GetProps(kGlueParent); props != nullptr) {
      props->scale = 1.0f;
    }
  }

  std::vector<const openwow::ui::framexml::UiFrame*> frames;
  frames.reserve(layout_frames_by_name_.size());
  for (auto& [name, frame] : layout_frames_by_name_) {

    if (const auto* props = FindProps(name); props != nullptr) {
      frame.scale = props->scale;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(frame.kind, "Texture")) {
      SyncTextureNaturalSize(name, frame);
    }
    frames.push_back(&frame);
  }

  const float ndc_to_pixel =
      openwow::ui::ResolveDevicePixelsPerUiUnit(effective_viewport.height, 1.0F)
          .value;
  constexpr float kGlueRootScale = 1.0f;
  const float ui_scale = ndc_to_pixel * kGlueRootScale;
  const auto aspect_scale_state =
      openwow::ui::ComputeUiAspectScaleStateFromViewport(
          effective_viewport.width, effective_viewport.height);
  ndc_to_pixel_ = ndc_to_pixel;
  root_scale_ = kGlueRootScale;
  ui_scale_ = ui_scale;
  kx_ = aspect_scale_state.kx;
  conversion_factor_ = aspect_scale_state.kx * 1024.0f;

  const auto layout = openwow::ui::framexml::ResolveExpandedLayout(
      frames, effective_viewport_width, effective_viewport_height, ui_scale);

  for (const auto& [name, frame] : layout_frames_by_name_) {
    if (layout.contains(name)) {
      continue;
    }
    const auto widget_it = widgets_.find(name);
    if (widget_it == widgets_.end()) {
      continue;
    }
    const float render_scale = std::max(
        0.000001F, ndc_to_pixel_ * GetEffectiveScale(name));
    if (frame.width.has_value() && frame.width.value() > 0.0F) {
      widget_it->second.width = std::max(
          1, static_cast<int>(std::ceil(frame.width.value() * render_scale)));
    }
    if (frame.height.has_value() && frame.height.value() > 0.0F) {
      widget_it->second.height = std::max(
          1, static_cast<int>(std::ceil(frame.height.value() * render_scale)));
    }
  }

  std::size_t visible_total = 0;
  std::size_t visible_resolved = 0;
  std::vector<std::string> unresolved_visible;
  for (const auto& [name, widget] : widgets_) {
    if (name.empty()) {
      continue;
    }
    if (widget.virtual_template) {
      continue;
    }
    if (!IsVisible(name)) {
      continue;
    }
    ++visible_total;
    const auto it = layout.find(name);
    if (it != layout.end()) {
      ++visible_resolved;
    } else {
      unresolved_visible.push_back(name);
    }
  }
  if (visible_resolved < visible_total) {
    std::string message =
        "Glue layout incomplete: resolved=" + std::to_string(visible_resolved)
        + " total=" + std::to_string(visible_total);
    if (!unresolved_visible.empty()) {
      constexpr std::size_t kLimit = 12;
      message += " unresolved=";
      std::sort(unresolved_visible.begin(), unresolved_visible.end());
      const std::size_t limit = std::min(kLimit, unresolved_visible.size());
      for (std::size_t i = 0; i < limit; ++i) {
        if (i != 0) message += ",";
        message += unresolved_visible[i];
      }
      if (unresolved_visible.size() > limit) {
        message += ",...";
      }
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    const char* dump = std::getenv("OPENWOW_UI_DUMP_LAYOUT");
    if (dump != nullptr && (std::string(dump) == "1" || std::string(dump) == "true")) {
      const std::size_t limit = std::min<std::size_t>(40, unresolved_visible.size());
      for (std::size_t i = 0; i < limit; ++i) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                           "Glue unresolved visible widget: " + unresolved_visible[i]);
      }
    }
  }

  resolved_layout_widgets_.clear();
  for (const auto& [name, rect] : layout) {
    auto it = widgets_.find(name);
    if (it == widgets_.end()) {
      continue;
    }
    resolved_layout_widgets_.insert(name);
    it->second.x = rect.x;
    it->second.y = rect.y;
    it->second.width = rect.width;
    it->second.height = rect.height;
  }

  struct ScrollContentBounds {
    int right{std::numeric_limits<int>::min()};
    int bottom{std::numeric_limits<int>::min()};
  };
  std::unordered_map<std::string, ScrollContentBounds> scroll_bounds;
  scroll_bounds.reserve(16);
  for (const auto& [name, widget] : widgets_) {
    if (!widget.scroll_child_content || widget.width <= 0 ||
        widget.height <= 0 || !widget.visible) {
      continue;
    }

    std::string ancestor_name = widget.parent;
    std::unordered_set<std::string> visited;
    while (!ancestor_name.empty() && visited.insert(ancestor_name).second) {
      const auto ancestor = widgets_.find(ancestor_name);
      if (ancestor == widgets_.end()) {
        break;
      }
      if (openwow::text::EqualsIgnoreCaseAscii(ancestor->second.kind,
                                               "ScrollFrame")) {
        auto& bounds = scroll_bounds[ancestor_name];
        bounds.right = std::max(bounds.right, widget.x + widget.width);
        bounds.bottom = std::max(bounds.bottom, widget.y + widget.height);
        break;
      }
      ancestor_name = ancestor->second.parent;
    }
  }

  for (const auto& [scroll_name, scroll] : widgets_) {
    if (!openwow::text::EqualsIgnoreCaseAscii(scroll.kind, "ScrollFrame")) {
      continue;
    }
    double horizontal_range = 0.0;
    double vertical_range = 0.0;
    if (const auto bounds = scroll_bounds.find(scroll_name);
        bounds != scroll_bounds.end()) {
      horizontal_range = std::max(
          0.0, static_cast<double>(
                   bounds->second.right - (scroll.x + scroll.width)));
      vertical_range = std::max(
          0.0, static_cast<double>(
                   bounds->second.bottom - (scroll.y + scroll.height)));
    }
    SetHorizontalScrollRange(scroll_name, horizontal_range);
    SetVerticalScrollRange(scroll_name, vertical_range);
    SetMinMaxValues(scroll_name + "ScrollBar", 0.0, vertical_range);
    for (const char* suffix : {"ScrollBarScrollUpButton",
                               "ScrollBarScrollDownButton"}) {
      const std::string button_name = scroll_name + suffix;
      if (widgets_.contains(button_name)) {
        SetEnabled(button_name, vertical_range > 0.0);
      }
    }
  }

  if (const char* target = std::getenv("OPENWOW_UI_DEBUG_WIDGET"); target != nullptr
      && std::string(target).size() > 0) {
    const std::string root(target);
    const auto log_widget = [&](const std::string& n) {
      const auto it = widgets_.find(n);
      if (it == widgets_.end()) return;
      const auto& w = it->second;
      std::string extra;
      if (!w.texture_file.empty()) {
        extra += " texture=\"" + w.texture_file + "\"";
      }
      if (!w.model_file.empty()) {
        extra += " model=\"" + w.model_file + "\"";
      }
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                         "UI debug widget: name=" + w.name + " kind=" + w.kind
                             + " parent=" + w.parent
                             + " visible=" + std::string(IsVisible(w.name) ? "true" : "false")
                             + " rect=" + std::to_string(w.x) + "," + std::to_string(w.y)
                             + " " + std::to_string(w.width) + "x" + std::to_string(w.height)
                             + (w.text.empty() ? "" : (" text=\"" + w.text + "\""))
                             + extra);
    };
    log_widget(root);
    for (const auto& [name, widget] : widgets_) {
      if (widget.parent == root) {
        log_widget(name);
      }
    }
  }
  hit_test_spatial_cache_dirty_ = true;
  layout_dirty_ = false;
}

void GlueWidgetRuntime::SetViewport(int viewport_width, int viewport_height) {
  const auto effective_viewport = openwow::ui::ResolveUiViewportDimensions(
      static_cast<float>(viewport_width), static_cast<float>(viewport_height));
  const int next_w = static_cast<int>(effective_viewport.width);
  const int next_h = static_cast<int>(effective_viewport.height);

  const float next_aspect = static_cast<float>(next_w) / static_cast<float>(next_h);
  const auto cached_aspect = openwow::ui::GetCachedUiAspectScaleState();
  if (!cached_aspect.initialized ||
      !openwow::math::float_compare::WithinClientEpsilon(
          cached_aspect.aspect_ratio, next_aspect)) {
    openwow::ui::SetCachedUiAspectScaleState(next_aspect);
  }

  if (next_w != viewport_width_ || next_h != viewport_height_) {
    MarkAllFontStringMetricsDirty();
    layout_dirty_ = true;
    deferred_hit_test_refresh_ = true;
    const float next_height = static_cast<float>(next_h);
    if (std::fabs(model_ffx_viewport_height_ - next_height) >= 0.01f) {
      model_ffx_viewport_height_ = next_height;
      MarkModelFFXViewportDirty();
    }
  }
  viewport_width_ = next_w;
  viewport_height_ = next_h;
}

void GlueWidgetRuntime::MarkModelFFXViewportDirty() {
  model_ffx_viewport_dirty_ = true;
  for (auto& [name, widget] : model_ffx_widgets_) {
    (void)name;
    if (widget != nullptr) {
      widget->set_dirty(true);
    }
  }
}

bool GlueWidgetRuntime::ConsumeModelFFXViewportDirty() {
  const bool dirty = model_ffx_viewport_dirty_;
  model_ffx_viewport_dirty_ = false;
  return dirty;
}

int GlueWidgetRuntime::viewport_width() const {
  return viewport_width_;
}

int GlueWidgetRuntime::viewport_height() const {
  return viewport_height_;
}

bool GlueWidgetRuntime::WouldCreateAnchorCycle(
    const std::string& widget_name,
    const std::string& relative_to) const {
  if (relative_to.empty() || widget_name.empty()) {
    return false;
  }

  std::unordered_set<std::string> visited;
  std::vector<std::string> pending{relative_to};
  while (!pending.empty()) {
    std::string current = std::move(pending.back());
    pending.pop_back();
    if (current == widget_name) {
      return true;
    }
    if (!visited.insert(current).second) {
      continue;
    }

    const auto frame = layout_frames_by_name_.find(current);
    if (frame == layout_frames_by_name_.end()) {
      continue;
    }
    if (frame->second.set_all_points && !frame->second.parent.empty()) {
      pending.push_back(frame->second.parent);
    }
    for (const auto& anchor : frame->second.anchors) {
      pending.push_back(
          anchor.relative_to.empty()
              ? (!frame->second.parent.empty() ? frame->second.parent
                                                : "UIParent")
              : anchor.relative_to);
    }
  }
  return false;
}

void GlueWidgetRuntime::SetPoint(const std::string& name,
                                const std::string& point,
                                const std::string& relative_to,
                                const std::string& relative_point,
                                float x,
                                float y) {
  if (name.empty()) {
    return;
  }

  auto it = layout_frames_by_name_.find(name);
  if (it == layout_frames_by_name_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetPoint: unknown widget: " + name);
    return;
  }

  auto& frame = it->second;
  frame.set_all_points = false;

  openwow::ui::framexml::UiAnchor anchor;
  anchor.point = point.empty() ? "CENTER" : point;
  anchor.relative_to = relative_to;
  anchor.relative_point = relative_point.empty() ? anchor.point : relative_point;
  anchor.x = x;
  anchor.y = y;

  if (anchor.relative_to.empty()) {
    anchor.relative_to = "UIParent";
  }

  if (anchor.relative_to == name) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetPoint: refusing self-anchor: name=" + name);
    return;
  }
  if (WouldCreateAnchorCycle(name, anchor.relative_to)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetPoint: refusing anchor cycle: name=" + name
                           + " relativeTo=" + anchor.relative_to);
    return;
  }

  for (auto& existing : frame.anchors) {
    if (!AnchorsSharePointSlot(existing, anchor)) {
      continue;
    }
    if (AnchorsMatchExactly(existing, anchor)) {
      return;
    }
    existing = anchor;
    SortAnchorsByPointSlot(&frame.anchors);
    MarkFontStringMetricsDirtyInSubtree(name);
    layout_dirty_ = true;
    deferred_hit_test_refresh_ = true;
    return;
  }

  frame.anchors.push_back(anchor);
  SortAnchorsByPointSlot(&frame.anchors);
  MarkFontStringMetricsDirtyInSubtree(name);
  layout_dirty_ = true;
  deferred_hit_test_refresh_ = true;
}

void GlueWidgetRuntime::SetAllPoints(const std::string& name, const std::string& relative_to) {
  if (name.empty()) {
    return;
  }
  auto it = layout_frames_by_name_.find(name);
  if (it == layout_frames_by_name_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetAllPoints: unknown widget: " + name);
    return;
  }

  auto& frame = it->second;
  const std::string target =
      !relative_to.empty() ? relative_to : (!frame.parent.empty() ? frame.parent : "UIParent");

  if (target == name) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetAllPoints: refusing self-anchor: name=" + name);
    return;
  }
  if (WouldCreateAnchorCycle(name, target)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetAllPoints: refusing anchor cycle: name=" + name
                           + " relativeTo=" + target);
    return;
  }

  frame.set_all_points = false;
  frame.anchors.clear();
  frame.anchors.push_back(openwow::ui::framexml::UiAnchor{
      .point = "TOPLEFT",
      .relative_to = target,
      .relative_point = "TOPLEFT",
      .x = 0,
      .y = 0,
  });
  frame.anchors.push_back(openwow::ui::framexml::UiAnchor{
      .point = "BOTTOMRIGHT",
      .relative_to = target,
      .relative_point = "BOTTOMRIGHT",
      .x = 0,
      .y = 0,
  });
  MarkFontStringMetricsDirtyInSubtree(name);
  layout_dirty_ = true;
  deferred_hit_test_refresh_ = true;
}

void GlueWidgetRuntime::ClearAllPoints(const std::string& name) {
  if (name.empty()) {
    return;
  }
  auto it = layout_frames_by_name_.find(name);
  if (it == layout_frames_by_name_.end()) {
    return;
  }
  if (it->second.anchors.empty() && !it->second.set_all_points) {
    return;
  }
  it->second.anchors.clear();
  it->second.set_all_points = false;

}

void GlueWidgetRuntime::ApplyExplicitDimensions(
    const std::string& name,
    GlueWidgetState& widget,
    const std::optional<float> width,
    const std::optional<float> height) {
  const float render_scale = std::max(
      0.000001F, ndc_to_pixel_ * GetEffectiveScale(name));
  const auto layout_frame = layout_frames_by_name_.find(name);

  const auto dimension_matches =
      [&](const std::optional<float>& requested,
          const std::optional<float>& stored,
          const bool intrinsic,
          const int rendered) {
        if (!requested.has_value()) {
          return true;
        }
        if (layout_frame != layout_frames_by_name_.end()) {
          return ExplicitDimensionMatches(stored, intrinsic, *requested);
        }
        return rendered ==
               static_cast<int>(std::ceil(*requested * render_scale));
      };

  const bool width_matches = dimension_matches(
      width,
      layout_frame != layout_frames_by_name_.end()
          ? layout_frame->second.width
          : std::optional<float>{},
      layout_frame != layout_frames_by_name_.end() &&
          layout_frame->second.font_intrinsic_width,
      widget.width);
  const bool height_matches = dimension_matches(
      height,
      layout_frame != layout_frames_by_name_.end()
          ? layout_frame->second.height
          : std::optional<float>{},
      layout_frame != layout_frames_by_name_.end() &&
          layout_frame->second.font_intrinsic_height,
      widget.height);
  if (width_matches && height_matches) {
    return;
  }

  if (width.has_value()) {
    widget.width = static_cast<int>(std::ceil(*width * render_scale));
  }
  if (height.has_value()) {
    widget.height = static_cast<int>(std::ceil(*height * render_scale));
  }
  if (layout_frame != layout_frames_by_name_.end()) {
    if (width.has_value()) {
      layout_frame->second.width = *width;
      layout_frame->second.font_intrinsic_width = false;
    }
    if (height.has_value()) {
      layout_frame->second.height = *height;
      layout_frame->second.font_intrinsic_height = false;
    }
  }

  if (width.has_value()) {
    MarkFontStringMetricsDirtyInSubtree(name);
  } else {
    MarkFontStringMetricsDirty(name);
  }
  layout_dirty_ = true;
  deferred_hit_test_refresh_ = true;
}

void GlueWidgetRuntime::SetSize(const std::string& name, float width, float height) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetSize: unknown widget: " + name);
    return;
  }
  ApplyExplicitDimensions(name, it->second, width, height);
}

void GlueWidgetRuntime::SetWidth(const std::string& name, float width) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetWidth: unknown widget: " + name);
    return;
  }
  ApplyExplicitDimensions(name, it->second, width, std::nullopt);
}

void GlueWidgetRuntime::SetHeight(const std::string& name, float height) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetHeight: unknown widget: " + name);
    return;
  }
  ApplyExplicitDimensions(name, it->second, std::nullopt, height);
}

bool GlueWidgetRuntime::SetFontStringIntrinsicSize(const std::string& name,
                                                   float width,
                                                   float height,
                                                   bool width_intrinsic,
                                                   bool height_intrinsic) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetFontStringIntrinsicSize: unknown widget: " + name);
    return false;
  }

  bool changed = false;
  const auto lf = layout_frames_by_name_.find(name);
  if (lf != layout_frames_by_name_.end()) {
    if (width_intrinsic) {
      if (!lf->second.width.has_value() ||
          !openwow::math::float_compare::WithinClientEpsilon(
              lf->second.width.value(), width) ||
          !lf->second.font_intrinsic_width) {
        lf->second.width = width;
        lf->second.font_intrinsic_width = true;
        changed = true;
      }
    } else if (lf->second.font_intrinsic_width) {
      lf->second.width.reset();
      lf->second.font_intrinsic_width = false;
      changed = true;
    }

    if (height_intrinsic) {
      if (!lf->second.height.has_value() ||
          !openwow::math::float_compare::WithinClientEpsilon(
              lf->second.height.value(), height) ||
          !lf->second.font_intrinsic_height) {
        lf->second.height = height;
        lf->second.font_intrinsic_height = true;
        changed = true;
      }
    } else if (lf->second.font_intrinsic_height) {
      lf->second.height.reset();
      lf->second.font_intrinsic_height = false;
      changed = true;
    }
  } else {
    const float scale = std::max(
        0.000001F, ndc_to_pixel_ * GetEffectiveScale(name));
    const float current_width = static_cast<float>(it->second.width) / scale;
    const float current_height = static_cast<float>(it->second.height) / scale;
    changed =
        (width_intrinsic &&
         !openwow::math::float_compare::WithinClientEpsilon(current_width,
                                                             width)) ||
        (height_intrinsic &&
         !openwow::math::float_compare::WithinClientEpsilon(current_height,
                                                             height));
  }

  if (changed) {

    const float render_scale = std::max(
        0.000001F, ndc_to_pixel_ * GetEffectiveScale(name));
    if (width_intrinsic) {
      it->second.width = std::max(
          1, static_cast<int>(std::ceil(width * render_scale)));
    }
    if (height_intrinsic) {
      it->second.height = std::max(
          1, static_cast<int>(std::ceil(height * render_scale)));
    }
    layout_dirty_ = true;
    deferred_hit_test_refresh_ = true;
  }
  return changed;
}

bool GlueWidgetRuntime::ClearFontStringIntrinsicSize(const std::string& name) {
  bool changed = false;
  if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
    if (lf->second.font_intrinsic_width) {
      lf->second.width.reset();
      lf->second.font_intrinsic_width = false;
      changed = true;
    }
    if (lf->second.font_intrinsic_height) {
      lf->second.height.reset();
      lf->second.font_intrinsic_height = false;
      changed = true;
    }
  }
  if (changed) {
    layout_dirty_ = true;
    deferred_hit_test_refresh_ = true;
  }
  return changed;
}

void GlueWidgetRuntime::SetParent(const std::string& name, const std::string& parent) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetParent: unknown widget: " + name);
    return;
  }
  if (it->second.parent == parent) {
    return;
  }
  const std::string old_parent = it->second.parent;
  it->second.parent = parent;
  ReindexVisibilityRelationships(name, old_parent, it->second.inherits);
  ++visibility_revision_;
  if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
    lf->second.parent = parent;
  }
  MarkFontStringMetricsDirtyInSubtree(name);
  if (IsGlueFrameLikeWidget(it->second)) {
    const bool parent_is_root = parent.empty() || parent == "UIParent";
    const auto parent_it = widgets_.find(parent);
    if (!parent_is_root && parent_it != widgets_.end()) {
      SetFrameStrata(name, EffectiveGlueFrameStrata(parent_it->second));
      SetFrameLevel(name, parent_it->second.frame_level + 1);
    } else if (parent_is_root) {
      SetFrameStrata(name, "MEDIUM");
      SetFrameLevel(name, 0);
    }
  }
  layout_dirty_ = true;
  deferred_hit_test_refresh_ = true;
}

void GlueWidgetRuntime::SetFrameLevel(const std::string& name, int level) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetFrameLevel: unknown widget: " + name);
    return;
  }
  struct PendingLevelUpdate {
    std::string name;
    int requested_level{0};
  };

  std::vector<PendingLevelUpdate> stack;
  stack.push_back(PendingLevelUpdate{name, level});
  while (!stack.empty()) {
    const PendingLevelUpdate pending = stack.back();
    stack.pop_back();

    auto current_it = widgets_.find(pending.name);
    if (current_it == widgets_.end()) {
      continue;
    }

    const int requested = std::max(pending.requested_level, 0);
    const int current_level = current_it->second.frame_level;
    if (requested == current_level) {
      continue;
    }

    int delta = requested - current_level;
    if (delta > 128) {
      delta = 128;
    }

    const std::string parent_strata = EffectiveGlueFrameStrata(current_it->second);
    current_it->second.frame_level += delta;
    if (auto lf = layout_frames_by_name_.find(pending.name); lf != layout_frames_by_name_.end()) {
      lf->second.frame_level = current_it->second.frame_level;
    }

    for (auto& [child_name, child] : widgets_) {
      if (child.parent != pending.name || !IsGlueFrameLikeWidget(child) ||
          EffectiveGlueFrameStrata(child) != parent_strata) {
        continue;
      }
      stack.push_back(PendingLevelUpdate{child_name, child.frame_level + delta});
    }
  }
  deferred_hit_test_refresh_ = true;
  MarkVisibleWidgetsDirty();
}

void GlueWidgetRuntime::SetFrameStrata(const std::string& name, const std::string& strata) {
  if (name.empty()) {
    return;
  }
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetFrameStrata: unknown widget: " + name);
    return;
  }
  const std::string canonical_strata = Trim(strata);
  if (EffectiveGlueFrameStrata(it->second) == canonical_strata) {
    return;
  }

  std::vector<std::string> stack{name};
  while (!stack.empty()) {
    const std::string current_name = stack.back();
    stack.pop_back();

    auto current_it = widgets_.find(current_name);
    if (current_it == widgets_.end()) {
      continue;
    }

    current_it->second.frame_strata = canonical_strata;
    if (auto lf = layout_frames_by_name_.find(current_name); lf != layout_frames_by_name_.end()) {
      lf->second.frame_strata = canonical_strata;
    }

    for (auto& [child_name, child] : widgets_) {
      if (child.parent == current_name && IsGlueFrameLikeWidget(child)) {
        stack.push_back(child_name);
      }
    }
  }
  deferred_hit_test_refresh_ = true;
  MarkVisibleWidgetsDirty();
}

int GlueWidgetRuntime::GetNumPoints(const std::string& name) const {
  if (name.empty()) {
    return 0;
  }
  const auto it = layout_frames_by_name_.find(name);
  if (it == layout_frames_by_name_.end()) {
    return 0;
  }
  return static_cast<int>(std::count_if(
      it->second.anchors.begin(), it->second.anchors.end(),
      [](const openwow::ui::framexml::UiAnchor& anchor) { return AnchorIsVisible(anchor); }));
}

std::optional<openwow::ui::framexml::UiAnchor> GlueWidgetRuntime::GetPoint(const std::string& name,
                                                                          int index) const {
  if (name.empty()) {
    return std::nullopt;
  }
  const auto it = layout_frames_by_name_.find(name);
  if (it == layout_frames_by_name_.end()) {
    return std::nullopt;
  }
  if (index <= 0) {
    return std::nullopt;
  }
  const auto ordered = CollectAnchorsInSlotOrder(it->second.anchors);
  const auto idx = static_cast<std::size_t>(index - 1);
  if (idx >= ordered.size()) {
    return std::nullopt;
  }
  return *ordered[idx];
}

}
