#pragma once

#include <cstdint>

namespace openwow::game::commerce {

class CopperAmount {
 public:
  explicit constexpr CopperAmount(const std::uint32_t value) : value_(value) {}

  [[nodiscard]] constexpr std::uint32_t value() const {
    return value_;
  }

  [[nodiscard]] constexpr bool IsZero() const {
    return value_ == 0;
  }

  friend constexpr bool operator==(CopperAmount, CopperAmount) = default;

 private:
  std::uint32_t value_;
};

}
