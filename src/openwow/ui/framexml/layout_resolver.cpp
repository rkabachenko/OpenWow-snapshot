#include "openwow/ui/framexml/layout_resolver.h"
#include "openwow/ui/framexml/anchor_semantics.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/framexml/layout_anchor_resolution.h"
#include "openwow/ui/rect_utils.h"
#include "openwow/ui/ui_aspect_scales.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

namespace openwow::ui::framexml {

namespace {

constexpr float kUndefined = std::numeric_limits<float>::max();

int StrataOrder(const std::string &strata) {
  static constexpr std::array<const char *, 8> order = {
      "BACKGROUND", "LOW", "MEDIUM", "HIGH", "DIALOG", "FULLSCREEN", "FULLSCREEN_DIALOG", "TOOLTIP",
  };
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (strata == order[i]) {
      return static_cast<int>(i);
    }
  }
  return 2;
}

struct RectDDC {
  float min_x{kUndefined};
  float min_y{kUndefined};
  float max_x{kUndefined};
  float max_y{kUndefined};
};

using ResolvedRectMap = std::unordered_map<std::string_view, RectDDC>;

void ApplyScreenClamp(const UiFrame &frame, float viewport_width, float viewport_height,
                      float effective_scale, RectDDC *rect) {
  if (rect == nullptr || !frame.clamped_to_screen.value_or(false)) {
    return;
  }

  openwow::ui::RectEdgesYUp edges{
      .bottom = rect->max_y,
      .left = rect->min_x,
      .top = rect->min_y,
      .right = rect->max_x,
  };
  openwow::ui::ClampRectEdgesYUpPreservingSpan(
      &edges, openwow::ui::RectBoundsYUp{
                  .min_x = -(frame.clamp_rect_inset_left * effective_scale),
                  .min_y = -(frame.clamp_rect_inset_bottom * effective_scale),
                  .max_x = viewport_width - frame.clamp_rect_inset_right * effective_scale,
                  .max_y = viewport_height - frame.clamp_rect_inset_top * effective_scale,
              });
  rect->min_x = edges.left;
  rect->min_y = edges.top;
  rect->max_x = edges.right;
  rect->max_y = edges.bottom;
}

bool IsDefined(float v) {
  return v != kUndefined && std::isfinite(v);
}

float SynthesizeHorizontalSide(float center, float opposite, float size) {
  if (IsDefined(center) && IsDefined(opposite)) {
    return center + center - opposite;
  }
  if (IsDefined(opposite) && size != 0.0F) {
    return opposite + size;
  }
  if (IsDefined(center)) {
    if (size == 0.0F) {
      if (IsDefined(opposite)) {
        return center + center - opposite;
      }
    } else {
      return center + (size * 0.5F);
    }
  }
  return kUndefined;
}

float SynthesizeVerticalSide(float center, float opposite, float size) {
  if (IsDefined(opposite) && size != 0.0F) {
    return opposite + size;
  }
  if (IsDefined(center)) {
    if (size == 0.0F) {
      if (IsDefined(opposite)) {
        return center + center - opposite;
      }
    } else {
      return center + (size * 0.5F);
    }
  }
  return kUndefined;
}

bool ShouldDumpLayoutTrace() {
  static const bool enabled = [] {
    const char *v = std::getenv("OPENWOW_UI_DUMP_LAYOUT");
    if (v == nullptr)
      return false;
    std::string s(v);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s == "1" || s == "true" || s == "yes" || s == "on";
  }();
  return enabled;
}

std::optional<RectDDC> ResolveRectDDC(const UiFrame &frame,
                                      const ResolvedRectMap &out,
                                      float viewport_width, float viewport_height,
                                      float effective_scale) {
  const auto apply_layout_offset = [&](RectDDC rect) {
    const float offset_x = frame.layout_offset_x * effective_scale;
    const float offset_y = frame.layout_offset_y * effective_scale;
    rect.min_x += offset_x;
    rect.max_x += offset_x;
    rect.min_y += offset_y;
    rect.max_y += offset_y;
    return rect;
  };
  if (frame.set_all_points) {

    const std::string_view target =
        openwow::ui::framexml::detail::SetAllPointsTargetName(frame);

    const auto it = out.find(target);
    if (it == out.end()) {

      return std::nullopt;
    }

    return apply_layout_offset(it->second);
  }

  const auto anchor_slots = openwow::ui::framexml::detail::BuildAnchorSlots(frame.anchors);
  const auto has_visible_anchor = [&anchor_slots](const auto& point_slots) {
    for (const int slot : point_slots) {
      if (slot < 0 || slot >= static_cast<int>(anchor_slots.size())) {
        continue;
      }
      const auto* anchor = anchor_slots[static_cast<std::size_t>(slot)];
      if (anchor != nullptr &&
          !openwow::ui::framexml::detail::AnchorHasFlag(
              *anchor, openwow::ui::framexml::detail::kAnchorHiddenBit)) {
        return true;
      }
    }
    return false;
  };
  const auto lookup_anchor_rect =
      [&out](
          const std::string_view name) -> std::optional<openwow::ui::framexml::detail::AnchorRect> {
    const auto it = out.find(name);
    if (it == out.end()) {
      return std::nullopt;
    }

    return openwow::ui::framexml::detail::AnchorRect{
        .min_x = it->second.min_x,
        .min_y = it->second.min_y,
        .max_x = it->second.max_x,
        .max_y = it->second.max_y,
    };
  };

  float width = frame.width.value_or(0.0f) * effective_scale;
  float height = frame.height.value_or(0.0f) * effective_scale;

  if (openwow::text::EqualsIgnoreCaseAscii(frame.kind, "texture")) {
    if (width <= 0.0F && frame.texture_natural_width.has_value()) {
      width = *frame.texture_natural_width * effective_scale;
    }
    if (height <= 0.0F && frame.texture_natural_height.has_value()) {
      height = *frame.texture_natural_height * effective_scale;
    }
  }

  if (openwow::ui::framexml::IsSimpleModelRuntimeKind(frame.runtime_kind) &&
      (frame.model_natural_width.has_value() ||
       frame.model_natural_height.has_value())) {
    const float aspect_ratio =
        viewport_height > 0.0F ? viewport_width / viewport_height
                               : openwow::ui::kDefaultUiAspectRatio;
    if (width <= 0.0F && frame.model_natural_width.has_value()) {
      width = openwow::ui::StoredUiHorizontalCoordinateToPixels(
                  *frame.model_natural_width, aspect_ratio) *
              effective_scale;
    }
    if (height <= 0.0F && frame.model_natural_height.has_value()) {
      height = openwow::ui::StoredUiHorizontalCoordinateToPixels(
                   *frame.model_natural_height, aspect_ratio) *
               effective_scale;
    }
  }

  const bool has_left_anchor =
      has_visible_anchor(openwow::ui::framexml::detail::kLeftPointSlots);
  const bool has_center_x_anchor = has_visible_anchor(
      openwow::ui::framexml::detail::kHorizontalCenterPointSlots);
  const bool has_right_anchor =
      has_visible_anchor(openwow::ui::framexml::detail::kRightPointSlots);
  const bool has_top_anchor =
      has_visible_anchor(openwow::ui::framexml::detail::kTopPointSlots);
  const bool has_center_y_anchor = has_visible_anchor(
      openwow::ui::framexml::detail::kVerticalCenterPointSlots);
  const bool has_bottom_anchor =
      has_visible_anchor(openwow::ui::framexml::detail::kBottomPointSlots);

  const bool horizontal_anchor_constrained =
      static_cast<int>(has_left_anchor) + static_cast<int>(has_center_x_anchor) +
          static_cast<int>(has_right_anchor) >=
      2;
  const bool vertical_anchor_constrained =
      static_cast<int>(has_top_anchor) + static_cast<int>(has_center_y_anchor) +
          static_cast<int>(has_bottom_anchor) >=
      2;

  if (width <= 0.0f && frame.rel_width.has_value()) {
    const std::string_view parent_name =
        frame.parent.empty() ? std::string_view{"UIParent"} : frame.parent;
    const auto parent_it = out.find(parent_name);
    if (parent_it != out.end()) {
      const float parent_w = parent_it->second.max_x - parent_it->second.min_x;
      width = parent_w * frame.rel_width.value();
    }
  }
  if (height <= 0.0f && frame.rel_height.has_value()) {
    const std::string_view parent_name =
        frame.parent.empty() ? std::string_view{"UIParent"} : frame.parent;
    const auto parent_it = out.find(parent_name);
    if (parent_it != out.end()) {
      const float parent_h = parent_it->second.max_y - parent_it->second.min_y;
      height = parent_h * frame.rel_height.value();
    }
  }

  if (openwow::text::EqualsIgnoreCaseAscii(frame.kind, "fontstring")) {

    if (width <= 0.0F && !horizontal_anchor_constrained) {
      width = 1.0f;
    }
    if (height <= 0.0F && !vertical_anchor_constrained) {
      height = 14.0f;
    }
  }

  const float left_x = openwow::ui::framexml::detail::ResolvePointSetWithRetries(
      anchor_slots, openwow::ui::framexml::detail::kLeftPointSlots, frame.parent, effective_scale,
      lookup_anchor_rect, openwow::ui::framexml::detail::ResolveRelativeX);
  const float right_x = openwow::ui::framexml::detail::ResolvePointSetWithRetries(
      anchor_slots, openwow::ui::framexml::detail::kRightPointSlots, frame.parent, effective_scale,
      lookup_anchor_rect, openwow::ui::framexml::detail::ResolveRelativeX);
  const float center_x = openwow::ui::framexml::detail::ResolvePointSetWithRetries(
      anchor_slots, openwow::ui::framexml::detail::kHorizontalCenterPointSlots, frame.parent,
      effective_scale, lookup_anchor_rect, openwow::ui::framexml::detail::ResolveRelativeX);
  const float bottom_y = openwow::ui::framexml::detail::ResolvePointSetWithRetries(
      anchor_slots, openwow::ui::framexml::detail::kBottomPointSlots, frame.parent, effective_scale,
      lookup_anchor_rect, openwow::ui::framexml::detail::ResolveRelativeY);
  const float top_y = openwow::ui::framexml::detail::ResolvePointSetWithRetries(
      anchor_slots, openwow::ui::framexml::detail::kTopPointSlots, frame.parent, effective_scale,
      lookup_anchor_rect, openwow::ui::framexml::detail::ResolveRelativeY);
  const float center_y = openwow::ui::framexml::detail::ResolvePointSetWithRetries(
      anchor_slots, openwow::ui::framexml::detail::kVerticalCenterPointSlots, frame.parent,
      effective_scale, lookup_anchor_rect, openwow::ui::framexml::detail::ResolveRelativeY);

  if ((has_left_anchor && !IsDefined(left_x)) ||
      (has_right_anchor && !IsDefined(right_x)) ||
      (has_center_x_anchor && !IsDefined(center_x)) ||
      (has_bottom_anchor && !IsDefined(bottom_y)) ||
      (has_top_anchor && !IsDefined(top_y)) ||
      (has_center_y_anchor && !IsDefined(center_y))) {
    return std::nullopt;
  }

  float resolved_left = left_x;
  if (!IsDefined(resolved_left)) {
    const float synth = SynthesizeHorizontalSide(center_x, right_x, -width);
    resolved_left = synth;
  }
  float resolved_right = right_x;
  if (!IsDefined(resolved_right)) {
    const float synth = SynthesizeHorizontalSide(center_x, left_x, width);
    resolved_right = synth;
  }
  float resolved_bottom = bottom_y;
  if (!IsDefined(resolved_bottom)) {
    const float synth = SynthesizeVerticalSide(center_y, top_y, -height);
    resolved_bottom = synth;
  }
  float resolved_top = top_y;
  if (!IsDefined(resolved_top)) {
    const float synth = SynthesizeVerticalSide(center_y, bottom_y, height);
    resolved_top = synth;
  }

  if (!IsDefined(resolved_left) || !IsDefined(resolved_right) || !IsDefined(resolved_bottom) ||
      !IsDefined(resolved_top)) {
    return std::nullopt;
  }

  RectDDC rect;
  rect.min_x = resolved_left;
  rect.max_x = resolved_right;
  rect.min_y = resolved_bottom;
  rect.max_y = resolved_top;
  if (rect.max_x < rect.min_x)
    std::swap(rect.max_x, rect.min_x);
  if (rect.max_y < rect.min_y)
    std::swap(rect.max_y, rect.min_y);
  rect = apply_layout_offset(rect);
  ApplyScreenClamp(frame, viewport_width, viewport_height, effective_scale, &rect);
  return rect;
}

void MergeFromTemplate(UiFrame *frame, const UiFrame &templ) {
  if (frame->parent.empty() && !templ.parent.empty()) {
    frame->parent = templ.parent;
  }
  if (!frame->width.has_value() && templ.width.has_value()) {
    frame->width = templ.width;
  }
  if (!frame->height.has_value() && templ.height.has_value()) {
    frame->height = templ.height;
  }
  if (!frame->auto_focus.has_value() && templ.auto_focus.has_value()) {
    frame->auto_focus = templ.auto_focus;
  }
  if (!frame->backdrop.has_value() && templ.backdrop.has_value()) {
    frame->backdrop = templ.backdrop;
  }
  if (frame->anchors.empty() && !templ.anchors.empty()) {
    frame->anchors = templ.anchors;
  }
  if (!frame->set_all_points_explicit && templ.set_all_points_explicit) {
    frame->set_all_points = templ.set_all_points;
    frame->set_all_points_explicit = true;
  } else if (!frame->set_all_points_explicit && !frame->set_all_points &&
             templ.set_all_points) {
    frame->set_all_points = true;
  }
  if (!frame->clamped_to_screen.has_value() && templ.clamped_to_screen.has_value()) {
    frame->clamped_to_screen = templ.clamped_to_screen;
  }
  if (frame->frame_strata.empty() && !templ.frame_strata.empty()) {
    frame->frame_strata = templ.frame_strata;
  }
  if (!frame->has_frame_level && templ.has_frame_level) {
    frame->frame_level = templ.frame_level;
    frame->has_frame_level = true;
  }
  if (frame->draw_layer.empty() && !templ.draw_layer.empty()) {
    frame->draw_layer = templ.draw_layer;
  }
  if (frame->draw_sublevel == 0 && templ.draw_sublevel != 0) {
    frame->draw_sublevel = templ.draw_sublevel;
  }
  if (frame->quest_poi_fill_texture.empty() && !templ.quest_poi_fill_texture.empty()) {
    frame->quest_poi_fill_texture = templ.quest_poi_fill_texture;
  }
  if (frame->quest_poi_border_texture.empty() && !templ.quest_poi_border_texture.empty()) {
    frame->quest_poi_border_texture = templ.quest_poi_border_texture;
  }
  if (!frame->visibility_explicit && templ.visibility_explicit) {
    frame->visible = templ.visible;
    frame->visibility_explicit = true;
  } else if (!frame->visibility_explicit && !templ.visibility_explicit &&
             frame->visible && !templ.visible) {
    frame->visible = false;
  }
  if (!frame->protected_explicit && templ.protected_explicit) {
    frame->protected_frame = templ.protected_frame;
    frame->protected_explicit = true;
  } else if (!frame->protected_explicit && !templ.protected_explicit &&
             !frame->protected_frame && templ.protected_frame) {
    frame->protected_frame = true;
  }
  if (!frame->toplevel_explicit && templ.toplevel_explicit) {
    frame->toplevel = templ.toplevel;
    frame->toplevel_explicit = true;
  } else if (!frame->toplevel_explicit && !templ.toplevel_explicit &&
             !frame->toplevel && templ.toplevel) {
    frame->toplevel = true;
  }
}

void ResolveInheritance(std::vector<UiFrame> *frames) {
  std::unordered_map<std::string, std::size_t> index_by_name;
  index_by_name.reserve(frames->size());
  for (std::size_t i = 0; i < frames->size(); ++i) {
    index_by_name.insert_or_assign((*frames)[i].name, i);
  }
  std::vector<bool> resolved(frames->size(), false);

  std::function<void(std::size_t, std::unordered_set<std::string> *)> apply_inherit;
  apply_inherit = [&](std::size_t idx, std::unordered_set<std::string> *stack) {
    if (resolved[idx]) {
      return;
    }
    auto &frame = (*frames)[idx];
    if (frame.inherits.empty()) {
      resolved[idx] = true;
      return;
    }
    if (stack->find(frame.name) != stack->end()) {
      return;
    }
    stack->insert(frame.name);
    const auto direct_attributes = frame.initial_attributes;
    std::vector<UiFrame::InitialAttribute> inherited_attributes;
    const auto inherits_list = SplitTemplateList(frame.inherits, TemplateListSyntax::kCommaSeparated);
    for (const auto &templ_name : inherits_list) {
      const auto template_it = index_by_name.find(templ_name);
      if (template_it == index_by_name.end()) {
        continue;
      }
      apply_inherit(template_it->second, stack);
    }
    for (auto templ_name = inherits_list.rbegin();
         templ_name != inherits_list.rend(); ++templ_name) {
      const auto template_it = index_by_name.find(*templ_name);
      if (template_it != index_by_name.end()) {
        MergeFromTemplate(&frame, (*frames)[template_it->second]);
      }
    }
    for (const auto &templ_name : inherits_list) {
      const auto template_it = index_by_name.find(templ_name);
      if (template_it == index_by_name.end()) continue;
      const auto &template_attributes = (*frames)[template_it->second].initial_attributes;
      inherited_attributes.insert(inherited_attributes.end(), template_attributes.begin(),
                                  template_attributes.end());
    }
    if (!inherited_attributes.empty()) {
      inherited_attributes.insert(inherited_attributes.end(), direct_attributes.begin(),
                                  direct_attributes.end());
      frame.initial_attributes = std::move(inherited_attributes);
    }
    stack->erase(frame.name);
    resolved[idx] = true;
  };

  for (std::size_t i = 0; i < frames->size(); ++i) {
    std::unordered_set<std::string> stack;
    apply_inherit(i, &stack);
  }
}

void DumpUnresolvedLayoutTrace(const std::span<const UiFrame *const> frames,
                               const ResolvedRectMap &ddc) {
  std::size_t unresolved_count = 0;
  for (const UiFrame *const frame : frames) {
    if (frame != nullptr && !frame->name.empty() &&
        !ddc.contains(frame->name)) {
      ++unresolved_count;
    }
  }
  if (unresolved_count != 0) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                       "Layout unresolved frames: " +
                           std::to_string(unresolved_count));
    std::size_t dumped = 0;
    for (const UiFrame *const frame_ptr : frames) {
      if (dumped >= 50)
        break;
      if (frame_ptr == nullptr || frame_ptr->name.empty())
        continue;
      const UiFrame &frame = *frame_ptr;
      if (ddc.contains(frame.name))
        continue;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kTrace,
          "Unresolved frame: name=" + frame.name + " kind=" + frame.kind +
              " parent=" + frame.parent + " anchors=" + std::to_string(frame.anchors.size()) +
              " w=" + std::to_string(frame.width.value_or(0)) +
              " h=" + std::to_string(frame.height.value_or(0)) +
              " setAllPoints=" + std::string(frame.set_all_points ? "true" : "false"));
      for (const auto &a : frame.anchors) {
        const std::string point{EffectiveAnchorPoint(a)};
        const std::string rel_point{EffectiveAnchorRelativePoint(a)};
        const std::string rel_to = !a.relative_to.empty()
                                       ? a.relative_to
                                       : (!frame.parent.empty() ? frame.parent : "UIParent");
        const bool rel_ready = ddc.find(rel_to) != ddc.end();
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                           "  anchor point=" + point + " relTo=" + rel_to +
                               " relPoint=" + rel_point + " dx=" + std::to_string(a.x) +
                               " dy=" + std::to_string(a.y) +
                               " relReady=" + std::string(rel_ready ? "true" : "false"));
      }
      ++dumped;
    }
  }
}

