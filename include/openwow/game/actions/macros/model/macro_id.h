#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace openwow::game::actions::macros {

class MacroId {
 public:
  constexpr MacroId() = default;
  explicit constexpr MacroId(const std::uint32_t value) : value_(value) {}

  [[nodiscard]] constexpr std::uint32_t value() const {
    return value_;
  }

  [[nodiscard]] constexpr bool IsValid() const {
    return value_ != 0;
  }

  [[nodiscard]] constexpr MacroId Next() const {
    return MacroId(value_ + 1u);
  }

  auto operator<=>(const MacroId&) const = default;

 private:
  std::uint32_t value_{0};
};

}

template <>
struct std::hash<openwow::game::actions::macros::MacroId> {
  std::size_t operator()(
      const openwow::game::actions::macros::MacroId id) const noexcept {
    return std::hash<std::uint32_t>{}(id.value());
  }
};
