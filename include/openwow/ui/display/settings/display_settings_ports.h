#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::ui::display {

struct DisplayMode final {
  int width{0};
  int height{0};
  int bits_per_pixel{0};
  int refresh_rate{0};
};

struct ScreenResolution final {
  int width{0};
  int height{0};

  friend bool operator==(const ScreenResolution&,
                         const ScreenResolution&) = default;
};

struct MultisampleFormat final {
  int color_bits{24};
  int depth_bits{24};
  int samples{1};

  friend bool operator==(const MultisampleFormat&,
                         const MultisampleFormat&) = default;
};

struct VideoCapabilities final {
  bool anisotropic{false};
  bool pixel_shaders{false};
  bool vertex_shaders{false};
  bool trilinear{false};
  int buffering{1};
  std::optional<int> max_anisotropy;
  bool hardware_cursor{false};
  bool stereo_video{false};
};

class DisplayModePort {
 public:
  virtual ~DisplayModePort() = default;

  [[nodiscard]] virtual std::vector<DisplayMode> AvailableModes() const = 0;
};

class DisplayDevicePort {
 public:
  virtual ~DisplayDevicePort() = default;

  [[nodiscard]] virtual std::vector<MultisampleFormat>
  AvailableMultisampleFormats(int color_bits, int depth_bits) const = 0;
  [[nodiscard]] virtual std::optional<VideoCapabilities>
  Capabilities() const = 0;
  [[nodiscard]] virtual bool PlayerResolutionCapabilityAvailable() const = 0;
};

class DisplayConfigurationPort {
 public:
  virtual ~DisplayConfigurationPort() = default;

  [[nodiscard]] virtual bool WidescreenEnabled() const = 0;
  [[nodiscard]] virtual ScreenResolution CurrentResolution() const = 0;
  virtual void SetResolution(ScreenResolution resolution) = 0;

  [[nodiscard]] virtual int ColorBits() const = 0;
  [[nodiscard]] virtual int DepthBits() const = 0;
  [[nodiscard]] virtual int MultisampleCount() const = 0;
  virtual void SetMultisampleFormat(MultisampleFormat format) = 0;

  [[nodiscard]] virtual float StoredGamma() const = 0;

  virtual void SetStoredGamma(double value) = 0;
  [[nodiscard]] virtual std::int32_t StoredTerrainMip() const = 0;
  virtual void SetStoredTerrainMip(std::int32_t value) = 0;
};

}
