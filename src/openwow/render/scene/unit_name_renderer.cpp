
#include "openwow/render/scene/unit_name_renderer.h"

#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/resources/fonts/text_layout.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace openwow::render {

namespace {

constexpr float kMinLinePixelHeight = 1.0f;

constexpr float kMaxLinePixelHeight = 512.0f;

struct ProjectedPoint {
  float sx;
  float sy;
  float w;

  float ndc_z;
};

bool ProjectWorldPoint(const float wx, const float wy, const float wz,
                       const float* view_mtx, const float* proj_mtx,
                       const WorldOverlayMetrics& metrics,
                       ProjectedPoint& out) {
  if (view_mtx == nullptr || proj_mtx == nullptr) {
    return false;
  }
  const RenderVec4 world{wx, wy, wz, 1.0f};
  const RenderVec4 view =
      TransformRowVector4x4(world, RenderMatrix4x4View{view_mtx, 16u});
  const RenderVec4 clip =
      TransformRowVector4x4(view, RenderMatrix4x4View{proj_mtx, 16u});
  if (clip[3] <= 0.0f) {
    return false;
  }
  const float inv_w = 1.0f / clip[3];
  const auto screen = metrics.NdcToFramebuffer(clip[0] * inv_w,
                                               clip[1] * inv_w);
  out = {screen.x, screen.y, clip[3], clip[2] * inv_w};
  return true;
}

}

bool UnitNameRenderer::Initialize() {

  static_cast<void>(text_renderer_.WarmWorldDepthProgram());
  initialized_ = true;
  return true;
}

void UnitNameRenderer::Shutdown() {
  text_renderer_.Shutdown();
  entries_.clear();
  line_layout_cache_.clear();
  initialized_ = false;
}

bool UnitNameRenderer::EnsureFont() {
  if (text_renderer_.is_ready()) {
    return true;
  }

  if (text_renderer_.InitFromVirtualPath(kUnitNameFontPath,
                                         kBaseFontPixelHeight)) {
    return true;
  }
  return text_renderer_.Init(kBaseFontPixelHeight);
}

void UnitNameRenderer::ConsumePresentation(
    UnitNamePresentationSnapshot snapshot) {
  entries_ = std::move(snapshot.names);
}

void UnitNameRenderer::Render(const std::uint8_t view_id,
                              const WorldOverlayMetrics& metrics,
                              const float* view_mtx, const float* proj_mtx) {
  if (!initialized_ || entries_.empty()) {
    return;
  }
  const float screen_w = metrics.framebuffer_width;
  const float screen_h = metrics.framebuffer_height;
  if (screen_w <= 0.0f || screen_h <= 0.0f || !EnsureFont()) {
    return;
  }

  const float proj_y_scale = proj_mtx != nullptr ? proj_mtx[5] : 0.0f;
  if (proj_y_scale <= 0.0f) {
    return;
  }

  if (!text_renderer_.BeginWorldDepthFrame(view_id, screen_w, screen_h)) {
    return;
  }

  constexpr std::size_t kMaxCachedLineLayouts = 512u;
  if (line_layout_cache_.size() > kMaxCachedLineLayouts) {
    line_layout_cache_.clear();
  }

  for (const auto& entry : entries_) {
    const std::uint32_t alpha = entry.color_argb >> 24u;
    if (alpha == 0u) {

      continue;
    }
    const float line_world_height = kWorldLineHeightPerScale * entry.scale;

    const float top_z =
        entry.world_z +
        line_world_height * static_cast<float>(std::max(entry.lines, 1u));

    ProjectedPoint anchor{};
    if (!ProjectWorldPoint(entry.world_x, entry.world_y, top_z, view_mtx,
                           proj_mtx, metrics, anchor)) {
      continue;
    }
    const float line_px =
        line_world_height * proj_y_scale / anchor.w * (screen_h * 0.5f);
    if (line_px < kMinLinePixelHeight || line_px > kMaxLinePixelHeight) {
      continue;
    }
    const float glyph_scale =
        line_px / static_cast<float>(kBaseFontPixelHeight);
    const float text_alpha = static_cast<float>(alpha) / 255.0f;
    const std::uint32_t color = entry.color_argb | 0xFF000000u;

    text_renderer_.SetWorldDepth(anchor.ndc_z);

    std::string_view remaining(entry.text);
    float line_y = anchor.sy;
    while (!remaining.empty()) {
      const auto newline = remaining.find('\n');
      const std::string line(remaining.substr(0, newline));
      remaining = newline == std::string_view::npos
                      ? std::string_view{}
                      : remaining.substr(newline + 1);
      if (!line.empty()) {

        auto cached = line_layout_cache_.find(line);
        if (cached == line_layout_cache_.end()) {
          auto layout = text_renderer_.LayoutPlainText(line);
          if (layout == nullptr) {
            line_y += line_px;
            continue;
          }
          cached = line_layout_cache_.emplace(line, std::move(layout)).first;
        }
        text_renderer_.DrawTextCentered(view_id, anchor.sx, line_y,
                                        *cached->second, color, text_alpha,
                                        glyph_scale);
      }
      line_y += line_px;
    }
  }
  text_renderer_.EndWorldDepthFrame();
}

}
