#pragma once

#include <cstdint>

namespace openwow::game {

struct UnitAreaWeatherData {
  std::int32_t min_elevation = 0;
  std::uint32_t elevation_range = 0;
  void *weather_spline_track = nullptr;
};

class UnitAreaWeatherComponent {
public:
  UnitAreaWeatherComponent() = default;
  UnitAreaWeatherComponent(const UnitAreaWeatherComponent &) = delete;
  UnitAreaWeatherComponent &operator=(const UnitAreaWeatherComponent &) = delete;
  UnitAreaWeatherComponent(UnitAreaWeatherComponent &&) noexcept = default;
  UnitAreaWeatherComponent &operator=(UnitAreaWeatherComponent &&) noexcept = default;
  ~UnitAreaWeatherComponent() = default;

  void SetData(UnitAreaWeatherData *data) noexcept { data_ = data; }

  [[nodiscard]] const UnitAreaWeatherData *Data() const noexcept { return data_; }

  [[nodiscard]] double MinElevation() const noexcept {
    if (data_ != nullptr) {
      return static_cast<double>(data_->min_elevation);
    }
    return 0.0;
  }

  [[nodiscard]] double ElevationRange() const noexcept {
    if (data_ != nullptr) {
      return static_cast<double>(data_->elevation_range);
    }
    return 0.0;
  }

  [[nodiscard]] void *SplineTrack() const noexcept {
    if (data_ != nullptr) {
      return data_->weather_spline_track;
    }
    return nullptr;
  }

private:
  UnitAreaWeatherData *data_{nullptr};
};

}
