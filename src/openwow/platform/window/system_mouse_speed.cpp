
#include "openwow/platform/window/system_mouse_speed.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace openwow::platform {

namespace {
#if defined(_WIN32)
constexpr unsigned int kSpiGetMouseSpeed = 0x70u;
constexpr unsigned int kSpiSetMouseSpeed = 0x71u;
#endif
}

int DefaultSystemMouseSpeedBackend::GetSystemMouseSpeed() {
#if defined(_WIN32)
  UINT raw_speed = 10;
  if (::SystemParametersInfoA(kSpiGetMouseSpeed, 0, &raw_speed, 0) != FALSE) {
    cached_speed_ = ClampRawSpeed(static_cast<int>(raw_speed));
  }
#endif
  return cached_speed_;
}

bool DefaultSystemMouseSpeedBackend::SetSystemMouseSpeed(int raw_speed) {
  cached_speed_ = ClampRawSpeed(raw_speed);
#if defined(_WIN32)
  return ::SystemParametersInfoA(
             kSpiSetMouseSpeed, 0,
             reinterpret_cast<void *>(static_cast<intptr_t>(cached_speed_)),
             0) != FALSE;
#else
  return true;
#endif
}

}
