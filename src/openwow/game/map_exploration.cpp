
#include "openwow/game/map_exploration.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace openwow::game {

void MapExplorationManager::SetExplorationBits(std::uint32_t index,
                                               std::uint32_t bits) {
  if (index < kMaxExplorationFields)
    fields_[index] = bits;
}

std::uint32_t MapExplorationManager::GetExplorationBits(
    std::uint32_t index) const {
  return (index < kMaxExplorationFields) ? fields_[index] : 0;
}

bool MapExplorationManager::IsAreaExplored(std::uint32_t areaFlagIndex) const {
  const std::uint32_t wordIdx = areaFlagIndex / 32;
  const std::uint32_t bitIdx  = areaFlagIndex % 32;
  if (wordIdx >= kMaxExplorationFields) return false;
  return (fields_[wordIdx] & (1u << bitIdx)) != 0;
}

void MapExplorationManager::MarkExplored(std::uint32_t areaFlagIndex) {
  const std::uint32_t wordIdx = areaFlagIndex / 32;
  const std::uint32_t bitIdx  = areaFlagIndex % 32;
  if (wordIdx >= kMaxExplorationFields) return;

  const std::uint32_t mask = (1u << bitIdx);
  if (fields_[wordIdx] & mask) return;

  fields_[wordIdx] |= mask;

  for (auto& [id, area] : areas_) {
    if (area.exploreFlag == areaFlagIndex) {
      area.isExplored = true;
    }
  }
}

std::uint32_t MapExplorationManager::GetExploredCount() const {
  std::uint32_t count = 0;
  for (std::uint32_t i = 0; i < kMaxExplorationFields; ++i)
    count += static_cast<std::uint32_t>(std::popcount(fields_[i]));
  return count;
}

std::uint32_t MapExplorationManager::GetTotalAreaCount() const {
  return totalAreas_;
}

void MapExplorationManager::SetTotalAreas(std::uint32_t count) {
  totalAreas_ = count;
}

float MapExplorationManager::GetExplorationPercent() const {
  if (totalAreas_ == 0) return 0.0f;
  const float explored = static_cast<float>(GetExploredCount());
  const float total    = static_cast<float>(totalAreas_);
  return std::min((explored / total) * 100.0f, 100.0f);
}

void MapExplorationManager::RegisterArea(std::uint32_t areaId,
                                         std::uint32_t exploreFlag,
                                         std::uint32_t mapId,
                                         const std::string& name) {
  ExplorationArea area;
  area.areaId      = areaId;
  area.exploreFlag = exploreFlag;
  area.mapId       = mapId;
  area.name        = name;
  area.isExplored  = IsAreaExplored(exploreFlag);
  areas_[areaId]   = std::move(area);
}

std::string MapExplorationManager::GetAreaName(std::uint32_t areaId) const {
  auto it = areas_.find(areaId);
  return (it != areas_.end()) ? it->second.name : std::string{};
}

std::vector<std::uint32_t> MapExplorationManager::GetExploredAreasForMap(
    std::uint32_t mapId) const {
  std::vector<std::uint32_t> result;
  for (const auto& [id, area] : areas_) {
    if (area.mapId == mapId && IsAreaExplored(area.exploreFlag))
      result.push_back(id);
  }
  std::sort(result.begin(), result.end());
  return result;
}

void MapExplorationManager::Reset() {
  std::memset(fields_, 0, sizeof(fields_));
  totalAreas_ = 0;
  areas_.clear();
}

}
