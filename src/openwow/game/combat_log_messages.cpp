
#include "openwow/game/combat_log_messages.h"
#include "openwow/game/chat_display.h"

#include <cstdio>
#include <iterator>

namespace openwow::game {

namespace {

constexpr std::uint32_t kPowerGainTextColor = 0xFFFF8400u;

constexpr std::uint32_t kPetDamageColor = 0xFFFF8400u;

constexpr std::uint32_t kSpellDamageColor = 0xFFFFDE00u;

std::string FormatHealingAmount(std::int32_t heal_amount) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "+%d", heal_amount);
  return buf;
}

std::string FormatSignedAmount(std::int32_t amount) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d", amount);
  return buf;
}

}

std::optional<HealingFctDisplayRequest>
BuildHealingFctDisplay(std::int32_t heal_amount,
                       ObjectGuid target_guid,
                       ObjectGuid active_player_guid,
                       ObjectGuid mouseover_guid,
                       bool critical,
                       bool enable_combat_text,
                       bool target_is_active_player_controlled) {
  if (heal_amount == 0 || !enable_combat_text ||
      target_guid.IsEmpty() || active_player_guid.IsEmpty() ||
      target_guid == active_player_guid || target_guid == mouseover_guid ||
      !target_is_active_player_controlled) {
    return std::nullopt;
  }

  return HealingFctDisplayRequest{
      critical ? HealingFctDisplayType::kPeriodicHealCrit
               : HealingFctDisplayType::kPeriodicHeal,
      FormatHealingAmount(heal_amount),
  };
}

std::optional<StatusFctDisplayRequest>
BuildPowerGainFctDisplay(std::int32_t amount,
                         ObjectGuid target_guid,
                         ObjectGuid active_player_guid,
                         bool show_power_gain_text,
                         bool target_is_player) {
  if (amount == 0 || !show_power_gain_text || !target_is_player) {
    return std::nullopt;
  }

  if (target_guid.IsEmpty() || active_player_guid.IsEmpty()) {
    return std::nullopt;
  }

  if (target_guid != active_player_guid) {
    return std::nullopt;
  }

  return StatusFctDisplayRequest{
      StatusFctDisplayType::kPowerGain,
      FormatSignedAmount(amount),
      kPowerGainTextColor,
  };
}

std::optional<DamageFctDisplayRequest>
BuildDamageFctDisplay(std::int32_t damage,
                      bool is_physical_type,
                      bool is_crit,
                      bool is_direct_player,
                      bool is_periodic,
                      bool cvar_combat_damage,
                      bool cvar_periodic_spells,
                      bool cvar_pet_melee_damage,
                      bool cvar_pet_spell_damage) {
  if (damage == 0 || !cvar_combat_damage) {
    return std::nullopt;
  }

  if (is_periodic && !cvar_periodic_spells) {
    return std::nullopt;
  }

  std::optional<std::uint32_t> color;
  if (is_physical_type) {
    if (is_direct_player) {
      color = std::nullopt;
    } else {
      if (!cvar_pet_melee_damage) {
        return std::nullopt;
      }
      color = kPetDamageColor;
    }
  } else {
    if (!is_direct_player && !cvar_pet_spell_damage) {
      return std::nullopt;
    }
    color = kSpellDamageColor;
  }

  return DamageFctDisplayRequest{
      is_crit ? DamageFctDisplayType::kDamageCrit
              : DamageFctDisplayType::kDamage,
      FormatSignedAmount(damage),
      color,
  };
}

HonorKillFctDisplayRequest
BuildHonorKillFctDisplay(std::int32_t honor_amount,
                         std::uint32_t rank,
                         const std::string& rank_title,
                         const std::string& hk_label,
                         const std::string& dk_label) {

  const char* label;
  if (honor_amount <= 0) {
    label = (!dk_label.empty()) ? dk_label.c_str() : "DK";
  } else {
    label = (!hk_label.empty()) ? hk_label.c_str() : "HK";
  }

  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s: %s", label, rank_title.c_str());

  return HonorKillFctDisplayRequest{
      HonorKillFctDisplayType::kHonorKillText,
      std::string(buf),
      rank,
  };
}

void DisplayHappinessDrain(const ObjectManager& objects,
                           std::uint64_t source_guid,
                           std::uint64_t target_guid,
                           std::int32_t amount,
                           const std::string& source_name,
                           const std::string& target_name,
                           bool is_active_player) {
  std::string message = FormatHappinessDrainMessage(
      source_name, target_name, amount, is_active_player);

  ChatFrame_DisplayMessage(
      objects, message.c_str(), ChatDisplayType::kCombatPet, nullptr, 0,
      nullptr, nullptr, nullptr, source_guid, 0, target_guid, 0, 0, nullptr);
}

std::string FormatHappinessDrainMessage(
    const std::string& source_name,
    const std::string& target_name,
    std::int32_t amount,
    bool is_self) {

  char buf[3000];

  if (is_self) {

    std::snprintf(buf, sizeof(buf), "%s loses %d Happiness.",
                  target_name.c_str(), amount);
  } else {

    std::snprintf(buf, sizeof(buf), "%s causes %s to lose %d Happiness.",
                  source_name.c_str(), target_name.c_str(), amount);
  }

  return std::string(buf);
}

CombatLogEventType DetermineDrainEventType(const bool has_energize,
                                            const bool is_periodic) {
  if (has_energize) {
    return is_periodic ? CombatLogEventType::SPELL_PERIODIC_LEECH
                       : CombatLogEventType::SPELL_LEECH;
  }
  return is_periodic ? CombatLogEventType::SPELL_PERIODIC_DRAIN
                     : CombatLogEventType::SPELL_DRAIN;
}

std::int32_t FloatRoundToInt(const float value) {
  constexpr float kBias = 0.5f;
  double v1;
  if (value < 0.0f) {
    v1 = static_cast<double>(value) + static_cast<double>(value) +
         static_cast<double>(kBias);
  } else {
    v1 = static_cast<double>(value) + static_cast<double>(value) -
         static_cast<double>(kBias);
  }
  return static_cast<std::int32_t>(v1) >> 1;
}

std::uint32_t PowerType_GetDisplayValueDivisor(const std::int32_t power_type) {
  static constexpr std::uint32_t kDivisors[] = {
      1,
      10,
      1,
      1,
      1000,
      1,
      10,
  };
  if (power_type < 0 ||
      power_type >= static_cast<std::int32_t>(std::size(kDivisors))) {
    return 1;
  }
  return kDivisors[power_type];
}

DrainEventInfo ComputeDrainEventInfo(const std::uint32_t drain_amount,
                                      const float leech_coefficient,
                                      const bool is_periodic) {
  const float raw_energize =
      static_cast<float>(static_cast<std::int32_t>(drain_amount)) *
      leech_coefficient;
  const std::int32_t energize = FloatRoundToInt(raw_energize);
  return {DetermineDrainEventType(energize != 0, is_periodic), energize};
}

}
