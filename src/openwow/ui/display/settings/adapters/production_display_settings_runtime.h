#pragma once

#include <memory>

namespace openwow::ui::display {

class DisplaySettingsService;
class DisplayDevicePort;

namespace lua_adapter {
class FullscreenFrameScalePort;
}

class ProductionDisplaySettingsRuntime final {
 public:
  struct IsolatedRuntime final {};

  explicit ProductionDisplaySettingsRuntime(DisplayDevicePort& device);
  explicit ProductionDisplaySettingsRuntime(IsolatedRuntime);
  ~ProductionDisplaySettingsRuntime();
  ProductionDisplaySettingsRuntime(const ProductionDisplaySettingsRuntime&) =
      delete;
  ProductionDisplaySettingsRuntime& operator=(
      const ProductionDisplaySettingsRuntime&) = delete;

  [[nodiscard]] DisplaySettingsService& service() noexcept;
  [[nodiscard]] lua_adapter::FullscreenFrameScalePort& frame_scale() noexcept;

 private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}
