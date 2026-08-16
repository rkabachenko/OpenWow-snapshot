#pragma once

#include <cstdint>

namespace openwow::game {

enum class SpellVisualKitType : std::uint32_t {
  kCastTarget     = 1,
  kImpactState    = 2,
  kMissileImpact  = 3,
  kCastBegin      = 4,
  kAuraApply      = 6,
  kMissileNoSrc   = 7,
};

struct SpellVisualKitInput {
  std::uint32_t spell_record;
  std::uint32_t visual_kit_record;
  std::uint32_t visual_type;
  std::uint32_t position_ptr;
  std::uint32_t target_guid_low;
  std::uint32_t target_guid_high;
  std::uint32_t auto_start;
  std::uint32_t harmful;
  std::uint32_t is_persistent;
  std::int32_t  delay;
  std::uint32_t source_guid_low;
  std::uint32_t source_guid_high;
  std::uint32_t timestamp;

  void Init(std::uint32_t spell_rec,
            std::uint32_t kit_rec,
            std::uint32_t type);

  void SetSourceGuid(std::uint32_t low, std::uint32_t high) {
    source_guid_low  = low;
    source_guid_high = high;
  }

  void SetTargetGuid(std::uint32_t low, std::uint32_t high) {
    target_guid_low  = low;
    target_guid_high = high;
  }
};

static_assert(sizeof(SpellVisualKitInput) == 52,
              "SpellVisualKitInput must match binary layout (13 DWORDs)");

}