FrameRect FrameRectFromDdc(const RectDDC &rect, const int viewport_height) {
  const int x0 = static_cast<int>(std::round(rect.min_x));
  const int x1 = static_cast<int>(std::round(rect.max_x));
  const int y0 = viewport_height - static_cast<int>(std::round(rect.max_y));
  const int y1 = viewport_height - static_cast<int>(std::round(rect.min_y));
  return FrameRect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void SolveExpandedLayout(
    const std::span<const UiFrame *const> frames, const int viewport_width,
    const int viewport_height, const float ui_scale,
    ResolvedRectMap *const ddc_out,
    std::vector<std::optional<FrameRect>> *const positional_rects) {
  if (positional_rects != nullptr) {
    positional_rects->assign(frames.size(), std::nullopt);
  }

  ResolvedRectMap &ddc = *ddc_out;
  ddc.reserve(frames.size() + 1);

  ddc.insert_or_assign("UIParent", RectDDC{
                                       .min_x = 0.0F,
                                       .min_y = 0.0F,
                                       .max_x = static_cast<float>(viewport_width),
                                       .max_y = static_cast<float>(viewport_height),
                                   });

  std::unordered_map<std::string_view, std::size_t> index_by_name;
  index_by_name.reserve(frames.size() * 2);

  bool names_are_unique = true;
  for (std::size_t index = 0; index < frames.size(); ++index) {
    const UiFrame *const frame = frames[index];
    if (frame != nullptr && !frame->name.empty()) {
      names_are_unique &= index_by_name.insert_or_assign(frame->name, index).second;
    }
  }

  const auto compute_effective_scale_legacy = [&](std::string_view name) -> float {
    float eff = ui_scale;
    std::string_view current = name;
    constexpr int kMaxDepth = 64;
    for (int depth = 0; depth < kMaxDepth && !current.empty(); ++depth) {
      const auto frame_it = index_by_name.find(current);
      if (frame_it == index_by_name.end()) {
        break;
      }
      const UiFrame *const frame = frames[frame_it->second];
      if (frame == nullptr) {
        break;
      }
      eff *= frame->scale;
      current = frame->parent;
    }
    return eff;
  };

  enum class ScaleResolveState : std::uint8_t {
    Pending,
    Visiting,
    Complete,
    LegacyFallback,
  };
  std::vector<ScaleResolveState> scale_states(
      frames.size(), ScaleResolveState::Pending);
  std::vector<float> effective_scales(frames.size(), ui_scale);

  const auto resolve_effective_scale = [&](auto &&self, const std::size_t index,
                                           const int depth) -> bool {
    if (index >= frames.size() || frames[index] == nullptr) {
      return true;
    }
    if (scale_states[index] == ScaleResolveState::Complete) {
      return true;
    }
    if (scale_states[index] == ScaleResolveState::LegacyFallback) {
      return false;
    }
    if (scale_states[index] == ScaleResolveState::Visiting || depth >= 64) {
      scale_states[index] = ScaleResolveState::LegacyFallback;
      effective_scales[index] =
          compute_effective_scale_legacy(frames[index]->name);
      return false;
    }

    scale_states[index] = ScaleResolveState::Visiting;
    const UiFrame& frame = *frames[index];
    float parent_scale = ui_scale;
    if (!frame.parent.empty()) {
      if (const auto parent = index_by_name.find(frame.parent);
          parent != index_by_name.end()) {
        if (!self(self, parent->second, depth + 1)) {
          scale_states[index] = ScaleResolveState::LegacyFallback;
          effective_scales[index] =
              compute_effective_scale_legacy(frame.name);
          return false;
        }
        parent_scale = effective_scales[parent->second];
      }
    }
    effective_scales[index] = parent_scale * frame.scale;
    scale_states[index] = ScaleResolveState::Complete;
    return true;
  };
  for (std::size_t index = 0; index < frames.size(); ++index) {
    (void)resolve_effective_scale(resolve_effective_scale, index, 0);
  }

  const auto compute_effective_scale = [&](const std::string_view name) {
    if (const auto frame = index_by_name.find(name);
        frame != index_by_name.end()) {
      return effective_scales[frame->second];
    }
    return ui_scale;
  };

  enum class ResolveState : std::uint8_t { Pending, Visiting, Complete };
  std::vector<ResolveState> states(frames.size(), ResolveState::Pending);

  std::vector<bool> produced_rect(frames.size(), false);

  const auto resolve_frame = [&](auto &&self, const std::size_t index) -> bool {
    if (index >= frames.size() || frames[index] == nullptr ||
        frames[index]->name.empty()) {
      return false;
    }

    const UiFrame &frame = *frames[index];
    if (states[index] == ResolveState::Complete) {
      return names_are_unique ? produced_rect[index] : ddc.contains(frame.name);
    }
    if (states[index] == ResolveState::Visiting) {
      return false;
    }
    states[index] = ResolveState::Visiting;

    const auto resolve_dependency = [&](const std::string_view dependency) {
      if (dependency.empty() || dependency == "UIParent" ||
          dependency == frame.name) {
        return;
      }
      if (const auto dependency_it = index_by_name.find(dependency);
          dependency_it != index_by_name.end()) {
        (void)self(self, dependency_it->second);
      }
    };

    resolve_dependency(frame.parent);
    for (const auto &anchor : frame.anchors) {
      resolve_dependency(!anchor.relative_to.empty()
                             ? std::string_view{anchor.relative_to}
                             : std::string_view{frame.parent});
    }

    const auto rect = ResolveRectDDC(
        frame, ddc, static_cast<float>(viewport_width),
        static_cast<float>(viewport_height),
        names_are_unique ? effective_scales[index]
                         : compute_effective_scale(frame.name));
    if (rect.has_value()) {
      ddc.insert_or_assign(frame.name, *rect);
      if (positional_rects != nullptr) {
        (*positional_rects)[index] = FrameRectFromDdc(*rect, viewport_height);
      }
    }
    states[index] = ResolveState::Complete;
    produced_rect[index] = rect.has_value();
    return rect.has_value();
  };

  for (std::size_t index = 0; index < frames.size(); ++index) {
    (void)resolve_frame(resolve_frame, index);
  }

  if (ShouldDumpLayoutTrace()) {
    DumpUnresolvedLayoutTrace(frames, ddc);
  }
}

}

