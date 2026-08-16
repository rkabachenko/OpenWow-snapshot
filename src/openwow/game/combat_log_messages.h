
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "openwow/game/combat_log.h"
#include "openwow/game/object_guid.h"

namespace openwow::game {

class ObjectManager;

enum class HealingFctDisplayType : std::uint32_t {
  kPeriodicHeal = 6,
  kPeriodicHealCrit = 7,
  kSpellHeal = 9,
};

enum class DamageFctDisplayType : std::uint32_t {
  kDamage = 0,
  kDamageCrit = 2,
};

enum class HonorKillFctDisplayType : std::uint32_t {
  kHonorKillText = 5,
};

enum class StatusFctDisplayType : std::uint32_t {
  kPowerGain = 8,
};

enum class ChatMessageType : std::uint32_t {
  kCombatPetInfo = 31,
};

struct HealingFctDisplayRequest {
  HealingFctDisplayType type;
  std::string text;
};

struct DamageFctDisplayRequest {
  DamageFctDisplayType type;
  std::string text;
  std::optional<std::uint32_t> color;
};

struct StatusFctDisplayRequest {
  StatusFctDisplayType type;
  std::string text;
  std::uint32_t color;
};

struct HonorKillFctDisplayRequest {
  HonorKillFctDisplayType type;
  std::string text;
  std::optional<std::uint32_t> rank;
};

[[nodiscard]] std::optional<HealingFctDisplayRequest>
BuildHealingFctDisplay(std::int32_t heal_amount,
                       ObjectGuid target_guid,
                       ObjectGuid active_player_guid,
                       ObjectGuid mouseover_guid,
                       bool critical,
                       bool enable_combat_text,
                       bool target_is_active_player_controlled);

[[nodiscard]] std::optional<StatusFctDisplayRequest>
BuildPowerGainFctDisplay(std::int32_t amount,
                         ObjectGuid target_guid,
                         ObjectGuid active_player_guid,
                         bool show_power_gain_text,
                         bool target_is_player);

[[nodiscard]] std::optional<DamageFctDisplayRequest>
BuildDamageFctDisplay(std::int32_t damage,
                      bool is_physical_type,
                      bool is_crit,
                      bool is_direct_player,
                      bool is_periodic,
                      bool cvar_combat_damage,
                      bool cvar_periodic_spells,
                      bool cvar_pet_melee_damage,
                      bool cvar_pet_spell_damage);

[[nodiscard]] HonorKillFctDisplayRequest
BuildHonorKillFctDisplay(std::int32_t honor_amount,
                         std::uint32_t rank,
                         const std::string& rank_title,
                         const std::string& hk_label = {},
                         const std::string& dk_label = {});

void DisplayHappinessDrain(const ObjectManager& objects,
                           std::uint64_t source_guid,
                           std::uint64_t target_guid,
                           std::int32_t amount,
                           const std::string& source_name,
                           const std::string& target_name,
                           bool is_active_player);

std::string FormatHappinessDrainMessage(
    const std::string& source_name,
    const std::string& target_name,
    std::int32_t amount,
    bool is_self);

[[nodiscard]] CombatLogEventType DetermineDrainEventType(bool has_energize,
                                                          bool is_periodic);

[[nodiscard]] std::int32_t FloatRoundToInt(float value);

[[nodiscard]] std::uint32_t PowerType_GetDisplayValueDivisor(
    std::int32_t power_type);

struct DrainEventInfo {
  CombatLogEventType event_type;
  std::int32_t energize_amount;
};

[[nodiscard]] DrainEventInfo ComputeDrainEventInfo(std::uint32_t drain_amount,
                                                    float leech_coefficient,
                                                    bool is_periodic);

}
