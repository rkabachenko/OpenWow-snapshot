#pragma once

#include <bit>
#include <cstdint>

namespace openwow::game {

struct SpellShapeshiftMask {
  std::uint32_t low = 0;
  std::uint32_t high = 0;
};

[[nodiscard]] constexpr SpellShapeshiftMask MakeSpellShapeshiftMask(
    const std::uint32_t low,
    const std::uint32_t high) {
  return {
      .low = low,
      .high = high,
  };
}

[[nodiscard]] constexpr bool SpellShapeshiftMaskEmpty(
    const SpellShapeshiftMask mask) {
  return mask.low == 0 && mask.high == 0;
}

[[nodiscard]] constexpr bool SpellShapeshiftMaskHasZeroBasedFormIndex(
    const SpellShapeshiftMask mask,
    const int zero_based_form_index) {
  if (zero_based_form_index < 0) {
    return false;
  }

  const auto form_index = static_cast<std::uint32_t>(zero_based_form_index);
  if (form_index < 32u) {
    return (mask.low & (1u << form_index)) != 0;
  }
  if (form_index < 64u) {
    return (mask.high & (1u << (form_index - 32u))) != 0;
  }

  return false;
}

[[nodiscard]] constexpr bool SpellShapeshiftMaskHasFormId(
    const SpellShapeshiftMask mask,
    const std::uint32_t form_id) {
  if (form_id == 0) {
    return false;
  }

  return SpellShapeshiftMaskHasZeroBasedFormIndex(
      mask, static_cast<int>(form_id - 1u));
}

[[nodiscard]] constexpr std::uint32_t SpellShapeshiftMaskFirstFormId(
    const SpellShapeshiftMask mask) {
  if (mask.low != 0) {
    return static_cast<std::uint32_t>(std::countr_zero(mask.low)) + 1u;
  }
  if (mask.high != 0) {
    return static_cast<std::uint32_t>(std::countr_zero(mask.high)) + 33u;
  }

  return 0;
}

}
