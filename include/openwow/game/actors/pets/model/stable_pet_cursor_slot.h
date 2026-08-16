#pragma once

#include <cstdint>
#include <optional>

namespace openwow::game::actors::pets {

class StablePetCursorSlot {
 public:
  [[nodiscard]] static constexpr StablePetCursorSlot CurrentPet() {
    return StablePetCursorSlot(std::nullopt);
  }

  [[nodiscard]] static constexpr StablePetCursorSlot StabledPet(
      const std::uint32_t zero_based_index) {
    return StablePetCursorSlot(zero_based_index);
  }

  [[nodiscard]] constexpr bool IsCurrentPet() const {
    return !stabled_index_.has_value();
  }

  [[nodiscard]] constexpr std::optional<std::uint32_t> stabled_index() const {
    return stabled_index_;
  }

  friend constexpr bool operator==(StablePetCursorSlot,
                                   StablePetCursorSlot) = default;

 private:
  explicit constexpr StablePetCursorSlot(
      const std::optional<std::uint32_t> stabled_index)
      : stabled_index_(stabled_index) {}

  std::optional<std::uint32_t> stabled_index_;
};

}
