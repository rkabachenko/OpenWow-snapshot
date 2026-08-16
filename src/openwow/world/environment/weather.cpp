#include "openwow/world/environment/weather.h"

#include "openwow/data/formats/dbc/dbc_entries_world.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace openwow::world {
namespace {

constexpr float kMinimumTransitionDivisor = 0.001f;

float Transition(const float from, const float to, const std::uint32_t elapsed_ms,
                 const float seconds_per_unit, const float diff_scale = 1.0f) {
  const float duration =
      std::abs((to - from) * diff_scale + kMinimumTransitionDivisor) * seconds_per_unit;
  const float progress =
      duration > 0.0f
          ? std::clamp(static_cast<float>(elapsed_ms) / 1000.0f / duration,
                       0.0f, 1.0f)
          : 1.0f;
  return std::lerp(from, to, progress);
}

std::uint8_t ColorByte(const float value) {
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(value * 255.0f), 0l, 255l));
}

std::string_view DefaultTexture(const WeatherKind kind) {
  switch (kind) {
    case WeatherKind::kRain:
      return "textures\\Weather\\RainDrop01.blp";
    case WeatherKind::kSnow:
      return "textures\\Weather\\SnowFlake01.blp";
    case WeatherKind::kSandstorm:
    case WeatherKind::kNone:
      return {};
  }
  return {};
}

}

void SetWeather(WeatherState& state, const WeatherKind kind, float density,
                const data::dbc::WeatherEntry* row, const bool smooth,
                const float fog_speed, const std::uint32_t now) {
  density = std::clamp(density, 0.0f, 1.0f);
  state.transition_start_density = smooth ? state.density : density;
  state.transition_start_fog_speed = smooth ? state.fog_speed : fog_speed;
  state.target_density = density;
  state.target_fog_speed = fog_speed;
  state.transition_started_at = now;
  state.smooth_transition = smooth;
  state.kind = kind;
  state.texture = row != nullptr && !row->effect_texture.empty()
                      ? std::string(row->effect_texture)
                      : std::string(DefaultTexture(kind));
  if (row != nullptr) {
    const auto r = ColorByte(row->effect_color_r);
    const auto g = ColorByte(row->effect_color_g);
    const auto b = ColorByte(row->effect_color_b);
    state.color_abgr = 0xff000000u | (static_cast<std::uint32_t>(b) << 16u) |
                       (static_cast<std::uint32_t>(g) << 8u) | r;
  } else {
    state.color_abgr = 0xffffffffu;
  }
  if (!smooth) {
    state.density = density;
    state.fog_speed = fog_speed;
    state.sky_overlay = std::min(density, 0.25f);
  }
}

void UpdateWeather(WeatherState& state, const WeatherUpdate& update) {
  if (state.has_motion_sample && update.now != state.previous_motion_tick) {
    const float seconds =
        static_cast<float>(update.now - state.previous_motion_tick) / 1000.0f;
    if (seconds > 0.0f) {
      for (std::size_t axis = 0; axis < state.velocity.size(); ++axis) {
        state.velocity[axis] =
            (update.position[axis] - state.position[axis]) / seconds;
      }
    }
  } else {
    state.has_motion_sample = true;
  }
  state.position = update.position;
  state.previous_motion_tick = update.now;
  state.indoors = update.indoors;

  if (!state.smooth_transition) {
    return;
  }
  const std::uint32_t elapsed = update.now - state.transition_started_at;
  state.density = Transition(state.transition_start_density,
                             state.target_density, elapsed, 10.0f);

  state.sky_overlay =
      Transition(std::min(state.transition_start_density, 0.25f),
                 std::min(state.target_density, 0.25f), elapsed, 10.0f, 4.0f);
  state.fog_speed = Transition(state.transition_start_fog_speed,
                               state.target_fog_speed, elapsed, 5.0f, 4.0f);
}

void ResetWeather(WeatherState& state) {
  state = {};
}

}
