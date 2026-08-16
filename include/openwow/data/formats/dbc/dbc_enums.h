#pragma once

#include "openwow/data/formats/dbc/dbc_locale.h"

#include <cstdint>

namespace openwow::data::dbc {

enum class MapType : std::uint32_t {
  kNormal    = 0,
  kInstance  = 1,
  kRaid      = 2,
  kBg        = 3,
  kArena     = 4,
};

enum AreaFlags : std::uint32_t {
  kAreaFlagSnow                = 0x00000001,
  kAreaFlagUNK1                = 0x00000002,
  kAreaFlagUNK2                = 0x00000004,
  kAreaFlagSlaveCapital        = 0x00000008,
  kAreaFlagUNK3                = 0x00000010,
  kAreaFlagSlaveCapital2       = 0x00000020,
  kAreaFlagAllowDuels          = 0x00000040,
  kAreaFlagArena               = 0x00000080,
  kAreaFlagCapital             = 0x00000100,
  kAreaFlagCity                = 0x00000200,
  kAreaFlagOutland             = 0x00000400,
  kAreaFlagSanctuary           = 0x00000800,
  kAreaFlagNeedFly             = 0x00001000,
  kAreaFlagUnused1             = 0x00002000,
  kAreaFlagOutlandx            = 0x00004000,
  kAreaFlagPvP                 = 0x00008000,
  kAreaFlagNoFlyZone           = 0x00020000,
  kAreaFlagSubZone             = 0x00040000,
  kAreaFlagCanHearthAndResurrect = 0x08000000,
};

enum class TeamId : std::uint32_t {
  kAlliance  = 1,
  kHorde     = 2,
  kNeutral   = 3,
};

enum class PowerType : std::uint32_t {
  kMana       = 0,
  kRage       = 1,
  kFocus      = 2,
  kEnergy     = 3,
  kHappiness  = 4,
  kRune       = 5,
  kRunicPower = 6,
  kHealth     = 0xFFFFFFFE,
};

enum class Expansion : std::uint32_t {
  kVanilla          = 0,
  kBurningCrusade   = 1,
  kWrathOfLichKing  = 2,
};

enum SpellSchoolMask : std::uint32_t {
  kSpellSchoolMaskNormal = 0x01,
  kSpellSchoolMaskHoly   = 0x02,
  kSpellSchoolMaskFire   = 0x04,
  kSpellSchoolMaskNature = 0x08,
  kSpellSchoolMaskFrost  = 0x10,
  kSpellSchoolMaskShadow = 0x20,
  kSpellSchoolMaskArcane = 0x40,
};

enum class SpellDmgClass : std::uint32_t {
  kNone   = 0,
  kMagic  = 1,
  kMelee  = 2,
  kRanged = 3,
};

enum class SpellPreventionType : std::uint32_t {
  kNone    = 0,
  kSilence = 1,
  kPacify  = 2,
};

enum class SpellFamilyName : std::uint32_t {
  kGeneric      = 0,
  kEnvironment  = 1,
  kMage         = 3,
  kWarrior      = 4,
  kWarlock      = 5,
  kPriest       = 6,
  kDruid        = 7,
  kRogue        = 8,
  kHunter       = 9,
  kPaladin      = 10,
  kShaman       = 11,
  kUNK2         = 13,
  kPotion       = 14,
  kDeathKnight  = 15,
  kPet          = 17,
};

enum class SkillCategory : std::uint32_t {
  kNone              = 0,
  kAttributes        = 5,
  kWeapon            = 6,
  kClass             = 7,
  kArmor             = 8,
  kSecondary         = 9,
  kLanguages         = 10,
  kProfession        = 11,
  kNotDisplayed      = 12,
};

inline constexpr int kMaxSpellEffects   = 3;
inline constexpr int kMaxSpellReagents  = 8;
inline constexpr int kMaxSpellTotems    = 2;
inline constexpr int kMaxLocales = static_cast<int>(kDbcLocaleCount);

inline constexpr std::uint32_t kSpellAuraPeriodicTriggerSpellFromClient = 48;

inline constexpr std::uint32_t kShapeshiftFormFlagIsStance = 0x1;

inline constexpr std::uint32_t kShapeshiftFormFlagAllowsNpcInteraction = 0x8;

inline constexpr std::uint32_t kShapeshiftFormFlagApFromAgility = 0x20;

inline constexpr std::uint32_t kShapeshiftFormFlagBlocksAutoCancel = 0x100;

inline constexpr std::uint32_t kShapeshiftFormFlagCancelOverride = 0x800;

inline constexpr std::uint32_t kShapeshiftFormFlagSuppressEmoteSound = 0x1000;

inline constexpr int kLocalizedStringFields =
    static_cast<int>(kDbcLocalizedStringFieldCount);

}
