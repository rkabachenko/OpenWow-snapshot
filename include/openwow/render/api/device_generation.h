#pragma once

#include <cstdint>

namespace openwow::render::api {

struct DeviceGeneration {
  std::uint64_t value{0};

  [[nodiscard]] constexpr bool IsValid() const noexcept {
    return value != 0;
  }

  friend constexpr bool operator==(DeviceGeneration, DeviceGeneration) = default;
};

}
