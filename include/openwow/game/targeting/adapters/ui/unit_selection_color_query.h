#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {
class WorldSession;

namespace targeting::ui {

enum class UnitSelectionColorVariant : std::uint8_t {
  kSelection,
  kNameplate,
};

class UnitSelectionColor {
 public:
  static constexpr UnitSelectionColor FromPackedArgb(
      const std::uint32_t packed_argb) {
    return UnitSelectionColor(packed_argb);
  }

  static constexpr UnitSelectionColor Hostile() {
    return UnitSelectionColor(0xFFFF0000u);
  }

  [[nodiscard]] constexpr std::uint32_t packed_argb() const {
    return packed_argb_;
  }

  friend constexpr bool operator==(UnitSelectionColor,
                                   UnitSelectionColor) = default;

 private:
  explicit constexpr UnitSelectionColor(const std::uint32_t packed_argb)
      : packed_argb_(packed_argb) {}

  std::uint32_t packed_argb_;
};

[[nodiscard]] UnitSelectionColor DefaultUnitSelectionColor(
    UnitSelectionColorVariant variant);
[[nodiscard]] UnitSelectionColor ResolveUnitSelectionColor(
    const WorldSession &session, ObjectGuid target,
    UnitSelectionColorVariant variant);

}
}
