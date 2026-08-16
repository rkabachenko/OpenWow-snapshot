#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/unit_defines.h"
#include "openwow/render/scene/world_overlay_metrics.h"
#include "openwow/ui/game/nameplate_system.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::render {

using NameplateInfo = openwow::ui::NameplateInfo;
using NameplatePixelRect = openwow::ui::NameplatePixelRect;
using NameplatePixelGeometry = openwow::ui::NameplatePixelGeometry;

struct NameplateDrawInfo {
  NameplateInfo nameplate;
  float screen_x{0.0f};
  float screen_y{0.0f};
  float screen_sort_key{0.0f};
  float camera_distance{0.0f};

  float view_depth{0.0f};
  std::uint8_t frame_level{10u};
};

struct NameplatePresentationSnapshot {
  game::ObjectGuid target;
  std::vector<NameplateInfo> nameplates;
};

class NameplateRenderer {
 public:
  NameplateRenderer() = default;
  ~NameplateRenderer() = default;

  NameplateRenderer(const NameplateRenderer&) = delete;
  NameplateRenderer& operator=(const NameplateRenderer&) = delete;

  bool Initialize();

  void ConsumePresentation(NameplatePresentationSnapshot presentation);

  void PublishFrameLayout(const WorldOverlayMetrics& metrics,
                          const float* view_mtx, const float* proj_mtx,
                          std::uint64_t compositor_generation = 0u);

  [[nodiscard]] std::vector<NameplateDrawInfo> BuildDrawList(
      const WorldOverlayMetrics& metrics, const float* view_mtx,
      const float* proj_mtx) const;
  [[nodiscard]] std::vector<NameplateInfo> GetVisibleNameplatesSnapshot() const;

  void Shutdown();

  void SetShowClassColorInNameplate(bool show) {
    show_class_color_in_nameplate_ = show;
  }
  void SetAllowOverlap(const bool allow) { allow_overlap_ = allow; }
  [[nodiscard]] bool show_class_color_in_nameplate() const {
    return show_class_color_in_nameplate_;
  }

  [[nodiscard]] bool stock_visuals_ready() const {
    return openwow::ui::NameplateFrameChannel::Get()
        .widget_evidence()
        .widgets_ready;
  }
  [[nodiscard]] std::size_t last_rendered_stock_plate_count() const {
    return openwow::ui::NameplateFrameChannel::Get().widget_evidence().plates;
  }
  [[nodiscard]] std::size_t last_rendered_name_text_count() const {
    return openwow::ui::NameplateFrameChannel::Get()
        .widget_evidence()
        .named_plates;
  }
  [[nodiscard]] std::uint64_t last_render_generation() const {
    return openwow::ui::NameplateFrameChannel::Get()
        .widget_evidence()
        .generation;
  }

  [[nodiscard]] static std::uint32_t ResolveHealthBarColorArgb(
      game::ReactionType reaction, bool is_player, bool is_trivial_level,
      std::uint8_t class_id, bool show_class_color_in_nameplate);
  [[nodiscard]] static std::uint32_t ResolveLevelColorArgb(
      std::uint32_t player_level, std::uint32_t unit_level);
  [[nodiscard]] static bool ShouldShowLevel(std::uint32_t player_level,
                                            std::uint32_t unit_level,
                                            game::ReactionType reaction,
                                            bool is_boss);
  [[nodiscard]] static std::uint8_t ResolveFrameAlpha(bool has_target,
                                                       bool is_target);
  [[nodiscard]] static std::uint32_t ResolveThreatColorArgb(
      std::uint8_t status);
  [[nodiscard]] static std::uint8_t ResolveRaidTargetIconAlpha(
      float distance_to_camera);
  [[nodiscard]] static NameplatePixelGeometry ResolvePixelGeometry(
      const WorldOverlayMetrics& metrics);

  [[nodiscard]] static constexpr NameplatePixelRect ThreatFlashUv() {
    return {0.0f, 0.53f, 0.555f, 0.07f};
  }

  [[nodiscard]] static constexpr NameplatePixelRect EliteIconUv() {
    return {0.0f, 0.0f, 0.578125f, 0.84375f};
  }

  [[nodiscard]] static constexpr NameplatePixelRect CastBorderUv() {
    return {1.0f, 0.0f, -1.0f, 1.0f};
  }

 private:
  openwow::ui::NameplateSystem nameplates_;

  static bool WorldToScreen(float wx, float wy, float wz,
                             const float* view_mtx, const float* proj_mtx,
                             const WorldOverlayMetrics& metrics,
                             float& sx, float& sy);
  [[nodiscard]] static bool ExtractCameraWorldPosition(const float* view_mtx,
                                                       float& x, float& y,
                                                       float& z);

  game::ObjectGuid target_guid_;
  bool show_class_color_in_nameplate_ = false;
  bool allow_overlap_ = true;
  bool initialized_{false};

  static constexpr float kFrameStoredWidth = 0.1f;
  static constexpr float kFrameStoredHeight = 0.025f;
  static constexpr float kThreatStoredWidth = 0.11f;
  static constexpr float kThreatStoredHeight = 0.029f;
  static constexpr float kThreatStoredOffsetX = -0.001f;
  static constexpr float kThreatStoredOffsetY = 0.0065f;
  static constexpr float kNameFontStoredHeight = 0.01f;
  static constexpr float kLevelFontStoredHeight = 0.009f;
  static constexpr float kCastBarWidthRatio = 0.804f;
  static constexpr float kCastBarHeightRatio = 0.281f;
  static constexpr float kStatusBarInsetXRatio = 0.031f;
  static constexpr float kStatusBarOffsetYRatio = 0.125f;
  static constexpr float kCastBorderTopRatio = 0.5f;
  static constexpr float kCastIconAnchorXRatio = 0.092f;
  static constexpr float kCastIconAnchorYRatio = 0.284f;
  static constexpr float kCastIconStoredSize = 0.01f;
  static constexpr float kSkullStoredSize = 0.01f;
  static constexpr float kEliteStoredWidth = 0.0294f;
  static constexpr float kEliteStoredHeight = 0.0215f;
  static constexpr float kEliteStoredOffsetX = 0.003f;
  static constexpr float kEliteStoredOffsetY = -0.001f;
  static constexpr float kRaidTargetIconStoredSize = 0.02f;
};

}
