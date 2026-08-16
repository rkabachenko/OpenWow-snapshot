
#pragma once

#include <cstdint>
#include <cstring>

namespace openwow::math {

[[nodiscard]] inline float CopySignFloat(float magnitude, float sign_source) noexcept {
  std::uint32_t mag_bits;
  std::uint32_t sign_bits;
  std::memcpy(&mag_bits, &magnitude, sizeof(float));
  std::memcpy(&sign_bits, &sign_source, sizeof(float));

  const std::uint32_t result_bits = sign_bits ^ ((mag_bits ^ sign_bits) & 0x7FFFFFFFu);

  float result;
  std::memcpy(&result, &result_bits, sizeof(float));
  return result;
}

}
