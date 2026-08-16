#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {

inline constexpr std::uint32_t kSpellAttrRequiresRangedWeapon = 0x00000002u;

inline constexpr std::uint32_t kSpellAttrEx2AutoRepeat = 0x00000020u;

[[nodiscard]] inline constexpr bool IsAutoRepeatRangedSpell(
    const std::uint32_t attributes, const std::uint32_t attributes_ex2) {
  return (attributes & kSpellAttrRequiresRangedWeapon) != 0u &&
         (attributes_ex2 & kSpellAttrEx2AutoRepeat) != 0u;
}

enum class SpellHelpfulHarmfulDisposition : std::uint8_t {
  kNeutral = 0,
  kHelpful = 1,
  kHarmful = 2,
};

enum class ChannelUpdateTransition : std::uint8_t {
  kIgnored = 0,
  kUpdated,
  kStopped,
};

struct SpellMissileTargetList {
  std::uint32_t count{0};
  const ObjectGuid* guids{nullptr};
};

struct SpellGroundClickData {
  ObjectGuid object_guid;
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

enum class SpellGroundClickValidation : std::uint8_t {
  kInRange = 0,
  kOutOfRange = 1,
  kTooFar = 2,
};

struct SpellActionInvocation {
  std::uintptr_t action_data{0};
  std::uint32_t spell_id{0};
};

}
