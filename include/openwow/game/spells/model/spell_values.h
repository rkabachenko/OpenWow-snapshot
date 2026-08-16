#pragma once

#include <cstdint>

namespace openwow::game::spells {

class TotemCategoryId {
 public:
  explicit constexpr TotemCategoryId(const std::uint32_t value) : value_(value) {}

  [[nodiscard]] constexpr std::uint32_t value() const {
    return value_;
  }

  [[nodiscard]] constexpr bool IsValid() const {
    return value_ != 0;
  }

  friend constexpr bool operator==(TotemCategoryId, TotemCategoryId) = default;

 private:
  std::uint32_t value_;
};

}
