
#include "hid_manager.h"

#include "openwow/game/keybind_system.h"
#include "openwow/input/input_manager.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/runtime/frame_input_router.h"

#include <SDL.h>

#include <new>
#include <unordered_set>
#include <vector>

namespace openwow::input {

namespace {

std::unordered_set<HidDevice*> g_registered_wow_mouse_devices;
std::uint32_t g_wow_mouse_raw_button_state = 0;
openwow::ui::game::runtime::FrameInputRouter* g_world_ui_input_router = nullptr;

void DispatchWowMouseButtonTransition(const std::uint32_t button_flag,
                                      const bool pressed) {
  auto& input_manager = InputManager::Get();
  const auto [mouse_x, mouse_y] = input_manager.GetMousePosition();
  const auto input_modifiers = input_manager.GetModifierBitmask();
  const auto keybind_modifiers =
      static_cast<std::uint16_t>(SDL_GetModState());

  if (pressed) {
    input_manager.ProcessMouseButtonFlagDown(button_flag, mouse_x, mouse_y,
                                             input_modifiers);
    openwow::game::KeybindSystem::Get().ProcessMouseButtonDown(
        button_flag, keybind_modifiers);
    if (g_world_ui_input_router != nullptr) {
      g_world_ui_input_router->HandleMouseButtonDownByFlag(
          static_cast<float>(mouse_x), static_cast<float>(mouse_y),
          button_flag);
    }
    return;
  }

  input_manager.ProcessMouseButtonFlagUp(button_flag, mouse_x, mouse_y,
                                         input_modifiers);
  openwow::game::KeybindSystem::Get().ProcessMouseButtonUp(button_flag,
                                                           keybind_modifiers);
  if (g_world_ui_input_router != nullptr) {
    g_world_ui_input_router->HandleMouseButtonUpByFlag(
        static_cast<float>(mouse_x), static_cast<float>(mouse_y), button_flag);
  }
}

void HidManagerButtonStateSink(void* const context,
                               const std::uint32_t raw_button_state) {
  (void)context;
  if (g_registered_wow_mouse_devices.empty()) {
    return;
  }

  const std::uint32_t visible_state = raw_button_state & 0xFFFFFFE0u;
  const std::uint32_t changed_bits =
      g_wow_mouse_raw_button_state ^ visible_state;
  g_wow_mouse_raw_button_state = visible_state;

  for (std::uint32_t bit_index = 5; bit_index < 32; ++bit_index) {
    const std::uint32_t button_flag = 1u << bit_index;
    if ((changed_bits & button_flag) == 0u) {
      continue;
    }

    DispatchWowMouseButtonTransition(button_flag,
                                     (visible_state & button_flag) != 0u);
  }
}

void RegisterWowMouseReader(HidDevice* const device) {
  if (device == nullptr) {
    return;
  }

  const bool should_install_sink = g_registered_wow_mouse_devices.empty();
  g_registered_wow_mouse_devices.insert(device);
  if (should_install_sink && !g_registered_wow_mouse_devices.empty()) {
    SetWowMouseButtonStateSink(HidManagerButtonStateSink, nullptr);
  }
}

void UnregisterWowMouseReader(HidDevice* const device) {
  if (device == nullptr) {
    return;
  }

  if (g_registered_wow_mouse_devices.erase(device) != 0u &&
      g_registered_wow_mouse_devices.empty()) {
    SetWowMouseButtonStateSink(nullptr, nullptr);
  }
}

}

void BindWorldUiInputRouter(
    openwow::ui::game::runtime::FrameInputRouter* router) noexcept {
  g_world_ui_input_router = router;
}

HidDevice* HidManagerImpl::CreateDevice() {
  auto* device = new (std::nothrow) HidDevice{};
  return InitDevice(device);
}

HidDevice* HidManagerImpl::InitDevice(HidDevice* device) {
  if (device == nullptr) {
    return nullptr;
  }

  device->target_window = nullptr;
  device->button_report_devinst = 0;
  device->auxiliary_devinst = 0;
  device->tracked_devices.clear();
  return device;
}

void HidManagerImpl::DestroyDevice(HidDevice* device) {
  UnregisterWowMouseReader(device);
  delete device;
}

void HidManagerImpl::SetTargetWindow(HidDevice* device, void* window) {
  if (device == nullptr) {
    return;
  }

  device->target_window = window;
}

bool HidManagerImpl::BeginPolling(HidDevice* device) {
  if (device == nullptr) {
    return false;
  }

  for (auto& [_, tracked_device] : device->tracked_devices) {
    tracked_device.is_stale = true;
  }

  bool started_button_reader = false;
  std::vector<WowMouseEnumeratedDevice> enumerated_devices;
  EnumerateWowMouseDevices(enumerated_devices);
  for (const auto& enumerated_device : enumerated_devices) {
    if (enumerated_device.devinst == 0u) {
      continue;
    }

    const auto existing = device->tracked_devices.find(enumerated_device.devinst);
    if (existing != device->tracked_devices.end()) {
      existing->second.is_stale = false;
      continue;
    }

    if (!enumerated_device.has_button_report_interface &&
        !enumerated_device.has_auxiliary_interface) {
      continue;
    }

    auto& tracked_device = device->tracked_devices[enumerated_device.devinst];
    tracked_device.is_stale = false;
    if (enumerated_device.has_auxiliary_interface) {
      device->auxiliary_devinst = enumerated_device.devinst;
    }
    if (enumerated_device.has_button_report_interface) {
      device->button_report_devinst = enumerated_device.devinst;
      started_button_reader = true;
    }
  }

  for (auto it = device->tracked_devices.begin();
       it != device->tracked_devices.end();) {
    if (!it->second.is_stale) {
      ++it;
      continue;
    }

    if (device->auxiliary_devinst == it->first) {
      device->auxiliary_devinst = 0;
    }
    if (device->button_report_devinst == it->first) {
      device->button_report_devinst = 0;
    }
    it = device->tracked_devices.erase(it);
  }

  if (device->button_report_devinst != 0u) {
    RegisterWowMouseReader(device);
  } else {
    UnregisterWowMouseReader(device);
  }

  return started_button_reader;
}

bool HidManagerImpl::HasDetectedDevice(const HidDevice* device) {
  return device != nullptr && !device->tracked_devices.empty();
}

InputPeripheralBridge* InputPeripheralBridge::CreateForActiveWindow() {
  auto* bridge = new (std::nothrow) InputPeripheralBridge(nullptr);
  if (bridge == nullptr) {
    return nullptr;
  }

  void* active_window = openwow::platform::OS_GetActiveWindow(0);
  bridge->device_ = HidManagerImpl::CreateDevice();
  if (bridge->device_ == nullptr) {
    return bridge;
  }

  HidManagerImpl::SetTargetWindow(bridge->device_, active_window);
  HidManagerImpl::BeginPolling(bridge->device_);
  return bridge;
}

InputPeripheralBridge::~InputPeripheralBridge() {
  HidManagerImpl::DestroyDevice(device_);
}

bool InputPeripheralBridge::HasDetectedDevice() const {
  return HidManagerImpl::HasDetectedDevice(device_);
}

}
