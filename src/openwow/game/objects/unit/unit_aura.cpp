#include "openwow/game/objects/unit/unit_aura.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/world_session.h"

#include <cstring>

namespace openwow::game {

namespace {

constexpr std::uint32_t kAuraFlagNegative = 0x80u;
constexpr std::uint32_t kAuraModQuestExperiencePercent = 0x123u;

}

UnitAuraComponent &CGUnit_C::Auras() noexcept { return aura_; }

const UnitAuraComponent &CGUnit_C::Auras() const noexcept { return aura_; }

void UnitAuraComponent::RebuildCache() const {
  if (!cache_dirty_) {
    return;
  }
  cached_buffs_.clear();
  cached_debuffs_.clear();
  for (const auto &aura : auras_) {
    if ((aura.flags & kAuraFlagNegative) != 0) {
      cached_debuffs_.push_back(aura);
    } else {
      cached_buffs_.push_back(aura);
    }
  }
  cache_dirty_ = false;
}

std::vector<AuraInfo> UnitAuraComponent::Buffs() const {
  RebuildCache();
  return cached_buffs_;
}

std::vector<AuraInfo> UnitAuraComponent::Debuffs() const {
  RebuildCache();
  return cached_debuffs_;
}

std::size_t UnitAuraComponent::NumBuffs() const {
  RebuildCache();
  return cached_buffs_.size();
}

std::size_t UnitAuraComponent::NumDebuffs() const {
  RebuildCache();
  return cached_debuffs_.size();
}

const AuraInfo *UnitAuraComponent::BuffAt(std::size_t index) const {
  RebuildCache();
  return index < cached_buffs_.size() ? &cached_buffs_[index] : nullptr;
}

const AuraInfo *UnitAuraComponent::DebuffAt(std::size_t index) const {
  RebuildCache();
  return index < cached_debuffs_.size() ? &cached_debuffs_[index] : nullptr;
}

float UnitAuraComponent::QuestExperienceMultiplier(const CGUnit_C &unit) const {
  if (!unit.IsPlayer()) {
    return 1.0f;
  }
  const auto *const dbc = unit.dbc_loader();
  if (dbc == nullptr) {
    return 1.0f;
  }

  float multiplier = 1.0f;
  for (const AuraInfo &aura : auras_) {
    const auto *const spell = dbc->spell().LookupEntry(aura.spell_id);
    if (spell == nullptr) {
      continue;
    }

    for (std::size_t effect_index = 0u;
         effect_index < spell->effect_apply_aura.size(); ++effect_index) {
      if (spell->effect_apply_aura[effect_index] !=
          kAuraModQuestExperiencePercent) {
        continue;
      }

      const std::uint32_t wrapped =
          static_cast<std::uint32_t>(spell->effect_base_points[effect_index]) + 1u;
      std::int32_t signed_points = 0;
      std::memcpy(&signed_points, &wrapped, sizeof(signed_points));
      multiplier += static_cast<float>(signed_points) * 0.01f;
    }
  }
  return multiplier;
}

void UnitAuraComponent::ReschedulePeriodicClientAuras(
    const CGUnit_C &unit, const WorldSession &session) {
  if (auras_.empty()) {
    return;
  }
  if (unit.dbc_loader() == nullptr) {
    return;
  }

  const auto &spell_store = unit.dbc_loader()->spell();
  const ObjectGuid unit_guid = unit.GetGuid();

  for (std::size_t idx = 0; idx < auras_.size(); ++idx) {
    const auto &aura = auras_[idx];
    if (aura.spell_id == 0) {
      continue;
    }

    const auto *spell = spell_store.LookupEntry(aura.spell_id);
    if (spell == nullptr) {
      continue;
    }

    for (std::uint32_t eff = 0; eff < data::dbc::kMaxSpellEffects; ++eff) {
      if (spell->effect_apply_aura[eff] !=
          data::dbc::kSpellAuraPeriodicTriggerSpellFromClient) {
        continue;
      }

      auto amplitude = static_cast<std::int32_t>(spell->effect_amplitude[eff]);
      (void)session.aura().ApplySpellModifierDeltas(
          spell->spell_family_name, *spell,

          SpellModOp::kActivationTime, &amplitude);

      SpellAction_SchedulePeriodicClientSpell(
          unit_guid,
          spell->id,
          spell->effect_trigger_spell[eff],
          static_cast<std::uint32_t>(amplitude));
    }
  }
}

}
