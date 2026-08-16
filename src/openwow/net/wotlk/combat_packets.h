#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/unit_defines.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::net::wotlk {

namespace CombatHitInfo {
inline constexpr std::uint32_t kNormalSwing     = 0x00000000;
inline constexpr std::uint32_t kUnk1            = 0x00000001;
inline constexpr std::uint32_t kAffectsVictim   = 0x00000002;
inline constexpr std::uint32_t kOffhand          = 0x00000004;
inline constexpr std::uint32_t kMiss             = 0x00000010;
inline constexpr std::uint32_t kFullAbsorb       = 0x00000020;
inline constexpr std::uint32_t kPartialAbsorb    = 0x00000040;
inline constexpr std::uint32_t kFullResist       = 0x00000080;
inline constexpr std::uint32_t kPartialResist    = 0x00000100;
inline constexpr std::uint32_t kCriticalHit      = 0x00000200;
inline constexpr std::uint32_t kBlock            = 0x00002000;
inline constexpr std::uint32_t kGlancing         = 0x00010000;
inline constexpr std::uint32_t kCrushing         = 0x00020000;
inline constexpr std::uint32_t kNoAnimation      = 0x00040000;
inline constexpr std::uint32_t kRageGain         = 0x00800000;
inline constexpr std::uint32_t kFakeDamage       = 0x01000000;
}

enum class CombatVictimState : std::uint8_t {
  kIntact    = 0,
  kHit       = 1,
  kDodge     = 2,
  kParry     = 3,
  kInterrupt = 4,
  kBlock     = 5,
  kEvade     = 6,
  kImmune    = 7,
  kDeflect   = 8,
};

struct CombatSubDamage {
  std::uint32_t school_mask{0};
  float         damage_float{0.0f};
  std::uint32_t damage{0};
  std::uint32_t absorbed{0};
  std::uint32_t resisted{0};
};

struct AttackerStateUpdateData {
  std::uint32_t hit_info{0};
  game::ObjectGuid attacker;
  game::ObjectGuid victim;
  std::uint32_t total_damage{0};
  std::uint32_t overkill{0};
  std::vector<CombatSubDamage> sub_damages;
  CombatVictimState victim_state{CombatVictimState::kIntact};
  std::uint32_t attacker_state{0};
  std::uint32_t melee_spell_id{0};
  std::uint32_t blocked_amount{0};
  std::uint32_t rage_gain{0};
};

struct AttackStartData {
  game::ObjectGuid attacker;
  game::ObjectGuid victim;
};

struct AttackStopData {
  game::ObjectGuid attacker;
  game::ObjectGuid victim;
  std::uint32_t now_dead{0};
};

struct PowerUpdateData {
  game::ObjectGuid unit;
  game::PowerType power_type{game::PowerType::kMana};
  std::uint32_t value{0};
};

struct HealthUpdateData {
  game::ObjectGuid unit;
  std::uint32_t health{0};
};

struct AiReactionData {
  game::ObjectGuid unit;
  std::uint32_t reaction{0};
};

struct EnvironmentalDamageData {
  game::ObjectGuid guid;
  std::uint8_t type{0};
  std::uint32_t damage{0};
  std::uint32_t absorbed{0};
  std::uint32_t resisted{0};
};

struct XpGainData {
  game::ObjectGuid victim;
  std::uint32_t xp_total{0};
  std::uint8_t xp_type{0};
  float group_rate{0.0f};
};

struct AuraSlotData {
  std::uint8_t slot{0};
  std::uint32_t spell_id{0};
  std::uint8_t flags{0};
  std::uint8_t caster_level{0};
  std::uint8_t stack_or_charges{0};

  game::ObjectGuid caster_guid;
  std::uint32_t max_duration{0};
  std::uint32_t remaining_duration{0};
};

struct AuraUpdateData {
  game::ObjectGuid target;
  std::vector<AuraSlotData> slots;
};

struct AuraUpdateAllData {
  game::ObjectGuid target;
  std::vector<AuraSlotData> slots;
};

struct ThreatEntryData {
  game::ObjectGuid unit;
  std::uint32_t threat{0};
};

struct ThreatUpdateData {
  game::ObjectGuid target;
  std::vector<ThreatEntryData> entries;
};

struct HighestThreatUpdateData {
  game::ObjectGuid target;
  game::ObjectGuid highest;
  std::vector<ThreatEntryData> entries;
};

struct LevelUpInfoData {
  std::uint32_t level{0};
  std::int32_t health_delta{0};
  std::int32_t mana_delta{0};
  std::int32_t power_delta[6]{};
  std::int32_t stat_delta[5]{};
};

[[nodiscard]] std::optional<AttackerStateUpdateData>
ParseAttackerStateUpdate(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<AttackStartData>
ParseAttackStart(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<AttackStopData>
ParseAttackStop(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<PowerUpdateData>
ParsePowerUpdate(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<HealthUpdateData>
ParseHealthUpdate(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<AiReactionData>
ParseAiReaction(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<EnvironmentalDamageData>
ParseEnvironmentalDamage(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<XpGainData>
ParseLogXpGain(const std::uint8_t* data, std::size_t len);

[[nodiscard]] bool
ParseAuraSlotBlock(const std::uint8_t* data, std::size_t len,
                   std::size_t& offset, AuraSlotData& out,
                   const game::ObjectGuid& target);

[[nodiscard]] std::optional<AuraUpdateData>
ParseAuraUpdate(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<AuraUpdateAllData>
ParseAuraUpdateAll(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<ThreatUpdateData>
ParseThreatUpdate(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<HighestThreatUpdateData>
ParseHighestThreatUpdate(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<LevelUpInfoData>
ParseLevelUpInfo(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::size_t CombatReadPackedGuid(const std::uint8_t* data,
                                                std::size_t len,
                                                std::size_t offset,
                                                game::ObjectGuid& out);

}
