#include "openwow/render/scene/nameplate_renderer.h"

#include "openwow/game/trivial_level.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/ui/game/nameplate_position_2d.h"
#include "openwow/ui/ui_anchor_bfs.h"
#include "openwow/ui/ui_aspect_scales.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace openwow::render {

namespace {

constexpr std::uint32_t kFriendlyPlayerColorArgb = 0xFF0000FFu;
constexpr std::uint32_t kNeutralOrCivilianColorArgb = 0xFFFFFF00u;
constexpr std::uint32_t kFriendlyNpcColorArgb = 0xFF00FF00u;
constexpr std::uint32_t kHostileColorArgb = 0xFFFF0000u;
constexpr float kNameplateSortAnchorX = 0.4f;
constexpr float kNameplateSortAnchorY = 0.3f;
constexpr std::uint32_t kClassColorArgbById[] = {
    0xFF000000u,
    0xFFC69B6Du,
    0xFFF48CBAu,
    0xFFAAD372u,
    0xFFFFF468u,
    0xFFFFFFFFu,
    0xFFC41E3Au,
    0xFF0070DDu,
    0xFF68CCEFu,
    0xFF9382C9u,
    0xFF000000u,
    0xFFFF7C0Au,
};

}

bool NameplateRenderer::Initialize() {
  if (initialized_) return true;
  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "NameplateRenderer: initialized");
  return true;
}

void NameplateRenderer::Shutdown() {
  if (!initialized_) return;
  nameplates_.ClearAll();
  openwow::ui::NameplateFrameChannel::Get().Reset();
  initialized_ = false;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "NameplateRenderer: shutdown");
}

void NameplateRenderer::ConsumePresentation(
    NameplatePresentationSnapshot presentation) {
  target_guid_ = presentation.target;
  nameplates_.ReplaceSnapshot(std::move(presentation.nameplates));
}

std::vector<NameplateInfo> NameplateRenderer::GetVisibleNameplatesSnapshot() const {
  const auto snapshot = nameplates_.AcquireSnapshot();
  return *snapshot;
}

