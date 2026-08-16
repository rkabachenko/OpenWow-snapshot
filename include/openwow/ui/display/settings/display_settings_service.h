#pragma once

#include "openwow/ui/display/settings/display_settings_ports.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace openwow::ui::display {

class DisplaySettingsService final {
 public:
  DisplaySettingsService(DisplayModePort& modes,
                         DisplayDevicePort& device,
                         DisplayConfigurationPort& configuration);

  [[nodiscard]] std::vector<ScreenResolution> ScreenResolutions();

  [[nodiscard]] std::size_t CurrentResolutionIndex();
  [[nodiscard]] std::vector<int> RefreshRates(
      std::optional<std::size_t> one_based_resolution_index);

  void SelectResolution(std::optional<std::size_t> one_based_resolution_index);

  [[nodiscard]] std::vector<MultisampleFormat> MultisampleFormats();

  [[nodiscard]] std::size_t CurrentMultisampleFormatIndex();

  void SelectMultisampleFormat(
      std::optional<std::size_t> one_based_format_index);

  [[nodiscard]] std::optional<VideoCapabilities> Capabilities() const;
  [[nodiscard]] bool StereoVideoAvailable() const;
  [[nodiscard]] bool PlayerResolutionAvailable() const;

  [[nodiscard]] float Gamma() const;
  void SetGamma(double value);
  [[nodiscard]] double TerrainMip() const;
  void SetTerrainMip(std::int32_t value);

 private:
  void EnsureScreenResolutionCache();
  void EnsureMultisampleFormatCache();

  DisplayModePort& modes_;
  DisplayDevicePort& device_;
  DisplayConfigurationPort& configuration_;
  mutable std::mutex mutex_;
  std::vector<ScreenResolution> screen_resolutions_;
  std::vector<MultisampleFormat> multisample_formats_;
  bool screen_resolutions_initialized_{false};
  bool multisample_formats_initialized_{false};
};

}
