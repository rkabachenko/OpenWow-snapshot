#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>

namespace openwow::game {

class WorldEnvironmentState {
 public:
  using OutdoorPositionQuery =
      std::function<std::optional<bool>(float, float, float)>;
  using SupportSurfaceHeightQuery =
      std::function<std::optional<float>(float, float, float)>;
  using LiquidSurfaceHeightQuery =
      std::function<std::optional<float>(float, float, float)>;
  using SnowPositionQuery = std::function<std::optional<bool>(float, float, float)>;

  void SetIndoors(bool indoors) noexcept {
    indoors_.store(indoors, std::memory_order_release);
  }

  [[nodiscard]] bool IsIndoors() const noexcept {
    return indoors_.load(std::memory_order_acquire);
  }

  void SetOutdoorPositionQuery(OutdoorPositionQuery query);
  [[nodiscard]] std::optional<bool> QueryOutdoorStateAtWorldPosition(
      float x, float y, float z) const;
  void SetSupportSurfaceHeightQuery(SupportSurfaceHeightQuery query);
  [[nodiscard]] std::optional<float> QuerySupportSurfaceHeight(
      float x, float y, float z) const;
  void SetLiquidSurfaceHeightQuery(LiquidSurfaceHeightQuery query);
  [[nodiscard]] std::optional<float> QueryLiquidSurfaceHeight(
      float x, float y, float z) const;
  void SetSnowPositionQuery(SnowPositionQuery query);
  [[nodiscard]] std::optional<bool> QuerySnowStateAtWorldPosition(
      float x, float y, float z) const;

  void Reset() noexcept { SetIndoors(false); }

 private:
  std::atomic<bool> indoors_{false};
  mutable std::mutex query_mutex_;
  OutdoorPositionQuery outdoor_position_query_;
  SupportSurfaceHeightQuery support_surface_height_query_;
  LiquidSurfaceHeightQuery liquid_surface_height_query_;
  SnowPositionQuery snow_position_query_;
};

using OutdoorPositionQuery = WorldEnvironmentState::OutdoorPositionQuery;

}
