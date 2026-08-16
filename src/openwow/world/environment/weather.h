#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace openwow::data::dbc {
struct WeatherEntry;
}

namespace openwow::world {

enum class WeatherKind : std::uint8_t {
  kNone,
  kRain,
  kSnow,
  kSandstorm,
};

struct WeatherState {
  WeatherKind kind{WeatherKind::kNone};
  float target_density{};
  float density{};
  float transition_start_density{};
  float target_fog_speed{1.0f};
  float fog_speed{1.0f};
  float transition_start_fog_speed{1.0f};
  float sky_overlay{};
  std::uint32_t color_abgr{0xffffffffu};
  std::string texture;
  std::array<float, 3> position{};
  std::array<float, 3> velocity{};
  std::uint32_t transition_started_at{};
  std::uint32_t previous_motion_tick{};
  bool smooth_transition{};
  bool has_motion_sample{};
  bool indoors{};
};

struct WeatherUpdate {
  std::uint32_t now{};
  std::array<float, 3> position{};
  float facing{};
  float movement_speed{};
  bool indoors{};
};

void SetWeather(WeatherState& state, WeatherKind kind, float density,
                const data::dbc::WeatherEntry* row, bool smooth,
                float fog_speed, std::uint32_t now);
void UpdateWeather(WeatherState& state, const WeatherUpdate& update);
void ResetWeather(WeatherState& state);

}
