#include "openwow/ui/glue/glue_font_metrics.h"

#include "openwow/ui/font_layout.h"
#include "openwow/ui/font_string_layout.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::glue {

using openwow::text::Trim;

namespace {

std::vector<std::string> SplitCsv(const std::string& raw) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : raw) {
    if (ch == ',') {
      out.push_back(Trim(cur));
      cur.clear();
      continue;
    }
    cur.push_back(ch);
  }
  out.push_back(Trim(cur));
  out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& s) { return s.empty(); }),
            out.end());
  return out;
}

openwow::render::text::WrapMode ResolveMeasurementWrapMode(
    const GlueWidgetState& widget,
    const GlueResolvedFontStringStyle& style,
    const bool width_intrinsic) {
  if (width_intrinsic) {
    return openwow::render::text::WrapMode::None;
  }
  return openwow::render::text::ResolveWrapMode(widget.word_wrap,
                                                style.non_space_wrap);
}

}

GlueResolvedFontStringStyle ResolveGlueFontStringStyle(
    const GlueFontRegistry* registry,
    const GlueWidgetState& widget) {
  GlueResolvedFontStringStyle resolved;
  resolved.font.font_file = "/Fonts/FRIZQT__.TTF";
  resolved.font.height_px = 14;

  const std::string style_tokens =
      widget.font_style.empty() ? widget.inherits : widget.font_style;
  if (registry != nullptr) {
    for (const auto& token : SplitCsv(style_tokens)) {
      const auto candidate = registry->Resolve(token);
      if (!candidate.has_value() || candidate->font_file.empty() ||
          candidate->height_px <= 0) {
        continue;
      }
      resolved.font = *candidate;
      resolved.has_bound_font = true;
      break;
    }
  }

  resolved.line_spacing_px =
      widget.text_spacing_stored != 0.0F
          ? openwow::ui::StoredUiHorizontalCoordinateToPixels(
                widget.text_spacing_stored)
          : resolved.font.spacing_px;
  resolved.non_space_wrap =
      widget.non_space_wrap || resolved.font.non_space_wrap;
  resolved.indented_word_wrap =
      widget.indented_word_wrap || resolved.font.indented_word_wrap;
  return resolved;
}

std::uint64_t BuildGlueTextLayoutCacheKey(
    const openwow::vfs::VirtualFileSystem& vfs,
    const std::string_view font_file, const int font_height_px,
    const openwow::render::text::TextLayoutRequest& request,
    const float render_scale) noexcept {

  std::uint64_t hash = 1469598103934665603ULL;
  const auto add_byte = [&hash](const std::uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };
  const auto add_u32 = [&add_byte](const std::uint32_t value) {
    add_byte(static_cast<std::uint8_t>(value));
    add_byte(static_cast<std::uint8_t>(value >> 8U));
    add_byte(static_cast<std::uint8_t>(value >> 16U));
    add_byte(static_cast<std::uint8_t>(value >> 24U));
  };
  const auto add_u64 = [&add_byte](const std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
      add_byte(static_cast<std::uint8_t>(value >> shift));
    }
  };
  add_u64(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&vfs)));
  for (const char ch : font_file) {
    add_byte(static_cast<std::uint8_t>(ch));
  }
  add_byte(0);
  add_u32(static_cast<std::uint32_t>(font_height_px));
  add_u32(std::bit_cast<std::uint32_t>(request.maximum_width));
  add_u32(std::bit_cast<std::uint32_t>(request.maximum_height));
  add_u32(std::bit_cast<std::uint32_t>(request.line_spacing));
  add_u32(std::bit_cast<std::uint32_t>(request.line_height));
  add_u32(std::bit_cast<std::uint32_t>(request.inline_image_scale));
  add_u32(request.maximum_lines);
  add_byte(static_cast<std::uint8_t>(request.wrap));
  add_byte(request.indent_continuation_lines ? 1U : 0U);
  add_u32(std::bit_cast<std::uint32_t>(render_scale));
  return hash;
}

GlueFontCoordinateSpace ResolveGlueFontCoordinateSpace(
    const GlueWidgetRuntime& runtime,
    const std::string& widget_name) {
  const auto positive_or_one = [](const float value) {
    return std::isfinite(value) && value > 0.000001F ? value : 1.0F;
  };
  return GlueFontCoordinateSpace{
      .viewport_scale = positive_or_one(runtime.ndc_to_pixel()),
      .widget_scale = positive_or_one(runtime.GetEffectiveScale(widget_name)),
  };
}

