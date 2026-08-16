#pragma once

#include <cstdint>

namespace openwow::game {

enum class PowerType : std::uint8_t {
  kMana = 0,
  kRage = 1,
  kFocus = 2,
  kEnergy = 3,
  kHappiness = 4,
  kRune = 5,
  kRunes = 5,
  kRunicPower = 6,
  kHealth = 0xFE,
  kMax = 7,
};

enum class ReactionType : std::uint8_t {
  kHated = 0,
  kHostile = 1,
  kUnfriendly = 2,
  kNeutral = 3,
  kFriendly = 4,
  kHonored = 5,
  kRevered = 6,
  kExalted = 7,
};

enum UnitDynFlag : std::uint32_t {
  kUnitDynFlagLootable              = 0x01,
  kUnitDynFlagTrackUnit             = 0x02,
  kUnitDynFlagTapped                = 0x04,
  kUnitDynFlagTappedByPlayer        = 0x08,
  kUnitDynFlagSpecialInfo           = 0x10,
  kUnitDynFlagDead                  = 0x20,
  kUnitDynFlagReferAFriend          = 0x40,
  kUnitDynFlagTappedByAllThreatList = 0x80,
};

enum class ClassificationRank : std::uint32_t {
  kNormal    = 0,
  kElite     = 1,
  kRareElite = 2,
  kWorldBoss = 3,
  kRare      = 4,
  kTrivial   = 5,
};

enum class CreatureTypeId : std::uint8_t {
  kNone           = 0,
  kBeast          = 1,
  kDragonkin      = 2,
  kDemon          = 3,
  kElemental      = 4,
  kGiant          = 5,
  kUndead         = 6,
  kHumanoid       = 7,
  kCritter        = 8,
  kMechanical     = 9,
  kNotSpecified   = 10,
  kTotem          = 11,
  kNonCombatPet   = 12,
  kGasCloud       = 13,
};

struct DamageRange {
  float min_damage = 0.0f;
  float max_damage = 0.0f;
};

enum AttackSlot : std::uint8_t {
  kAttackSlotMainHand = 0,
  kAttackSlotOffHand  = 1,
  kAttackSlotRanged   = 2,
};

enum class UnitStateFlag : std::uint32_t {
  kNone             = 0x00000000,
  kServerControlled = 0x00000001,
  kNonAttackable    = 0x00000002,
  kDisarmed         = 0x00200000,
  kStunned          = 0x00040000,
  kSilenced         = 0x00002000,
  kPacified         = 0x00020000,
  kFleeing          = 0x00800000,
  kInCombat         = 0x00080000,
  kTaxiFlight       = 0x00100000,

  kConfused         = 0x00400000,
  kSkinnable        = 0x04000000,
  kMount            = 0x08000000,
  kNotSelectable    = 0x02000000,
  kPossessed        = 0x01000000,
  kSheathe          = 0x40000000,
  kFeignDeath       = 0x20000000,
  kRooted           = 0x00000800,
};

inline UnitStateFlag operator|(UnitStateFlag a, UnitStateFlag b) {
  return static_cast<UnitStateFlag>(
      static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline UnitStateFlag operator&(UnitStateFlag a, UnitStateFlag b) {
  return static_cast<UnitStateFlag>(
      static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline bool HasUnitStateFlag(UnitStateFlag val, UnitStateFlag flag) {
  return (static_cast<std::uint32_t>(val) &
          static_cast<std::uint32_t>(flag)) != 0;
}

}
