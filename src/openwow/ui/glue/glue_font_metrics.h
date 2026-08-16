#pragma once

#include "openwow/ui/glue/glue_font_registry.h"
#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/render/resources/fonts/font_string_flags.h"
#include "openwow/render/resources/fonts/text_layout.h"
#include "openwow/ui/ui_coordinate_space.h"
#include "openwow/vfs/virtual_file_system.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::ui::glue {

struct GlueFontCoordinateSpace {
  float viewport_scale{1.0F};
  float widget_scale{1.0F};

  [[nodiscard]] float render_scale() const noexcept {
    return openwow::ui::ResolveDevicePixelsPerUiUnit(
               viewport_scale * openwow::ui::kUiScriptScreenUnitHeight,
               widget_scale)
        .value;
  }

  [[nodiscard]] float ScreenPixelsToScriptUnits(float pixels) const noexcept {
    return pixels / render_scale();
  }

  [[nodiscard]] float ScriptUnitsToScreenPixels(float units) const noexcept {
    return units * render_scale();
  }

  [[nodiscard]] int RasterFontPixelHeight(float script_height_units) const noexcept {
    return std::max(1, openwow::render::ClampRetailFontPixelHeight(
                           static_cast<int>(std::lround(
                               ScriptUnitsToScreenPixels(script_height_units)))));
  }
};

struct GlueResolvedFontStringStyle {
  GlueFontStyle font;
  float line_spacing_px{0.0F};
  bool non_space_wrap{false};
  bool indented_word_wrap{false};
  bool has_bound_font{false};
};

struct GlueLayoutUpdateStats {
  std::uint64_t full_layout_resolves{0};
  int intrinsic_size_updates{0};
};

[[nodiscard]] GlueResolvedFontStringStyle ResolveGlueFontStringStyle(
    const GlueFontRegistry* registry,
    const GlueWidgetState& widget);

[[nodiscard]] GlueFontCoordinateSpace ResolveGlueFontCoordinateSpace(
    const GlueWidgetRuntime& runtime,
    const std::string& widget_name);

[[nodiscard]] std::uint64_t BuildGlueTextLayoutCacheKey(
    const openwow::vfs::VirtualFileSystem& vfs,
    std::string_view font_file,
    int font_height_px,
    const openwow::render::text::TextLayoutRequest& request,
    float render_scale) noexcept;

int ApplyFontStringIntrinsicSizes(GlueWidgetRuntime* runtime,
                                  const openwow::vfs::VirtualFileSystem& vfs,
                                  const GlueFontRegistry& fonts);

[[nodiscard]] GlueLayoutUpdateStats ResolveGlueLayoutAndFontMetrics(
    GlueWidgetRuntime* runtime,
    const openwow::vfs::VirtualFileSystem& vfs,
    const GlueFontRegistry* fonts,
    int viewport_width,
    int viewport_height);

}
