#include "openwow/ui/display/settings/display_settings_service.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace openwow::ui::display {
namespace {

constexpr std::array<ScreenResolution, 4> kFallbackResolutions{{
    {800, 600},
    {1024, 768},
    {1280, 1024},
    {1600, 1200},
}};

bool HasWidescreenAspect(const DisplayMode& mode) {
  return mode.width > 640 && mode.height > 480 &&

         static_cast<float>(mode.width) / static_cast<float>(mode.height) >=
             1.248F;
}

std::int32_t OneMinusWithI32Wrap(const std::int32_t value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  return std::bit_cast<std::int32_t>(1U - bits);
}

std::optional<std::size_t> ResolveOneBasedIndex(
    const std::size_t size, const std::optional<std::size_t> one_based_index) {
  if (size == 0 || !one_based_index || *one_based_index == 0 ||
      *one_based_index > size) {
    return std::nullopt;
  }
  return *one_based_index - 1;
}

std::optional<std::size_t> ResolveRetailSetterIndex(
    const std::size_t size, const std::optional<std::size_t> one_based_index) {
  if (size == 0) {
    return std::nullopt;
  }
  if (!one_based_index) {
    return std::size_t{0};
  }
  return ResolveOneBasedIndex(size, one_based_index);
}

constexpr std::size_t kRetailUnmatchedCurrentIndex = 1;

}

DisplaySettingsService::DisplaySettingsService(
    DisplayModePort& modes, DisplayDevicePort& device,
    DisplayConfigurationPort& configuration)
    : modes_(modes), device_(device), configuration_(configuration) {}

void DisplaySettingsService::EnsureScreenResolutionCache() {
  if (screen_resolutions_initialized_) {
    return;
  }

  if (configuration_.WidescreenEnabled()) {
    for (const auto& mode : modes_.AvailableModes()) {
      if (!HasWidescreenAspect(mode)) {
        continue;
      }
      const ScreenResolution candidate{mode.width, mode.height};
      if (!screen_resolutions_.empty() &&
          screen_resolutions_.back() == candidate) {
        continue;
      }
      screen_resolutions_.push_back(candidate);
    }
  }
  if (screen_resolutions_.empty()) {
    screen_resolutions_.assign(kFallbackResolutions.begin(),
                               kFallbackResolutions.end());
  }
  screen_resolutions_initialized_ = true;
}

std::vector<ScreenResolution> DisplaySettingsService::ScreenResolutions() {
  std::lock_guard lock(mutex_);
  EnsureScreenResolutionCache();
  return screen_resolutions_;
}

std::size_t DisplaySettingsService::CurrentResolutionIndex() {
  std::lock_guard lock(mutex_);
  EnsureScreenResolutionCache();
  const auto current = configuration_.CurrentResolution();
  const auto found =
      std::find(screen_resolutions_.begin(), screen_resolutions_.end(), current);
  return found == screen_resolutions_.end()
             ? kRetailUnmatchedCurrentIndex
             : static_cast<std::size_t>(
                   std::distance(screen_resolutions_.begin(), found) + 1);
}

std::vector<int> DisplaySettingsService::RefreshRates(
    const std::optional<std::size_t> one_based_resolution_index) {
  std::lock_guard lock(mutex_);
  EnsureScreenResolutionCache();
  const auto index = ResolveOneBasedIndex(
      screen_resolutions_.size(), one_based_resolution_index.value_or(1));
  if (!index) {
    return {};
  }

  const auto selected = screen_resolutions_[*index];
  std::vector<int> rates;
  for (const auto& mode : modes_.AvailableModes()) {
    if (mode.width == selected.width && mode.height == selected.height) {
      rates.push_back(mode.refresh_rate);
    }
  }
  std::sort(rates.begin(), rates.end());
  rates.erase(std::unique(rates.begin(), rates.end()), rates.end());
  return rates;
}

void DisplaySettingsService::SelectResolution(
    const std::optional<std::size_t> one_based_resolution_index) {
  std::lock_guard lock(mutex_);
  EnsureScreenResolutionCache();
  const auto index = ResolveRetailSetterIndex(screen_resolutions_.size(),
                                              one_based_resolution_index);
  if (!index) {
    return;
  }

  const ScreenResolution selected = screen_resolutions_[*index];

  if (configuration_.CurrentResolution() == selected) {
    return;
  }
  configuration_.SetResolution(selected);
}

void DisplaySettingsService::EnsureMultisampleFormatCache() {
  if (multisample_formats_initialized_) {
    return;
  }

  const int color_bits = configuration_.ColorBits();
  const int depth_bits = configuration_.DepthBits();
  for (const auto& format :
       device_.AvailableMultisampleFormats(color_bits, depth_bits)) {
    if (format.color_bits != color_bits || format.depth_bits != depth_bits ||
        std::find(multisample_formats_.begin(), multisample_formats_.end(),
                  format) != multisample_formats_.end()) {
      continue;
    }
    multisample_formats_.push_back(format);
  }

  multisample_formats_initialized_ = !multisample_formats_.empty();
}

std::vector<MultisampleFormat> DisplaySettingsService::MultisampleFormats() {
  std::lock_guard lock(mutex_);
  EnsureMultisampleFormatCache();
  return multisample_formats_;
}

std::size_t DisplaySettingsService::CurrentMultisampleFormatIndex() {
  std::lock_guard lock(mutex_);
  EnsureMultisampleFormatCache();
  const MultisampleFormat current{
      configuration_.ColorBits(),
      configuration_.DepthBits(),
      configuration_.MultisampleCount(),
  };
  const auto found = std::find(multisample_formats_.begin(),
                               multisample_formats_.end(), current);
  return found == multisample_formats_.end()
             ? kRetailUnmatchedCurrentIndex
             : static_cast<std::size_t>(
                   std::distance(multisample_formats_.begin(), found) + 1);
}

void DisplaySettingsService::SelectMultisampleFormat(
    const std::optional<std::size_t> one_based_format_index) {
  std::lock_guard lock(mutex_);
  EnsureMultisampleFormatCache();
  const auto index = ResolveRetailSetterIndex(multisample_formats_.size(),
                                              one_based_format_index);
  if (!index) {
    return;
  }

  const MultisampleFormat selected = multisample_formats_[*index];

  if (configuration_.ColorBits() == selected.color_bits &&
      configuration_.DepthBits() == selected.depth_bits &&
      configuration_.MultisampleCount() == selected.samples) {
    return;
  }
  configuration_.SetMultisampleFormat(selected);
}

std::optional<VideoCapabilities> DisplaySettingsService::Capabilities() const {
  return device_.Capabilities();
}

bool DisplaySettingsService::StereoVideoAvailable() const {
  const auto capabilities = device_.Capabilities();
  return capabilities && capabilities->stereo_video;
}

bool DisplaySettingsService::PlayerResolutionAvailable() const {
  return device_.PlayerResolutionCapabilityAvailable();
}

float DisplaySettingsService::Gamma() const {
  return 1.0F - configuration_.StoredGamma();
}

void DisplaySettingsService::SetGamma(const double value) {

  configuration_.SetStoredGamma(1.0 - value);
}

double DisplaySettingsService::TerrainMip() const {

  return 1.0 - static_cast<double>(configuration_.StoredTerrainMip());
}

void DisplaySettingsService::SetTerrainMip(const std::int32_t value) {
  configuration_.SetStoredTerrainMip(OneMinusWithI32Wrap(value));
}

}
