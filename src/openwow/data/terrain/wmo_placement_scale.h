#pragma once

#include <cstdint>

namespace openwow::data::terrain {

[[nodiscard]] inline constexpr float
DecodeWmoPlacementScale(const std::uint16_t encoded_scale) noexcept {
  return encoded_scale == 0 ? 1.0f : static_cast<float>(encoded_scale) / 1024.0f;
}

}
