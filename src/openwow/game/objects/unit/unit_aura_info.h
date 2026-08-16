#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {

struct AuraInfo {
  std::uint32_t spell_id = 0;

  std::uint32_t flags = 0;
  std::uint8_t stack_count = 0;
  std::int32_t duration = 0;
  std::int32_t remaining = 0;
  ObjectGuid caster_guid;

  [[nodiscard]] std::uint8_t ActiveEffectMask() const {
    constexpr std::uint32_t kAppliedEffectBits = 0x07u;
    return static_cast<std::uint8_t>(flags & kAppliedEffectBits);
  }
};

}
