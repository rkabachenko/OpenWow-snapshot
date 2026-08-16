#pragma once

#include <compare>
#include <cstdint>
#include <optional>

namespace openwow::game::actions {

class ActionPage {
 public:
  static constexpr std::uint8_t kFirst = 1;
  static constexpr std::uint8_t kLast = 6;

  [[nodiscard]] static constexpr ActionPage First() noexcept {
    return ActionPage(kFirst);
  }

  [[nodiscard]] static constexpr std::optional<ActionPage> FromValue(
      const std::uint8_t value) noexcept {
    return value >= kFirst && value <= kLast
               ? std::optional<ActionPage>(ActionPage(value))
               : std::nullopt;
  }

  [[nodiscard]] constexpr std::uint8_t value() const noexcept {
    return value_;
  }

  auto operator<=>(const ActionPage&) const = default;

 private:
  explicit constexpr ActionPage(const std::uint8_t value) noexcept
      : value_(value) {}

  std::uint8_t value_;
};

class ActionPageState {
 public:
  [[nodiscard]] ActionPage current() const noexcept { return current_; }

  [[nodiscard]] bool Set(const ActionPage page) noexcept {
    if (page == current_) {
      return false;
    }
    current_ = page;
    return true;
  }

  void Reset() noexcept { current_ = ActionPage::First(); }

 private:
  ActionPage current_{ActionPage::First()};
};

}