void ResolveExpandedLayoutInto(
    const std::span<const UiFrame *const> frames, const int viewport_width,
    const int viewport_height, const float ui_scale,
    std::vector<std::optional<FrameRect>> *const out_rects) {
  if (out_rects == nullptr) {
    return;
  }
  ResolvedRectMap ddc;
  SolveExpandedLayout(frames, viewport_width, viewport_height, ui_scale, &ddc,
                      out_rects);
}

openwow::ui::TransparentStringMap<FrameRect>
ResolveExpandedLayout(const std::span<const UiFrame *const> frames,
                      const int viewport_width, const int viewport_height,
                      const float ui_scale) {
  ResolvedRectMap ddc;
  SolveExpandedLayout(frames, viewport_width, viewport_height, ui_scale, &ddc,
                      nullptr);

  openwow::ui::TransparentStringMap<FrameRect> out;
  out.reserve(frames.size() + 1);
  for (const auto &[name, rect] : ddc) {
    if (name.empty()) {
      continue;
    }
    out.insert_or_assign(std::string(name),
                         FrameRectFromDdc(rect, viewport_height));
  }
  return out;
}

openwow::ui::TransparentStringMap<FrameRect> ResolveLayout(
    const std::vector<UiFrame> &frames, const int viewport_width,
    const int viewport_height, const float ui_scale) {

  auto expanded = frames;
  ResolveInheritance(&expanded);
  std::vector<const UiFrame *> expanded_frames;
  expanded_frames.reserve(expanded.size());
  for (const auto &frame : expanded) {
    expanded_frames.push_back(&frame);
  }
  return ResolveExpandedLayout(expanded_frames, viewport_width,
                               viewport_height, ui_scale);
}

std::vector<UiFrame> SortByRenderOrder(const std::vector<UiFrame> &frames) {
  std::vector<UiFrame> sorted = frames;
  std::sort(sorted.begin(), sorted.end(), [](const UiFrame &a, const UiFrame &b) {
    const int strata_a = StrataOrder(a.frame_strata);
    const int strata_b = StrataOrder(b.frame_strata);
    if (strata_a != strata_b) {
      return strata_a < strata_b;
    }
    if (a.frame_level != b.frame_level) {
      return a.frame_level < b.frame_level;
    }
    return a.name < b.name;
  });
  return sorted;
}

}