int ApplyFontStringIntrinsicSizes(GlueWidgetRuntime* runtime,
                                  const openwow::vfs::VirtualFileSystem& vfs,
                                  const GlueFontRegistry& fonts) {
  if (runtime == nullptr) {
    return 0;
  }

  int updated = 0;
  for (const auto& name : runtime->ConsumeDirtyFontStringMetricNames()) {
    const auto widget = runtime->GetWidget(name);
    if (!widget.has_value() || widget->kind != "FontString") {
      continue;
    }

    const auto* frame = runtime->GetLayoutFrameDefinition(name);
    const bool width_intrinsic =
        frame == nullptr || !openwow::ui::FontStringWidthComesFromLayout(*frame);
    const bool height_intrinsic =
        frame == nullptr || !openwow::ui::FontStringHeightComesFromLayout(*frame);
    const bool had_intrinsic =
        frame != nullptr && (frame->font_intrinsic_width || frame->font_intrinsic_height);
    if (!width_intrinsic && !height_intrinsic && !had_intrinsic) {
      continue;
    }

    if (widget->text.empty()) {
      if (runtime->ClearFontStringIntrinsicSize(name)) {
        ++updated;
      }
      continue;
    }

    const auto style = ResolveGlueFontStringStyle(&fonts, *widget);

    const auto coordinates = ResolveGlueFontCoordinateSpace(*runtime, name);
    openwow::render::text::TextLayoutRequest request;
    request.maximum_width =
        width_intrinsic
            ? 0.0F
            : coordinates.ScreenPixelsToScriptUnits(
                  static_cast<float>(std::max(0, widget->width)));
    request.maximum_height =
        height_intrinsic
            ? 0.0F
            : coordinates.ScreenPixelsToScriptUnits(
                  static_cast<float>(std::max(0, widget->height)));
    request.line_spacing = style.line_spacing_px;
    request.line_height =
        openwow::ui::StoredUiHorizontalCoordinateToPixels(widget->text_height_stored);
    request.maximum_lines = static_cast<std::uint32_t>(std::max(0, widget->max_lines));
    request.wrap =
        ResolveMeasurementWrapMode(*widget, style, width_intrinsic);
    request.indent_continuation_lines = style.indented_word_wrap;

    const std::uint64_t cache_key = BuildGlueTextLayoutCacheKey(
        vfs, style.font.font_file, style.font.height_px, request,
        coordinates.render_scale());
    GlueTextExtent extent;
    bool laid_out_now = false;
    if (const auto cached =
            runtime->GetCachedTextExtent(name, cache_key);
        cached.has_value()) {
      extent = *cached;
    } else {
      const auto layout = openwow::ui::LayoutFontText(
          &vfs, style.font.font_file, style.font.height_px, widget->text,
          request, coordinates.render_scale());
      if (!layout) continue;
      extent = {.width = layout->width, .height = layout->height};
      laid_out_now = true;
    }
    if (extent.width <= 0.0F || extent.height <= 0.0F) {
      continue;
    }
    runtime->CacheTextExtent(name, cache_key, extent,
                             true, laid_out_now);

    const float next_w =
        width_intrinsic
            ? openwow::ui::ResolveFontStringEffectiveScriptDimension(
                  0.0F, extent.width)
            : static_cast<float>(widget->width);
    const float next_h =
        height_intrinsic
            ? openwow::ui::ResolveFontStringEffectiveScriptDimension(
                  0.0F, extent.height)
            : static_cast<float>(widget->height);
    if (runtime->SetFontStringIntrinsicSize(name, next_w, next_h, width_intrinsic,
                                            height_intrinsic)) {
      ++updated;
    }

    if (height_intrinsic && widget->parent.size() < name.size() &&
        name.ends_with(".__HTMLContent1")) {
      const auto html = runtime->GetWidget(widget->parent);
      if (html.has_value() &&
          openwow::text::EqualsIgnoreCaseAscii(html->kind, "SimpleHTML")) {
        const auto scroll = runtime->GetWidget(html->parent);
        if (scroll.has_value() &&
            openwow::text::EqualsIgnoreCaseAscii(scroll->kind, "ScrollFrame")) {

          runtime->SetHeight(html->name, next_h);
          const float content_height_pixels =
              coordinates.ScriptUnitsToScreenPixels(extent.height);
          const double range = std::max(
              0.0, static_cast<double>(content_height_pixels - scroll->height));
          runtime->SetVerticalScrollRange(scroll->name, range);
          runtime->SetMinMaxValues(scroll->name + "ScrollBar", 0.0, range);
          for (const char* suffix : {"ScrollBarScrollUpButton", "ScrollBarScrollDownButton"}) {
            const std::string button_name = scroll->name + suffix;
            if (runtime->GetWidget(button_name).has_value()) {
              runtime->SetEnabled(button_name, range > 0.0);
            }
          }
        }
      }
    }
  }

  return updated;
}

GlueLayoutUpdateStats ResolveGlueLayoutAndFontMetrics(
    GlueWidgetRuntime* runtime,
    const openwow::vfs::VirtualFileSystem& vfs,
    const GlueFontRegistry* fonts,
    const int viewport_width,
    const int viewport_height) {
  GlueLayoutUpdateStats stats;
  if (runtime == nullptr) {
    return stats;
  }

  const std::uint64_t resolves_before = runtime->layout_resolve_count();

  runtime->ResolveLayout(viewport_width, viewport_height);
  if (fonts != nullptr && runtime->HasDirtyFontStringMetrics()) {
    stats.intrinsic_size_updates =
        ApplyFontStringIntrinsicSizes(runtime, vfs, *fonts);

    runtime->ResolveLayout(viewport_width, viewport_height);
  }

  stats.full_layout_resolves =
      runtime->layout_resolve_count() - resolves_before;
  return stats;
}

}
