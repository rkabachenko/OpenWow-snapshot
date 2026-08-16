#pragma once

#include "openwow/ui/game/loading_screen_progress_bar.h"
#include "openwow/ui/game/loading_screen_world_tiles.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::screens {
class LoadingScreenManager;
}

namespace openwow::ui::game {

namespace detail {

inline constexpr float kStandardLoadingScreenAspectRatio = 4.0f / 3.0f;
inline constexpr float kWideLoadingScreenAspectRatio = 16.0f / 10.0f;
inline constexpr float kWideLoadingScreenAspectRatioThreshold = 0.001f;
inline constexpr std::string_view kDefaultLoadingScreenTexturePath =
    "Interface\\Glues\\loading";
inline constexpr std::uint32_t kLoadingScreenClearColorAbgr = 0xFF000000u;
inline constexpr std::uint32_t kLoadingScreenClearColorRgba = 0x000000FFu;
struct LoadingScreenTextPixelLayout {
  float box_left_px = 0.0f;
  float box_width_px = 0.0f;
  float bottom_anchor_px = 0.0f;
  float font_height_px = 0.0f;
  float shadow_offset_x_px = 0.0f;
  float shadow_offset_y_px = 0.0f;
};

bool ShouldAttemptWideLoadingScreenTexture(bool has_wide_screen,
                                           float current_aspect_ratio);

std::string BuildWideLoadingScreenTexturePath(std::string_view base_path);

std::string SelectLoadingScreenTexturePath(
    std::string_view base_path, bool has_wide_screen,
    float current_aspect_ratio,
    const std::function<bool(const std::string&)>& archive_path_probe);

float ResolveLoadingScreenTargetAspectRatio(bool using_wide_texture);

std::uint32_t ResolveVisibleRibbonQuadCount(std::uint32_t overlay_vertex_count,
                                            float progress);

inline constexpr std::array<std::uint16_t, 12>
    kLoadingScreenTransportOverlayMarkerIndices = {
        0u, 1u, 2u, 1u, 3u, 2u,
        4u, 5u, 6u, 5u, 7u, 6u,
    };

constexpr LoadingScreenTextPixelLayout
BuildLoadingScreenTextPixelLayout(
    float screen_width, float screen_height, float anchor_x, float anchor_y,
    float normalized_width, float normalized_font_height,
    float normalized_shadow_offset_x, float normalized_shadow_offset_y) {
  if (screen_width <= 0.0f || screen_height <= 0.0f) {
    return {};
  }

  return {
      .box_left_px = anchor_x * screen_width,
      .box_width_px = normalized_width * screen_width,
      .bottom_anchor_px = (1.0f - anchor_y) * screen_height,
      .font_height_px = normalized_font_height * screen_height,
      .shadow_offset_x_px = normalized_shadow_offset_x * screen_width,
      .shadow_offset_y_px = -normalized_shadow_offset_y * screen_height,
  };
}

}

class GameLoadingScreen {
 public:
  GameLoadingScreen();
  ~GameLoadingScreen();

  GameLoadingScreen(const GameLoadingScreen&) = delete;
  GameLoadingScreen& operator=(const GameLoadingScreen&) = delete;

  bool Initialize();

  void Shutdown();

  void PrepareMap(std::uint32_t map_id);
  void ReleaseMap();

  void SetFileLoader(
      std::function<std::vector<std::uint8_t>(const std::string&)> loader) {
    file_loader_ = std::move(loader);
  }

  void SetArchivePathProbe(
      std::function<bool(const std::string&)> probe) {
    archive_path_probe_ = std::move(probe);
  }

  void SetDbcLoader(const void* dbc) { dbc_loader_ = dbc; }

  void Render(openwow::screens::LoadingScreenManager& state,
              std::uint8_t view_id, float screen_w, float screen_h);

  void RenderTransportProgressOverlay(
      openwow::screens::LoadingScreenManager& state,
      std::uint8_t view_id, float screen_w, float screen_h);

 private:
  struct GpuState;
  static constexpr std::uint16_t kInvalidTextureHandleIndex = 0xFFFFu;

  std::unique_ptr<GpuState> gpu_;

  bool initialized_{false};

  void DrawQuad(std::uint8_t view_id, float x, float y, float w, float h,
                std::uint32_t abgr);

  void DrawTexturedQuad(std::uint8_t view_id, float x, float y, float w,
                        float h, std::uint16_t texture_index, float u0 = 0.0f,
                        float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
                        std::uint32_t abgr = 0xFFFFFFFFu);

  void DrawNormalizedTexturedQuad(std::uint8_t view_id,
                                  float screen_w,
                                  float screen_h,
                                  float left,
                                  float right,
                                  float bottom,
                                  float top,
                                  std::uint16_t texture_index,
                                  float u0 = 0.0f,
                                  float v0 = 0.0f,
                                  float u1 = 1.0f,
                                  float v1 = 1.0f,
                                  std::uint32_t abgr = 0xFFFFFFFFu);
  [[nodiscard]] LoadingScreenViewportRect BuildContentViewportRect(
      float screen_w, float screen_h) const;

  void LoadBackgroundTexture(std::uint32_t map_id);
  void EnsureLoadingBarTexturesLoaded();
  bool EnsureTransportOverlayTexturesLoaded();
  void ReleaseTransportOverlayTextures();
  std::uint16_t LoadTextureAsset(const std::string& texture_path);
  [[nodiscard]] std::uint16_t ResolveLoadingBarTexture(
      std::string_view texture_path) const;
  void RenderTransportWorldBackground(std::uint8_t view_id,
                                      float screen_w,
                                      float screen_h,
                                      const LoadingScreenViewportRect& content_rect);
  void RenderTransportDynamicOverlay(std::uint8_t view_id,
                                     float screen_w,
                                     float screen_h,
                                     const LoadingScreenViewportRect& content_rect,
                                     float progress);
  bool TryLoadBackgroundTexturePath(std::string_view texture_path,
                                    bool use_wide_aspect);

  float content_aspect_ratio_{detail::kStandardLoadingScreenAspectRatio};
  std::function<std::vector<std::uint8_t>(const std::string&)> file_loader_;
  std::function<bool(const std::string&)> archive_path_probe_;
  const void* dbc_loader_{nullptr};

};

}
