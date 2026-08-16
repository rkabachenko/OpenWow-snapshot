#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::game {

enum class WeatherType : std::uint8_t {
  None        = 0,
  Rain        = 1,
  Snow        = 2,
  Storm       = 3,
  SandStorm   = 41,
  Thunderstorm = 86,
  BlackRain   = 90,
};

struct WorldSceneWorldState {
  std::uint32_t id = 0;
  std::int32_t value = 0;
};

class WorldSceneState {
 public:
  WorldSceneState() = default;

  void SetMapId(std::uint32_t map_id);
  [[nodiscard]] std::uint32_t GetMapId() const;

  void SetZoneId(std::uint32_t zone_id);
  [[nodiscard]] std::uint32_t GetZoneId() const;

  void SetSubZoneId(std::uint32_t sub_zone_id);
  [[nodiscard]] std::uint32_t GetSubZoneId() const;

  void SetZoneText(const std::string& text);
  [[nodiscard]] std::string GetZoneText() const;

  void SetSubZoneText(const std::string& text);
  [[nodiscard]] std::string GetSubZoneText() const;

  void SetRealZoneText(const std::string& text);
  [[nodiscard]] std::string GetRealZoneText() const;

  void SetMinimapZoneText(const std::string& text);
  [[nodiscard]] std::string GetMinimapZoneText() const;

  void SetPvPType(std::uint8_t type);
  [[nodiscard]] std::uint8_t GetPvPType() const;
  [[nodiscard]] bool IsPvPZone() const;
  [[nodiscard]] bool IsSanctuary() const;
  [[nodiscard]] bool IsCombatZone() const;

  [[nodiscard]] bool IsInInstance() const;
  [[nodiscard]] std::uint32_t GetInstanceId() const;
  void SetInstanceId(std::uint32_t id);
  [[nodiscard]] std::uint8_t GetDifficulty() const;
  void SetDifficulty(std::uint8_t difficulty);

  void SetWeather(WeatherType type, float intensity);
  [[nodiscard]] WeatherType GetWeatherType() const;
  [[nodiscard]] float GetWeatherIntensity() const;
  void SetFogColorRgb(float r, float g, float b);
  [[nodiscard]] std::uint32_t GetPackedFogColorArgb() const;

  void SetWorldState(std::uint32_t id, std::int32_t value);
  [[nodiscard]] std::int32_t GetWorldState(std::uint32_t id) const;
  void ClearWorldStates();
  void InitWorldStates(const std::vector<WorldSceneWorldState>& states);

  void SetGameTime(std::uint32_t hour, std::uint32_t minute);
  [[nodiscard]] std::uint32_t GetGameHour() const;
  [[nodiscard]] std::uint32_t GetGameMinute() const;

  void SetRestState(bool resting);
  [[nodiscard]] bool IsResting() const;

  void DiscoverArea(std::uint32_t area_flag);
  [[nodiscard]] bool IsAreaExplored(std::uint32_t area_flag) const;
  [[nodiscard]] std::uint32_t GetExploredAreaCount() const;

  void Reset();

 private:
  std::uint32_t map_id_{0};
  std::uint32_t zone_id_{0};
  std::uint32_t sub_zone_id_{0};
  std::string zone_text_;
  std::string sub_zone_text_;
  std::string real_zone_text_;
  std::string minimap_zone_text_;
  std::uint8_t pvp_type_{0};
  std::uint32_t instance_id_{0};
  std::uint8_t difficulty_{0};
  WeatherType weather_type_{WeatherType::None};
  float weather_intensity_{0.0f};
  std::uint32_t packed_fog_color_argb_{0xFF99B3D9u};
  std::unordered_map<std::uint32_t, std::int32_t> world_states_;
  std::uint32_t game_hour_{12};
  std::uint32_t game_minute_{0};
  bool resting_{false};
  std::unordered_set<std::uint32_t> explored_areas_;
  mutable std::mutex mutex_;
};

}
