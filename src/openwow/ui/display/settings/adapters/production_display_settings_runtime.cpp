#include "openwow/ui/display/settings/adapters/production_display_settings_runtime.h"

#include "openwow/ui/display/settings/adapters/lua/display_settings_lua.h"
#include "openwow/ui/display/settings/adapters/platform/display_mode_catalog.h"
#include "openwow/ui/display/settings/display_settings_service.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/framescript/core/frame_base_methods.h"
#include "openwow/ui/ui_aspect_scales.h"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openwow::ui::display {
namespace {

class ProductionDisplayModePort final : public DisplayModePort {
 public:
  std::vector<DisplayMode> AvailableModes() const override {
    std::vector<DisplayMode> result;
    for (const auto mode :
         openwow::ui::display::platform::AvailableDisplayModes()) {
      result.push_back({
          mode.width,
          mode.height,
          mode.bits_per_pixel,
          mode.refresh_rate,
      });
    }
    return result;
  }
};

class UnavailableDisplayDevicePort final : public DisplayDevicePort {
 public:
  std::vector<MultisampleFormat> AvailableMultisampleFormats(
      int, int) const override {
    return {};
  }

  std::optional<VideoCapabilities> Capabilities() const override {
    return std::nullopt;
  }

  bool PlayerResolutionCapabilityAvailable() const override { return false; }
};

class CVarDisplayConfigurationPort final : public DisplayConfigurationPort {
 public:
  bool WidescreenEnabled() const override {
    const auto& cvars = openwow::ui::game::CVarSystem::Instance();

    return cvars.GetCVarInt("widescreen") != 0;
  }

  ScreenResolution CurrentResolution() const override {
    ScreenResolution result;
    char separator = 0;
    const auto value =
        openwow::ui::game::CVarSystem::Instance().GetCVar("gxResolution");
    std::sscanf(value.c_str(), "%d%c%d", &result.width, &separator,
                &result.height);
    return result;
  }

  void SetResolution(const ScreenResolution resolution) override {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();

    if (!cvars.Exists("gxResolution")) {
      return;
    }
    (void)cvars.SetRegisteredCVarValueDirect(
        "gxResolution", std::to_string(resolution.width) + "x" +
                            std::to_string(resolution.height));
  }

  int ColorBits() const override {
    return openwow::ui::game::CVarSystem::Instance().GetCVarInt("gxColorBits");
  }

  int DepthBits() const override {
    return openwow::ui::game::CVarSystem::Instance().GetCVarInt("gxDepthBits");
  }

  int MultisampleCount() const override {
    return openwow::ui::game::CVarSystem::Instance().GetCVarInt(
        "gxMultisample");
  }

  void SetMultisampleFormat(const MultisampleFormat format) override {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();

    if (!cvars.Exists("gxColorBits") || !cvars.Exists("gxDepthBits") ||
        !cvars.Exists("gxMultisample")) {
      return;
    }

    (void)cvars.SetRegisteredCVarValueDirect(
        "gxColorBits", std::to_string(format.color_bits));
    (void)cvars.SetRegisteredCVarValueDirect(
        "gxDepthBits", std::to_string(format.depth_bits));
    (void)cvars.SetRegisteredCVarValueDirect(
        "gxMultisample", std::to_string(format.samples));
  }

  float StoredGamma() const override {
    return openwow::ui::game::CVarSystem::Instance().GetCVarFloat("Gamma");
  }

  void SetStoredGamma(const double value) override {

    constexpr std::size_t kNativeSetGammaBufferSize = 16;
    char formatted[kNativeSetGammaBufferSize]{};
    std::snprintf(formatted, sizeof(formatted), "%f", value);
    (void)openwow::ui::game::CVarSystem::Instance()
        .SetRegisteredCVarValueDirect("Gamma", formatted);
  }

  std::int32_t StoredTerrainMip() const override {
    return openwow::ui::game::CVarSystem::Instance().GetCVarInt("shadowLevel");
  }

  void SetStoredTerrainMip(const std::int32_t value) override {

    (void)openwow::ui::game::CVarSystem::Instance()
        .SetRegisteredCVarValueDirect("shadowLevel", std::to_string(value));
  }
};

class ProductionFrameScalePort final
    : public lua_adapter::FullscreenFrameScalePort {
 public:
  float AspectScale() const override {
    return openwow::ui::GetCachedUiAspectScaleKx();
  }

  void Apply(lua_State* lua, const int frame_index,
             const float scale) override {
    openwow::ui::game::frame_api::StoreLuaFrameScaleAndInvalidate(
        lua, frame_index, scale);
  }
};

}

class ProductionDisplaySettingsRuntime::Implementation final {
 public:
  explicit Implementation(DisplayDevicePort& device_value)
      : device(device_value), service(modes, device, configuration) {}

  Implementation()
      : isolated_device(std::make_unique<UnavailableDisplayDevicePort>()),
        device(*isolated_device),
        service(modes, device, configuration) {}

  ProductionDisplayModePort modes;
  std::unique_ptr<DisplayDevicePort> isolated_device;
  DisplayDevicePort& device;
  CVarDisplayConfigurationPort configuration;
  DisplaySettingsService service;
  ProductionFrameScalePort frame_scale;
};

ProductionDisplaySettingsRuntime::ProductionDisplaySettingsRuntime(
    DisplayDevicePort& device)
    : implementation_(std::make_unique<Implementation>(device)) {}

ProductionDisplaySettingsRuntime::ProductionDisplaySettingsRuntime(
    IsolatedRuntime)
    : implementation_(std::make_unique<Implementation>()) {}

ProductionDisplaySettingsRuntime::~ProductionDisplaySettingsRuntime() = default;

DisplaySettingsService& ProductionDisplaySettingsRuntime::service() noexcept {
  return implementation_->service;
}

lua_adapter::FullscreenFrameScalePort&
ProductionDisplaySettingsRuntime::frame_scale() noexcept {
  return implementation_->frame_scale;
}

}
