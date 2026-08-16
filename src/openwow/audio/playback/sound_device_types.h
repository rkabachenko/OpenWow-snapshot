#pragma once
#include <string>
#include <vector>
namespace openwow::audio {
struct SoundDeviceEnumerationState {
  std::vector<std::string> output_devices;
  std::vector<std::string> voice_output_devices;
  std::vector<std::string> input_devices;
  std::string current_output_device_name;
  std::string enumerated_default_output_device_name;
  std::string current_voice_output_device_name;
  std::string enumerated_default_voice_output_device_name;
  std::string current_input_device_name;
  std::string enumerated_default_input_device_name;
  bool output_reopen_pending{false};
};
struct SoundDeviceRefreshResult {
  bool output_driver_reset{false};
  bool voice_output_driver_reset{false};
  bool voice_input_driver_reset{false};
  bool output_backend_reopened{false};
};
}