std::vector<NameplateDrawInfo> NameplateRenderer::BuildDrawList(
    const WorldOverlayMetrics& metrics, const float* view_mtx,
    const float* proj_mtx) const {
  const float screen_w = metrics.framebuffer_width;
  const float screen_h = metrics.framebuffer_height;
  const auto geometry = ResolvePixelGeometry(metrics);
  std::vector<NameplateDrawInfo> draw_list;
  if (screen_w <= 0.0f || screen_h <= 0.0f || view_mtx == nullptr ||
      proj_mtx == nullptr) {
    return draw_list;
  }

  float camera_x = 0.0f;
  float camera_y = 0.0f;
  float camera_z = 0.0f;
  const bool have_camera =
      ExtractCameraWorldPosition(view_mtx, camera_x, camera_y, camera_z);

  const auto nameplates = nameplates_.AcquireSnapshot();
  draw_list.reserve(nameplates->size());
  for (const auto& np : *nameplates) {
    float sx = 0.0f;
    float sy = 0.0f;
    const bool anchor_projected = WorldToScreen(
        np.world_x, np.world_y, np.world_z, view_mtx, proj_mtx, metrics, sx,
        sy);
    if (!anchor_projected && !np.has_world_bounds) {
      continue;
    }

    float projected_min_x = sx;
    float projected_max_x = sx;
    float projected_min_y = sy;
    float projected_max_y = sy + geometry.frame_height;
    if (np.has_world_bounds) {
      bool projected_corner = false;
      projected_min_x = std::numeric_limits<float>::infinity();
      projected_min_y = std::numeric_limits<float>::infinity();
      projected_max_x = -std::numeric_limits<float>::infinity();
      projected_max_y = -std::numeric_limits<float>::infinity();
      for (std::uint32_t corner = 0u; corner < 8u; ++corner) {
        const float x = (corner & 1u) != 0u ? np.world_bounds[3]
                                            : np.world_bounds[0];
        const float y = (corner & 2u) != 0u ? np.world_bounds[4]
                                            : np.world_bounds[1];
        const float z = (corner & 4u) != 0u ? np.world_bounds[5]
                                            : np.world_bounds[2];
        float corner_x = 0.0f;
        float corner_y = 0.0f;
        if (!WorldToScreen(x, y, z, view_mtx, proj_mtx, metrics, corner_x,
                           corner_y)) {
          continue;
        }
        projected_corner = true;
        projected_min_x = std::min(projected_min_x, corner_x);
        projected_max_x = std::max(projected_max_x, corner_x);
        projected_min_y = std::min(projected_min_y, corner_y);
        projected_max_y = std::max(projected_max_y, corner_y);
      }
      if (!projected_corner) {
        continue;
      }
      if (!anchor_projected) {
        sx = (projected_min_x + projected_max_x) * 0.5f;
        sy = projected_min_y;
      }
    }
    if (projected_max_x < 0.0f || projected_min_x > screen_w ||
        projected_max_y < 0.0f || projected_min_y > screen_h) {
      continue;
    }

    float camera_distance = 8.0f;
    if (have_camera) {
      const float dx = np.world_x - camera_x;
      const float dy = np.world_y - camera_y;
      const float dz = np.world_z - camera_z;
      camera_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    const float normalized_x = sx / screen_w;
    const float normalized_y = sy / screen_h;
    const float sort_dx = normalized_x - kNameplateSortAnchorX;
    const float sort_dy = normalized_y - kNameplateSortAnchorY;

    float view_depth = 0.0f;
    if (view_mtx != nullptr) {
      const RenderVec4 anchor_world{np.world_x, np.world_y, np.world_z, 1.0f};
      const RenderVec4 anchor_view = TransformRowVector4x4(
          anchor_world, RenderMatrix4x4View{view_mtx, 16u});
      view_depth = anchor_view[2];
    }

    draw_list.push_back({
        np,
        sx,
        sy,
        sort_dx * sort_dx + sort_dy * sort_dy,
        camera_distance,
        view_depth,
        static_cast<std::uint8_t>(np.is_target ? 20u : 10u),
    });
  }

  const auto aspect = openwow::ui::ComputeUiAspectScaleState(
      metrics.aspect_ratio);
  std::vector<std::size_t> layout_order(draw_list.size());
  for (std::size_t index = 0; index < layout_order.size(); ++index) {
    layout_order[index] = index;
  }
  std::stable_sort(layout_order.begin(), layout_order.end(),
                   [&draw_list](const std::size_t lhs, const std::size_t rhs) {
                     if (draw_list[lhs].screen_sort_key !=
                         draw_list[rhs].screen_sort_key) {
                       return draw_list[lhs].screen_sort_key <
                              draw_list[rhs].screen_sort_key;
                     }
                     return draw_list[lhs].nameplate.guid <
                            draw_list[rhs].nameplate.guid;
                   });
  openwow::ui::ClearAnchorGrid(0);
  for (const auto index : layout_order) {
    auto& draw = draw_list[index];
    const auto position = openwow::ui::game::ComputeNameplatePosition2D(
        {
            .screen_x = draw.screen_x / screen_w * aspect.horizontal_scale,
            .screen_y = (screen_h - draw.screen_y) / screen_h *
                        aspect.vertical_scale,
            .frame_width = geometry.frame_width / screen_w *
                           aspect.horizontal_scale,
            .frame_height = geometry.frame_height / screen_h *
                            aspect.vertical_scale,
            .grid_index = 0,
        },
        aspect.horizontal_scale, aspect.vertical_scale);
    draw.screen_x = position.offset_x / aspect.horizontal_scale * screen_w;
    draw.screen_y = screen_h -
                    position.offset_y / aspect.vertical_scale * screen_h;
    if (allow_overlap_) {

      openwow::ui::ClearAnchorGrid(0);
    }
  }
  std::stable_sort(draw_list.begin(), draw_list.end(),
                   [](const NameplateDrawInfo& lhs,
                      const NameplateDrawInfo& rhs) {
                     if (lhs.frame_level != rhs.frame_level) {
                       return lhs.frame_level < rhs.frame_level;
                     }
                     if (lhs.screen_sort_key != rhs.screen_sort_key) {
                       return lhs.screen_sort_key > rhs.screen_sort_key;
                     }
                     return lhs.nameplate.guid < rhs.nameplate.guid;
                   });
  return draw_list;
}

void NameplateRenderer::PublishFrameLayout(
    const WorldOverlayMetrics& metrics, const float* view_mtx,
    const float* proj_mtx, const std::uint64_t compositor_generation) {
  openwow::ui::NameplateScreenLayout layout;
  layout.geometry = ResolvePixelGeometry(metrics);
  layout.framebuffer_width = metrics.framebuffer_width;
  layout.framebuffer_height = metrics.framebuffer_height;
  layout.ui_pixel_scale = metrics.ui_parent_effective_pixel_scale;
  layout.generation = compositor_generation;
  if (initialized_) {
    const auto draw_list = BuildDrawList(metrics, view_mtx, proj_mtx);
    layout.plates.reserve(draw_list.size());
    for (const auto& draw : draw_list) {
      layout.plates.push_back({
          .info = draw.nameplate,
          .screen_x = draw.screen_x,
          .screen_y = draw.screen_y,
          .camera_distance = draw.camera_distance,
          .view_depth = draw.view_depth,
          .frame_level = draw.frame_level,
      });
    }
  }
  openwow::ui::NameplateFrameChannel::Get().PublishLayout(std::move(layout));
}

bool NameplateRenderer::WorldToScreen(float wx, float wy, float wz,
                                       const float* view_mtx,
                                       const float* proj_mtx,
                                       const WorldOverlayMetrics& metrics,
                                       float& sx, float& sy) {
  if (view_mtx == nullptr || proj_mtx == nullptr) {
    return false;
  }

  const RenderVec4 world{wx, wy, wz, 1.0f};
  const RenderVec4 view = TransformRowVector4x4(
      world, RenderMatrix4x4View{view_mtx, 16u});
  const RenderVec4 clip = TransformRowVector4x4(
      view, RenderMatrix4x4View{proj_mtx, 16u});
  const float px = clip[0];
  const float py = clip[1];
  const float pw = clip[3];

  if (pw <= 0.0f) return false;

  const float inv_w = 1.0f / pw;
  const float ndc_x = px * inv_w;
  const float ndc_y = py * inv_w;

  const auto screen = metrics.NdcToFramebuffer(ndc_x, ndc_y);
  sx = screen.x;
  sy = screen.y;

  return true;
}

std::uint32_t NameplateRenderer::ResolveHealthBarColorArgb(
    const game::ReactionType reaction, const bool is_player,
    const bool is_trivial_level, const std::uint8_t class_id,
    const bool show_class_color_in_nameplate) {

  if (show_class_color_in_nameplate && is_player &&
      reaction <= game::ReactionType::kHostile) {
    if (class_id < std::size(kClassColorArgbById)) {
      return kClassColorArgbById[class_id];
    }
    return kFriendlyPlayerColorArgb;
  }

  if (reaction <= game::ReactionType::kHostile) {
    return kHostileColorArgb;
  }
  if (is_player) {
    return kFriendlyPlayerColorArgb;
  }
  return reaction > game::ReactionType::kNeutral || is_trivial_level
             ? kFriendlyNpcColorArgb
             : kNeutralOrCivilianColorArgb;
}

std::uint32_t NameplateRenderer::ResolveLevelColorArgb(
    const std::uint32_t player_level, const std::uint32_t unit_level) {

  constexpr std::uint32_t kRed = 0xFFFF1919u;
  constexpr std::uint32_t kOrange = 0xFFFF7F3Fu;
  constexpr std::uint32_t kYellow = 0xFFFFFF00u;
  constexpr std::uint32_t kGreen = 0xFF3FB23Fu;
  constexpr std::uint32_t kGray = 0xFF7F7F7Fu;
  const std::int64_t difference =
      static_cast<std::int64_t>(unit_level) -
      static_cast<std::int64_t>(player_level);
  if (difference >= 5) return kRed;
  if (difference >= 3) return kOrange;
  if (difference >= -2) return kYellow;

  const std::int64_t gray_range =
      game::GetTrivialLevelDifference(player_level);
  return -difference <= gray_range ? kGreen : kGray;
}

bool NameplateRenderer::ShouldShowLevel(
    const std::uint32_t player_level, const std::uint32_t unit_level,
    const game::ReactionType reaction, const bool is_boss) {
  if (is_boss) return false;
  if (reaction > game::ReactionType::kHostile) return true;

  return static_cast<std::uint64_t>(unit_level) <
         static_cast<std::uint64_t>(player_level) + 10u;
}

std::uint8_t NameplateRenderer::ResolveFrameAlpha(const bool has_target,
                                                   const bool is_target) {
  return !has_target || is_target ? 0xFFu : 0x7Fu;
}

std::uint32_t NameplateRenderer::ResolveThreatColorArgb(
    std::uint8_t status) {
  constexpr std::uint32_t kThreatColors[5] = {
      0xFFFFFFFFu, 0xFFB0B0B0u, 0xFFFFFF77u, 0xFFFF9900u, 0xFFFF0000u,
  };
  if (status >= std::size(kThreatColors)) status = 1u;
  return kThreatColors[status];
}

std::uint8_t NameplateRenderer::ResolveRaidTargetIconAlpha(
    const float distance_to_camera) {
  if (!std::isfinite(distance_to_camera) || distance_to_camera >= 8.0f) {
    return 0xFFu;
  }
  if (distance_to_camera <= 4.0f) {
    return 34u;
  }
  return static_cast<std::uint8_t>(
      (distance_to_camera - 4.0f) * 55.25f + 34.0f);
}

NameplatePixelGeometry NameplateRenderer::ResolvePixelGeometry(
    const WorldOverlayMetrics& metrics) {
  const auto aspect = openwow::ui::ComputeUiAspectScaleState(
      metrics.aspect_ratio);
  const float pixels_per_stored =
      aspect.kx * openwow::ui::kUiScriptCoordinateScale /
      aspect.horizontal_scale * metrics.ui_parent_effective_pixel_scale;
  const float frame_width = kFrameStoredWidth * pixels_per_stored;
  const float frame_height = kFrameStoredHeight * pixels_per_stored;
  const float health_width = frame_width * kCastBarWidthRatio;
  const float health_height = frame_height * kCastBarHeightRatio;
  const float health_x = frame_width * kStatusBarInsetXRatio;
  const float health_y = frame_height *
      (1.0f - kStatusBarOffsetYRatio - kCastBarHeightRatio);
  const float cast_width = frame_width * kCastBarWidthRatio;
  const float cast_height = frame_height * kCastBarHeightRatio;
  const float cast_border_y = frame_height * kCastBorderTopRatio;
  const float cast_x = frame_width *
      (1.0f - kStatusBarInsetXRatio - kCastBarWidthRatio);
  const float cast_y = cast_border_y + frame_height *
      (1.0f - kStatusBarOffsetYRatio - kCastBarHeightRatio);
  const float cast_icon_size = kCastIconStoredSize * pixels_per_stored;
  return {
      .frame_width = frame_width,
      .frame_height = frame_height,
      .threat_flash = {kThreatStoredOffsetX * pixels_per_stored,
                       kThreatStoredOffsetY * pixels_per_stored,
                       kThreatStoredWidth * pixels_per_stored,
                       kThreatStoredHeight * pixels_per_stored},
      .health_bar = {health_x, health_y, health_width, health_height},
      .cast_border = {0.0f, cast_border_y, frame_width, frame_height},
      .cast_bar = {cast_x, cast_y, cast_width, cast_height},
      .cast_icon = {frame_width * kCastIconAnchorXRatio -
                        cast_icon_size * 0.5f,
                    cast_border_y +
                        frame_height * (1.0f - kCastIconAnchorYRatio) -
                        cast_icon_size * 0.5f,
                    cast_icon_size, cast_icon_size},
      .elite_icon = {kEliteStoredOffsetX * pixels_per_stored,
                     kEliteStoredOffsetY * pixels_per_stored,
                     kEliteStoredWidth * pixels_per_stored,
                     kEliteStoredHeight * pixels_per_stored},
      .name_font_height = kNameFontStoredHeight * pixels_per_stored,
      .level_font_height = kLevelFontStoredHeight * pixels_per_stored,
      .cast_bar_width = cast_width,
      .cast_bar_height = cast_height,
      .cast_icon_size = cast_icon_size,
      .skull_size = kSkullStoredSize * pixels_per_stored,
      .elite_width = kEliteStoredWidth * pixels_per_stored,
      .elite_height = kEliteStoredHeight * pixels_per_stored,
      .raid_target_icon_size = kRaidTargetIconStoredSize *
                               pixels_per_stored,
      .pixels_per_stored = pixels_per_stored,
  };
}

bool NameplateRenderer::ExtractCameraWorldPosition(const float* view_mtx,
                                                   float& x, float& y,
                                                   float& z) {
  if (view_mtx == nullptr) {
    return false;
  }

  const RenderVec3 eye = ExtractCameraPositionFromRetailViewMatrix(
      RenderMatrix4x4View{view_mtx, 16u});
  x = eye[0];
  y = eye[1];
  z = eye[2];
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

}
