
#include "openwow/game/world_scene_state.h"

namespace openwow::game {

void WorldSceneState::SetMapId(std::uint32_t map_id) {
  std::lock_guard lock(mutex_);
  map_id_ = map_id;
}

std::uint32_t WorldSceneState::GetMapId() const {
  std::lock_guard lock(mutex_);
  return map_id_;
}

void WorldSceneState::SetZoneId(std::uint32_t zone_id) {
  std::lock_guard lock(mutex_);
  zone_id_ = zone_id;
}

std::uint32_t WorldSceneState::GetZoneId() const {
  std::lock_guard lock(mutex_);
  return zone_id_;
}

void WorldSceneState::SetSubZoneId(std::uint32_t sub_zone_id) {
  std::lock_guard lock(mutex_);
  sub_zone_id_ = sub_zone_id;
}

std::uint32_t WorldSceneState::GetSubZoneId() const {
  std::lock_guard lock(mutex_);
  return sub_zone_id_;
}

void WorldSceneState::SetZoneText(const std::string& text) {
  std::lock_guard lock(mutex_);
  zone_text_ = text;
}

std::string WorldSceneState::GetZoneText() const {
  std::lock_guard lock(mutex_);
  return zone_text_;
}

void WorldSceneState::SetSubZoneText(const std::string& text) {
  std::lock_guard lock(mutex_);
  sub_zone_text_ = text;
}

std::string WorldSceneState::GetSubZoneText() const {
  std::lock_guard lock(mutex_);
  return sub_zone_text_;
}

void WorldSceneState::SetRealZoneText(const std::string& text) {
  std::lock_guard lock(mutex_);
  real_zone_text_ = text;
}

std::string WorldSceneState::GetRealZoneText() const {
  std::lock_guard lock(mutex_);
  return real_zone_text_;
}

void WorldSceneState::SetMinimapZoneText(const std::string& text) {
  std::lock_guard lock(mutex_);
  minimap_zone_text_ = text;
}

std::string WorldSceneState::GetMinimapZoneText() const {
  std::lock_guard lock(mutex_);
  return minimap_zone_text_;
}

void WorldSceneState::SetPvPType(std::uint8_t type) {
  std::lock_guard lock(mutex_);
  pvp_type_ = type;
}

std::uint8_t WorldSceneState::GetPvPType() const {
  std::lock_guard lock(mutex_);
  return pvp_type_;
}

bool WorldSceneState::IsPvPZone() const {
  std::lock_guard lock(mutex_);
  return pvp_type_ == 1;
}

bool WorldSceneState::IsSanctuary() const {
  std::lock_guard lock(mutex_);
  return pvp_type_ == 3;
}

bool WorldSceneState::IsCombatZone() const {
  std::lock_guard lock(mutex_);
  return pvp_type_ == 4;
}

bool WorldSceneState::IsInInstance() const {
  std::lock_guard lock(mutex_);
  return instance_id_ != 0;
}

std::uint32_t WorldSceneState::GetInstanceId() const {
  std::lock_guard lock(mutex_);
  return instance_id_;
}

void WorldSceneState::SetInstanceId(std::uint32_t id) {
  std::lock_guard lock(mutex_);
  instance_id_ = id;
}

std::uint8_t WorldSceneState::GetDifficulty() const {
  std::lock_guard lock(mutex_);
  return difficulty_;
}

void WorldSceneState::SetDifficulty(std::uint8_t difficulty) {
  std::lock_guard lock(mutex_);
  difficulty_ = difficulty;
}

void WorldSceneState::SetWeather(WeatherType type, float intensity) {
  std::lock_guard lock(mutex_);
  weather_type_ = type;
  weather_intensity_ = intensity;
}

WeatherType WorldSceneState::GetWeatherType() const {
  std::lock_guard lock(mutex_);
  return weather_type_;
}

float WorldSceneState::GetWeatherIntensity() const {
  std::lock_guard lock(mutex_);
  return weather_intensity_;
}

void WorldSceneState::SetFogColorRgb(const float r, const float g,
                                     const float b) {
  const auto clamp_channel = [](const float value) -> std::uint32_t {
    if (value <= 0.0f) {
      return 0u;
    }
    if (value >= 1.0f) {
      return 255u;
    }
    return static_cast<std::uint32_t>(value * 255.0f + 0.5f);
  };

  const std::uint32_t red = clamp_channel(r);
  const std::uint32_t green = clamp_channel(g);
  const std::uint32_t blue = clamp_channel(b);

  std::lock_guard lock(mutex_);
  packed_fog_color_argb_ =
      0xFF000000u | (red << 16) | (green << 8) | blue;
}

std::uint32_t WorldSceneState::GetPackedFogColorArgb() const {
  std::lock_guard lock(mutex_);
  return packed_fog_color_argb_;
}

void WorldSceneState::SetWorldState(std::uint32_t id, std::int32_t value) {
  std::lock_guard lock(mutex_);
  world_states_[id] = value;
}

std::int32_t WorldSceneState::GetWorldState(std::uint32_t id) const {
  std::lock_guard lock(mutex_);
  auto it = world_states_.find(id);
  return it != world_states_.end() ? it->second : 0;
}

void WorldSceneState::ClearWorldStates() {
  std::lock_guard lock(mutex_);
  world_states_.clear();
}

void WorldSceneState::InitWorldStates(
    const std::vector<WorldSceneWorldState>& states) {
  std::lock_guard lock(mutex_);
  world_states_.clear();
  for (const auto& ws : states) {
    world_states_[ws.id] = ws.value;
  }
}

void WorldSceneState::SetGameTime(std::uint32_t hour, std::uint32_t minute) {
  std::lock_guard lock(mutex_);
  game_hour_ = hour;
  game_minute_ = minute;
}

std::uint32_t WorldSceneState::GetGameHour() const {
  std::lock_guard lock(mutex_);
  return game_hour_;
}

std::uint32_t WorldSceneState::GetGameMinute() const {
  std::lock_guard lock(mutex_);
  return game_minute_;
}

void WorldSceneState::SetRestState(bool resting) {
  std::lock_guard lock(mutex_);
  resting_ = resting;
}

bool WorldSceneState::IsResting() const {
  std::lock_guard lock(mutex_);
  return resting_;
}

void WorldSceneState::DiscoverArea(std::uint32_t area_flag) {
  std::lock_guard lock(mutex_);
  explored_areas_.insert(area_flag);
}

bool WorldSceneState::IsAreaExplored(std::uint32_t area_flag) const {
  std::lock_guard lock(mutex_);
  return explored_areas_.count(area_flag) > 0;
}

std::uint32_t WorldSceneState::GetExploredAreaCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(explored_areas_.size());
}

void WorldSceneState::Reset() {
  std::lock_guard lock(mutex_);
  map_id_ = 0;
  zone_id_ = 0;
  sub_zone_id_ = 0;
  zone_text_.clear();
  sub_zone_text_.clear();
  real_zone_text_.clear();
  minimap_zone_text_.clear();
  pvp_type_ = 0;
  instance_id_ = 0;
  difficulty_ = 0;
  weather_type_ = WeatherType::None;
  weather_intensity_ = 0.0f;
  packed_fog_color_argb_ = 0xFF99B3D9u;
  world_states_.clear();
  game_hour_ = 12;
  game_minute_ = 0;
  resting_ = false;
  explored_areas_.clear();
}

}
