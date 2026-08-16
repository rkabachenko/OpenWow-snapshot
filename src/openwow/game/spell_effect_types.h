#pragma once

#include <cstdint>

namespace openwow::game {

enum SpellTargetType : std::uint32_t {
  kTargetNone                    = 0,
  kTargetSelf                    = 1,
  kTargetUnitEnemy               = 6,
  kTargetUnitAlly                = 8,
  kTargetUnitParty               = 9,
  kTargetUnitRaid                = 10,
  kTargetUnitRaidClass           = 11,
  kTargetUnitPet                 = 13,
  kTargetUnitEnemyArea           = 15,
  kTargetUnitAllyArea            = 16,
  kTargetUnitPartyArea           = 21,
  kTargetUnitAllyAuraArea        = 25,
  kTargetUnitEnemyAuraArea       = 26,
  kTargetUnitConeEnemy           = 29,
  kTargetUnitConeAlly            = 30,
  kTargetUnitConeParty           = 31,
  kTargetUnitAreaAlly            = 32,
  kTargetUnitAreaEnemy           = 33,
  kTargetUnitAreaParty           = 34,
  kTargetUnitAreaRaid            = 35,
  kTargetUnitAllyRaidArea        = 36,
  kTargetUnitNearestEnemy        = 42,
  kTargetUnitNearestAlly         = 43,
  kTargetUnitNearestParty        = 44,
  kTargetUnitNearestRaid         = 45,
  kTargetUnitChain               = 52,
  kTargetUnitConeEnemyArea       = 53,
  kTargetUnitSummoner            = 54,
  kTargetUnitVehicle             = 61,
  kTargetUnitPassenger           = 62,
  kTargetDestAreaEnemy           = 83,
  kTargetDestAreaAlly            = 84,
  kTargetDestAreaParty           = 85,
  kTargetDestAreaRaid            = 86,
  kTargetDestAreaRaidClass       = 87,
  kTargetDestConeEnemy           = 89,
  kTargetDestConeAlly            = 90,
  kTargetDestConeParty           = 91,
  kTargetDestConeRaid            = 92,
  kTargetSrcAreaEnemy            = 93,
  kTargetSrcAreaAlly             = 94,
  kTargetSrcAreaParty            = 95,
  kTargetSrcAreaRaid             = 96,
  kTargetChannelTarget           = 99,
  kTargetDestAreaEnemyAirBurst   = 103,
  kTargetDestAreaAllyAirBurst    = 104,
  kTargetDestAreaPartyAirBurst   = 105,
  kTargetDestAreaRaidAirBurst    = 106,
  kTargetDestDestAlly            = 107,
  kTargetDestDestEnemy           = 108,
  kTargetDestDestParty           = 109,
  kTargetDestDestRaid            = 110,
  kTargetUnitAtLocation          = 117,
};

constexpr std::uint32_t kDefaultAoeHostileCap  = 10;
constexpr std::uint32_t kDefaultAoeFriendlyCap = 10;
constexpr std::uint32_t kUnlimitedAoECap       = 0xFFFFFFFFu;

struct ChainSpellParams {

  std::uint32_t max_jumps   = 0;

  float bounce_distance     = 10.0f;

  float damage_mult_per_jump = 1.0f;

  float total_multiplier     = 1.0f;

