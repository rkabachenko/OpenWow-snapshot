
#include "openwow/game/cinematic_data.h"

#include <algorithm>

namespace openwow::game {

CinematicData& CinematicData::Get() {
  static CinematicData instance;
  return instance;
}

void CinematicData::AddCinematic(const CinematicEntry& entry) {
  std::lock_guard lock(mutex_);
  cinematics_[entry.cinematic_id] = entry;
}

std::optional<CinematicEntry> CinematicData::GetCinematic(
    std::uint32_t id) const {
  std::lock_guard lock(mutex_);
  auto it = cinematics_.find(id);
  if (it == cinematics_.end()) return std::nullopt;
  return it->second;
}

bool CinematicData::HasCinematic(std::uint32_t id) const {
  std::lock_guard lock(mutex_);
  return cinematics_.count(id) > 0;
}

bool CinematicData::IsValidCinematic(std::uint32_t id) const {

  return HasCinematic(id);
}

std::vector<std::uint32_t> CinematicData::GetAllCinematicIds() const {
  std::lock_guard lock(mutex_);
  std::vector<std::uint32_t> ids;
  ids.reserve(cinematics_.size());
  for (const auto& [id, _] : cinematics_) {
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

void CinematicData::ForEach(
    const std::function<void(const CinematicEntry&)>& fn) const {
  std::lock_guard lock(mutex_);
  for (const auto& [_, entry] : cinematics_) {
    fn(entry);
  }
}

void CinematicData::AddCameraPoint(std::uint32_t cinematic_id,
                                   const CinematicCameraPoint& pt) {
  std::lock_guard lock(mutex_);
  auto& vec = camera_points_[cinematic_id];
  vec.push_back(pt);

  if (vec.size() > 1 && vec[vec.size() - 2].time_ms > pt.time_ms) {
    std::sort(vec.begin(), vec.end(),
              [](const CinematicCameraPoint& a,
                 const CinematicCameraPoint& b) {
                return a.time_ms < b.time_ms;
              });
  }
}

std::vector<CinematicCameraPoint> CinematicData::GetCameraPoints(
    std::uint32_t cinematic_id) const {
  std::lock_guard lock(mutex_);
  auto it = camera_points_.find(cinematic_id);
  if (it == camera_points_.end()) return {};
  return it->second;
}

float CinematicData::GetDuration(std::uint32_t cinematic_id) const {
  std::lock_guard lock(mutex_);
  auto it = camera_points_.find(cinematic_id);
  if (it == camera_points_.end() || it->second.empty()) return 0.0f;

  const float max_time = it->second.back().time_ms;
  return max_time / 1000.0f;
}

void CinematicData::Reset() {
  std::lock_guard lock(mutex_);
  cinematics_.clear();
  camera_points_.clear();
}

}
