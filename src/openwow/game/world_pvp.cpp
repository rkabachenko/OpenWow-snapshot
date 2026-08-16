#include "openwow/game/world_pvp.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

WorldPvPZoneData* WorldPvPManager::FindZone(WorldPvPZoneType type) {
  auto it = zones_.find(static_cast<std::uint8_t>(type));
  if (it == zones_.end()) return nullptr;
  return &it->second;
}

const WorldPvPZoneData* WorldPvPManager::FindZone(
    WorldPvPZoneType type) const {
  auto it = zones_.find(static_cast<std::uint8_t>(type));
  if (it == zones_.end()) return nullptr;
  return &it->second;
}

void WorldPvPManager::AddZone(const WorldPvPZoneData& zone) {
  zones_[static_cast<std::uint8_t>(zone.zone_type)] = zone;
}

std::optional<WorldPvPZoneData> WorldPvPManager::GetZone(
    WorldPvPZoneType type) const {
  const auto* z = FindZone(type);
  if (!z) return std::nullopt;
  return *z;
}

std::vector<WorldPvPZoneData> WorldPvPManager::GetAllZones() const {
  std::vector<WorldPvPZoneData> result;
  result.reserve(zones_.size());
  for (const auto& [key, zone] : zones_) {
    result.push_back(zone);
  }
  std::sort(result.begin(), result.end(),
            [](const WorldPvPZoneData& a, const WorldPvPZoneData& b) {
              return static_cast<std::uint8_t>(a.zone_type) <
                     static_cast<std::uint8_t>(b.zone_type);
            });
  return result;
}

void WorldPvPManager::SetObjectiveState(WorldPvPZoneType zone_type,
                                        std::uint32_t objective_id,
                                        WorldPvPObjectiveState state,
                                        float progress) {
  auto* z = FindZone(zone_type);
  if (!z) return;
  for (auto& obj : z->objectives) {
    if (obj.objective_id == objective_id) {
      obj.state = state;
      obj.capture_progress = std::clamp(progress, 0.0f, 1.0f);
      return;
    }
  }
}

std::optional<WorldPvPObjective> WorldPvPManager::GetObjective(
    WorldPvPZoneType zone_type, std::uint32_t objective_id) const {
  const auto* z = FindZone(zone_type);
  if (!z) return std::nullopt;
  for (const auto& obj : z->objectives) {
    if (obj.objective_id == objective_id) return obj;
  }
  return std::nullopt;
}

void WorldPvPManager::SetControllingFaction(WorldPvPZoneType zone_type,
                                            std::uint32_t faction) {
  auto* z = FindZone(zone_type);
  if (z) z->controlling_faction = faction;
}

std::uint32_t WorldPvPManager::GetControllingFaction(
    WorldPvPZoneType zone_type) const {
  const auto* z = FindZone(zone_type);
  return z ? z->controlling_faction : 0;
}

void WorldPvPManager::SetBattleTimer(WorldPvPZoneType zone_type,
                                     std::uint32_t seconds) {
  auto* z = FindZone(zone_type);
  if (z) z->time_to_next_battle = seconds;
}

std::uint32_t WorldPvPManager::GetBattleTimer(
    WorldPvPZoneType zone_type) const {
  const auto* z = FindZone(zone_type);
  return z ? z->time_to_next_battle : 0;
}

bool WorldPvPManager::IsActive(WorldPvPZoneType zone_type) const {
  const auto* z = FindZone(zone_type);
  return z ? z->is_active : false;
}

void WorldPvPManager::SetActive(WorldPvPZoneType zone_type, bool active) {
  auto* z = FindZone(zone_type);
  if (z) z->is_active = active;
}

std::string WorldPvPManager::GetStateName(WorldPvPObjectiveState state) {
  switch (state) {
    case WorldPvPObjectiveState::Neutral:   return "Neutral";
    case WorldPvPObjectiveState::Alliance:  return "Alliance";
    case WorldPvPObjectiveState::Horde:     return "Horde";
    case WorldPvPObjectiveState::Contested: return "Contested";
  }
  return "Unknown";
}

void WorldPvPManager::Update(float dt) {
  for (auto& [key, zone] : zones_) {
    if (zone.time_to_next_battle == 0) continue;

    float& accum = timer_accumulators_[key];
    accum += dt;

    auto whole = static_cast<std::uint32_t>(accum);
    if (whole > 0) {
      accum -= static_cast<float>(whole);
      if (whole >= zone.time_to_next_battle) {
        zone.time_to_next_battle = 0;
      } else {
        zone.time_to_next_battle -= whole;
      }
    }
  }
}

void WorldPvPManager::Reset() {
  zones_.clear();
  timer_accumulators_.clear();
}

}