  bool initial_target_in_range = true;
};

enum class TargetSelectionMode : std::uint8_t {
  kDefault       = 0,
  kNearest       = 1,
  kFarthest      = 2,
  kLowestHealth  = 3,
  kHighestHealth = 4,
  kThreat        = 5,
  kRandom        = 6,
};

enum class SpellFacingRequirement : std::uint8_t {
  kNone         = 0,
  kInFront      = 1,
  kBehind       = 2,
};

enum class SpellTargetingMode : std::uint8_t {
  kNone        = 0,
  kSingle      = 1,
  kSelf        = 2,
  kAoE         = 3,
  kChain       = 4,
  kCone        = 5,
  kBeam        = 6,
  kRain        = 7,
};

constexpr std::uint32_t GetAoeTargetCap(SpellTargetType target_type) {
  switch (target_type) {
    case kTargetSelf:
    case kTargetUnitPet:
    case kTargetUnitSummoner:
    case kTargetUnitVehicle:
    case kTargetUnitPassenger:
      return 1;
    case kTargetUnitEnemyArea:
    case kTargetUnitConeEnemy:
    case kTargetUnitConeEnemyArea:
    case kTargetUnitAreaEnemy:
    case kTargetDestAreaEnemy:
    case kTargetSrcAreaEnemy:
    case kTargetDestConeEnemy:
    case kTargetDestAreaEnemyAirBurst:
      return kDefaultAoeHostileCap;
    case kTargetUnitAllyArea:
    case kTargetUnitPartyArea:
    case kTargetUnitConeAlly:
    case kTargetUnitConeParty:
    case kTargetUnitAreaAlly:
    case kTargetUnitAreaParty:
    case kTargetUnitAreaRaid:
    case kTargetUnitAllyRaidArea:
    case kTargetDestAreaAlly:
    case kTargetDestAreaParty:
    case kTargetDestAreaRaid:
    case kTargetDestConeAlly:
    case kTargetDestConeParty:
    case kTargetDestConeRaid:
    case kTargetSrcAreaAlly:
    case kTargetSrcAreaParty:
    case kTargetSrcAreaRaid:
    case kTargetDestAreaAllyAirBurst:
    case kTargetDestAreaPartyAirBurst:
    case kTargetDestAreaRaidAirBurst:
      return kDefaultAoeFriendlyCap;
    default:
      return 1;
  }
  return 1;
}

constexpr bool IsAoeTargetType(SpellTargetType target_type) {
  return GetAoeTargetCap(target_type) > 1;
}

constexpr SpellTargetingMode GetTargetingModeForType(SpellTargetType target_type) {
  switch (target_type) {
    case kTargetSelf:
      return SpellTargetingMode::kSelf;
    case kTargetUnitChain:
      return SpellTargetingMode::kChain;
    case kTargetUnitConeEnemy:
    case kTargetUnitConeAlly:
    case kTargetUnitConeParty:
    case kTargetUnitConeEnemyArea:
    case kTargetDestConeEnemy:
    case kTargetDestConeAlly:
    case kTargetDestConeParty:
    case kTargetDestConeRaid:
      return SpellTargetingMode::kCone;
    case kTargetChannelTarget:
    case kTargetUnitAtLocation:
      return SpellTargetingMode::kBeam;
    case kTargetDestAreaEnemyAirBurst:
    case kTargetDestAreaAllyAirBurst:
    case kTargetDestAreaPartyAirBurst:
    case kTargetDestAreaRaidAirBurst:
      return SpellTargetingMode::kRain;
    case kTargetUnitEnemyArea:
    case kTargetUnitAllyArea:
    case kTargetUnitPartyArea:
    case kTargetDestAreaEnemy:
    case kTargetDestAreaAlly:
    case kTargetDestAreaParty:
    case kTargetDestAreaRaid:
    case kTargetSrcAreaEnemy:
    case kTargetSrcAreaAlly:
    case kTargetSrcAreaParty:
    case kTargetSrcAreaRaid:
      return SpellTargetingMode::kAoE;
    default:
      return SpellTargetingMode::kSingle;
  }
  return SpellTargetingMode::kSingle;
}

constexpr ChainSpellParams GetChainParams(
    std::uint32_t max_jumps,
    float bounce_dist,
    float dmg_mult_per_jump) {
  ChainSpellParams params;
  params.max_jumps = max_jumps;
  params.bounce_distance = bounce_dist > 0.0f ? bounce_dist : 10.0f;
  params.damage_mult_per_jump = dmg_mult_per_jump;
  params.total_multiplier = dmg_mult_per_jump;
  return params;
}

}
