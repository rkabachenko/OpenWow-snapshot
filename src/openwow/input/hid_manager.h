#pragma once

#include <cstdint>
#include <unordered_map>

namespace openwow::ui::game::runtime {
class FrameInputRouter;
}

namespace openwow::input {

void BindWorldUiInputRouter(
    openwow::ui::game::runtime::FrameInputRouter* router) noexcept;

struct HidManagerTrackedDevice {
  bool is_stale{false};
};

struct HidDevice {
  void* target_window{nullptr};
  std::uint32_t button_report_devinst{0};
  std::uint32_t auxiliary_devinst{0};
  std::unordered_map<std::uint32_t, HidManagerTrackedDevice> tracked_devices;
};

class HidManagerImpl {
 public:
  static HidDevice* CreateDevice();
  static HidDevice* InitDevice(HidDevice* device);
  static void DestroyDevice(HidDevice* device);
  static void SetTargetWindow(HidDevice* device, void* window);
  static bool BeginPolling(HidDevice* device);
  [[nodiscard]] static bool HasDetectedDevice(const HidDevice* device);
};

class InputPeripheralBridge final {
 public:
  static InputPeripheralBridge* CreateForActiveWindow();

  explicit InputPeripheralBridge(HidDevice* device) : device_(device) {}
  ~InputPeripheralBridge();

  InputPeripheralBridge(const InputPeripheralBridge&) = delete;
  InputPeripheralBridge& operator=(const InputPeripheralBridge&) = delete;

  [[nodiscard]] bool HasDetectedDevice() const;

 private:
  HidDevice* device_{nullptr};
};

}
