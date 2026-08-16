#include "openwow/game/world_environment_state.h"

#include <utility>

namespace openwow::game {

void WorldEnvironmentState::SetOutdoorPositionQuery(
    OutdoorPositionQuery query) {
  std::lock_guard lock(query_mutex_);
  outdoor_position_query_ = std::move(query);
}

std::optional<bool> WorldEnvironmentState::QueryOutdoorStateAtWorldPosition(
    const float x, const float y, const float z) const {
  OutdoorPositionQuery query;
  {
    std::lock_guard lock(query_mutex_);
    query = outdoor_position_query_;
  }
  return query ? query(x, y, z) : std::nullopt;
}

void WorldEnvironmentState::SetSupportSurfaceHeightQuery(
    SupportSurfaceHeightQuery query) {
  std::lock_guard lock(query_mutex_);
  support_surface_height_query_ = std::move(query);
}

std::optional<float> WorldEnvironmentState::QuerySupportSurfaceHeight(
    const float x, const float y, const float z) const {
  SupportSurfaceHeightQuery query;
  {
    std::lock_guard lock(query_mutex_);
    query = support_surface_height_query_;
  }
  return query ? query(x, y, z) : std::nullopt;
}

void WorldEnvironmentState::SetLiquidSurfaceHeightQuery(
    LiquidSurfaceHeightQuery query) {
  std::lock_guard lock(query_mutex_);
  liquid_surface_height_query_ = std::move(query);
}

std::optional<float> WorldEnvironmentState::QueryLiquidSurfaceHeight(
    const float x, const float y, const float z) const {
  LiquidSurfaceHeightQuery query;
  {
    std::lock_guard lock(query_mutex_);
    query = liquid_surface_height_query_;
  }
  return query ? query(x, y, z) : std::nullopt;
}

void WorldEnvironmentState::SetSnowPositionQuery(SnowPositionQuery query) {
  std::lock_guard lock(query_mutex_);
  snow_position_query_ = std::move(query);
}

std::optional<bool> WorldEnvironmentState::QuerySnowStateAtWorldPosition(
    const float x, const float y, const float z) const {
  SnowPositionQuery query;
  {
    std::lock_guard lock(query_mutex_);
    query = snow_position_query_;
  }
  return query ? query(x, y, z) : std::nullopt;
}

}
