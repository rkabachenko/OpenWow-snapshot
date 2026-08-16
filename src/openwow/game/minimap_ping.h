
#pragma once

#include <cstdint>
#include <string>
#include <mutex>
#include <utility>

namespace openwow::ui {
class MinimapSystem;
}

namespace openwow::game {

struct MinimapPingPosition {
  double x{0.0};
  double y{0.0};
};

class MinimapPingSystem {
 public:
  explicit MinimapPingSystem(
      const openwow::ui::MinimapSystem& minimap) noexcept
      : minimap_(minimap) {}

  void Reset();
  void SetLastPingWorldPosition(float world_x, float world_y);
  [[nodiscard]] std::pair<float, float> GetLastPingWorldPosition() const;

  [[nodiscard]] MinimapPingPosition GetNormalizedPingPosition(
      float player_x, float player_y, float player_facing) const;
  [[nodiscard]] std::pair<float, float> ResolveWorldPositionFromFrameOffset(
      double frame_x, double frame_y, float minimap_width, float minimap_height,
      float player_x, float player_y, float player_facing) const;
  [[nodiscard]] std::pair<float, float> ResolveWorldPosition(
      double normalized_x, double normalized_y, float player_x,
      float player_y, float player_facing) const;

  void DispatchPingEvent(std::uint64_t source_guid,
                         std::uint64_t active_player_guid, float player_x,
                         float player_y, float player_facing) const;

 private:
  [[nodiscard]] std::string ResolveSourceUnitToken(
      std::uint64_t source_guid, std::uint64_t active_player_guid) const;
  [[nodiscard]] static bool ShouldRotateMinimap();
  [[nodiscard]] float GetDisplayedMinimapDirection(
      float fallback_player_facing) const;
  [[nodiscard]] float GetVisibleRadius() const;

  const openwow::ui::MinimapSystem& minimap_;
  mutable std::mutex mutex_;
  float last_ping_world_x_{0.0f};
  float last_ping_world_y_{0.0f};
};

}
