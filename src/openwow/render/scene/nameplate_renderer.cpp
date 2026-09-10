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
  const auto aspect = openwow::ui::ComputeUiAspectScaleState(
      metrics.aspect_ratio);
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
    float projected_x = 0.0f;
    float projected_y = 0.0f;
    float projected_depth = 0.0f;
    if (!WorldToScreen(np.world_x, np.world_y, np.world_z, view_mtx,
                       proj_mtx, metrics, projected_x, projected_y,
                       projected_depth)) {
      continue;
    }
    const float sx = projected_x / aspect.horizontal_scale * screen_w;
    const float sy = screen_h -
                     projected_y / aspect.vertical_scale * screen_h;

    float camera_distance = 8.0f;
    if (have_camera) {
      const float dx = np.world_x - camera_x;
      const float dy = np.world_y - camera_y;
      const float dz = np.world_z - camera_z;
      camera_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    const float sort_dx = projected_x - kNameplateSortAnchorX;
    const float sort_dy = projected_y - kNameplateSortAnchorY;

    draw_list.push_back({
        np,
        sx,
        sy,
        sort_dx * sort_dx + sort_dy * sort_dy,
        camera_distance,
        projected_depth,
        static_cast<std::uint8_t>(np.is_target ? 20u : 10u),
    });
  }

  std::stable_sort(draw_list.begin(), draw_list.end(),
                   [](const NameplateDrawInfo& lhs,
                      const NameplateDrawInfo& rhs) {
                     if (lhs.screen_sort_key != rhs.screen_sort_key) {
                       return lhs.screen_sort_key > rhs.screen_sort_key;
                     }
                     return lhs.nameplate.guid < rhs.nameplate.guid;
                   });
  if (!allow_overlap_) {
    openwow::ui::ClearAnchorGrid(0);
    for (auto& draw : draw_list) {
      const auto position = openwow::ui::game::ComputeNameplatePosition2D(
          {
              .screen_x = draw.screen_x / screen_w * aspect.horizontal_scale,
              .screen_y = (screen_h - draw.screen_y) / screen_h *
                          aspect.vertical_scale,
              .screen_z = draw.projected_depth,
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
    }
  }
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
          .projected_depth = draw.projected_depth,
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
                                       float& projected_x,
                                       float& projected_y,
                                       float& projected_depth) {
  if (view_mtx == nullptr || proj_mtx == nullptr) {
    return false;
  }

  const auto aspect =
      openwow::ui::ComputeUiAspectScaleState(metrics.aspect_ratio);
  const auto projected = ProjectWorldPointToViewport(
      RenderVec3{wx, wy, wz}, RenderMatrix4x4View{view_mtx, 16u},
      RenderMatrix4x4View{proj_mtx, 16u},
      RenderProjectionViewport{.right = aspect.horizontal_scale,
                               .bottom = aspect.vertical_scale});
  if (!projected.on_screen || projected.position[1] <= 0.0f) {
    return false;
  }
  projected_x = projected.position[0];
  projected_y = projected.position[1];
  projected_depth = projected.position[2];
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
