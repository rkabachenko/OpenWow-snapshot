#include "openwow/game/objects/cgunit.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/unit/unit_descriptor_view.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

UnitStateRuntime::UnitStateRuntime(CGUnit_C& owner) noexcept
    : owner_(owner), descriptor_(owner) {}

std::uint32_t UnitStateRuntime::GetHealth() const {
  return owner_.Vitals().IsActive() ? owner_.Vitals().Get(-2)
                                    : descriptor_.Health();
}

std::uint32_t UnitStateRuntime::GetMaxHealth() const {
  return descriptor_.MaxHealth();
}

std::uint32_t UnitStateRuntime::GetLevel() const { return descriptor_.Level(); }

std::uint32_t UnitStateRuntime::GetFactionTemplate() const {
  return descriptor_.FactionTemplate();
}

std::uint32_t UnitStateRuntime::GetPower(const std::uint8_t power_type) const {
  return descriptor_.Power(power_type);
}

std::uint32_t UnitStateRuntime::GetMaxPower(const std::uint8_t power_type) const {
  return descriptor_.MaxPower(power_type);
}

std::uint8_t UnitStateRuntime::GetPowerType() const {
  return descriptor_.PowerType();
}

std::uint8_t UnitStateRuntime::GetRace() const {
  return descriptor_.Race();
}

std::uint8_t UnitStateRuntime::GetClass() const {
  return descriptor_.Class();
}

std::uint8_t UnitStateRuntime::GetGender() const {
  return descriptor_.Gender();
}

std::uint32_t UnitStateRuntime::GetUnitFlags() const {
  return descriptor_.UnitFlags();
}

std::uint32_t UnitStateRuntime::GetUnitFlags2() const {
  return descriptor_.UnitFlags2();
}

std::uint32_t UnitStateRuntime::GetDynamicFlags() const {
  return descriptor_.DynamicFlags();
}

std::uint32_t UnitStateRuntime::GetNpcFlags() const {
  return descriptor_.NpcFlags();
}

bool UnitStateRuntime::HasUnitFlag(const std::uint32_t flag) const {
  return (GetUnitFlags() & flag) != 0;
}

ObjectGuid UnitStateRuntime::GetTarget() const {
  return descriptor_.Target();
}

ObjectGuid UnitStateRuntime::GetSummonedBy() const {
  return descriptor_.SummonedBy();
}

ObjectGuid UnitStateRuntime::GetCharmedBy() const {
  return descriptor_.CharmedBy();
}

ObjectGuid UnitStateRuntime::GetCharmedUnitGUID() const {
  return descriptor_.CharmedUnit();
}

ObjectGuid UnitStateRuntime::GetPrimaryControlledUnitGUID() const {
  const auto charm_guid = GetCharmedUnitGUID();
  return !charm_guid.IsEmpty() ? charm_guid : GetPetGUID();
}

ObjectGuid UnitStateRuntime::GetCharmedByOrCreatedByGUID() const {
  const auto charmed_by = GetCharmedBy();
  return !charmed_by.IsEmpty() ? charmed_by : GetCreatedBy();
}

ObjectGuid UnitStateRuntime::GetCreatedBy() const {
  return descriptor_.CreatedBy();
}

ObjectGuid UnitStateRuntime::GetPetGUID() const {
  return descriptor_.Pet();
}

ObjectGuid UnitStateRuntime::GetCritterGUID() const {
  if (!descriptor_.CharmedUnit().IsEmpty() || !descriptor_.Pet().IsEmpty()) {
    return {};
  }
  return descriptor_.Critter();
}

float UnitStateRuntime::GetCombatReach() const {
  return descriptor_.CombatReach();
}

float UnitStateRuntime::GetBoundingRadius() const {
  return descriptor_.BoundingRadius();
}

DamageRange UnitStateRuntime::GetMainHandDamage() const {
  return {descriptor_.MinDamage(), descriptor_.MaxDamage()};
}

DamageRange UnitStateRuntime::GetOffHandDamage() const {
  return {descriptor_.MinOffHandDamage(), descriptor_.MaxOffHandDamage()};
}

DamageRange UnitStateRuntime::GetRangedDamage() const {
  return {descriptor_.MinRangedDamage(), descriptor_.MaxRangedDamage()};
}

float UnitStateRuntime::GetMinDamage() const { return descriptor_.MinDamage(); }
float UnitStateRuntime::GetMaxDamage() const { return descriptor_.MaxDamage(); }
float UnitStateRuntime::GetMinOffHandDamage() const {
  return descriptor_.MinOffHandDamage();
}
float UnitStateRuntime::GetMaxOffHandDamage() const {
  return descriptor_.MaxOffHandDamage();
}

std::uint32_t UnitStateRuntime::GetAttackSpeed(const AttackSlot slot) const {
  switch (slot) {
  case kAttackSlotMainHand:
    return descriptor_.AttackTime(0u);
  case kAttackSlotOffHand:
    return descriptor_.AttackTime(1u);
  case kAttackSlotRanged:
    return descriptor_.AttackTime(2u);
  default:
    return 0;
  }
}

std::uint32_t UnitStateRuntime::GetBaseAttackTime() const {
  return descriptor_.AttackTime(0u);
}

std::uint32_t UnitStateRuntime::GetRangedAttackTime() const {
  return descriptor_.AttackTime(2u);
}

std::uint32_t UnitStateRuntime::GetPetNumber() const {
  return descriptor_.PetNumber();
}

std::uint32_t UnitStateRuntime::GetPetNameTimestamp() const {
  return descriptor_.PetNameTimestamp();
}

std::uint32_t UnitStateRuntime::GetPetExperience() const {
  return descriptor_.PetExperience();
}

std::uint32_t UnitStateRuntime::GetPetNextLevelExp() const {
  return descriptor_.PetNextLevelExperience();
}

std::uint8_t UnitStateRuntime::GetPetTalentPoints() const {
  return descriptor_.PetTalentPoints();
}

std::uint8_t UnitStateRuntime::GetVisFlags() const {
  return descriptor_.VisibilityFlags();
}

std::uint8_t UnitStateRuntime::GetAnimTier() const {
  return descriptor_.AnimationTier();
}

std::uint8_t UnitStateRuntime::GetSheathState() const {
  return descriptor_.SheathState();
}

std::uint8_t UnitStateRuntime::GetPvPFlags() const {
  return descriptor_.PvpFlags();
}

std::uint8_t UnitStateRuntime::GetPetFlags() const {
  return descriptor_.PetFlags();
}

float UnitStateRuntime::GetMaxHealthModifier() const {
  return descriptor_.MaxHealthModifier();
}

float UnitStateRuntime::GetHoverHeight() const {
  return descriptor_.HoverHeight();
}

std::int32_t UnitStateRuntime::GetResistanceBuffModPositive(
    const std::uint8_t school) const {
  return descriptor_.PositiveResistanceModifier(school);
}

std::int32_t UnitStateRuntime::GetResistanceBuffModNegative(
    const std::uint8_t school) const {
  return descriptor_.NegativeResistanceModifier(school);
}

ResistanceDisplayValues UnitStateRuntime::GetResistanceDisplayValues(
    const std::uint8_t school) const {
  ResistanceDisplayValues values;
  const std::int32_t total = GetResistance(school);
  values.positive_modifier = GetResistanceBuffModPositive(school);
  values.negative_modifier = GetResistanceBuffModNegative(school);
  values.base_value = total - values.positive_modifier - values.negative_modifier;
  values.clamped_total = std::max(total, 0);
  return values;
}

std::int32_t UnitStateRuntime::GetPowerCostModifier(
    const std::uint8_t school) const {
  return descriptor_.PowerCostModifier(school);
}

float UnitStateRuntime::GetPowerCostMultiplier(const std::uint8_t school) const {
  return descriptor_.PowerCostMultiplier(school);
}

float UnitStateRuntime::GetMinRangedDamage() const {
  return descriptor_.MinRangedDamage();
}

float UnitStateRuntime::GetMaxRangedDamage() const {
  return descriptor_.MaxRangedDamage();
}

std::int32_t UnitStateRuntime::GetMeleeAttackPower() const {
  return descriptor_.MeleeAttackPowerBase() +
         descriptor_.MeleeAttackPowerModifier();
}

std::int32_t UnitStateRuntime::GetRangedAttackPower() const {
  return descriptor_.RangedAttackPowerBase() +
         descriptor_.RangedAttackPowerModifier();
}

std::int32_t UnitStateRuntime::GetAttackPower() const { return GetMeleeAttackPower(); }
std::int32_t UnitStateRuntime::GetArmor() const { return GetResistance(0); }

std::int32_t UnitStateRuntime::GetResistance(const std::uint8_t school) const {
  return descriptor_.Resistance(school);
}

std::int32_t UnitStateRuntime::GetStat(const std::uint8_t stat) const {
  return descriptor_.Stat(stat);
}

std::int32_t UnitStateRuntime::GetNonNegativeStat(const std::uint8_t stat) const {
  return std::max(GetStat(stat), 0);
}

std::int32_t UnitStateRuntime::GetPosStat(const std::uint8_t stat) const {
  return descriptor_.PositiveStat(stat);
}

std::int32_t UnitStateRuntime::GetNegStat(const std::uint8_t stat) const {
  return descriptor_.NegativeStat(stat);
}

float UnitStateRuntime::GetMeleeHaste() const {
  return descriptor_.MeleeHaste();
}

float UnitStateRuntime::GetRangedHaste() const {
  return descriptor_.RangedHaste();
}

float UnitStateRuntime::GetSpellHaste() const { return descriptor_.SpellHaste(); }

std::int32_t UnitStateRuntime::GetBaseMana() const {
  return descriptor_.BaseMana();
}

std::int32_t UnitStateRuntime::GetBaseHealth() const {
  return descriptor_.BaseHealth();
}

std::uint32_t UnitStateRuntime::GetActivePlayerTrackCreatureMask() const {
  return owner_.GetGuid() == owner_.GetActivePlayerGuid()
             ? descriptor_.TrackedCreatureMask()
             : 0u;
}

std::uint32_t UnitStateRuntime::GetVirtualItemSlotEntry(const std::uint8_t slot) const {
  return descriptor_.VirtualItemSlotEntry(slot);
}

std::uint32_t UnitStateRuntime::GetAuraState() const {
  return descriptor_.AuraState();
}

bool UnitStateRuntime::HasAuraState(const std::uint32_t state) const {
  return (GetAuraState() & state) != 0u;
}

void UnitStateRuntime::GetRangedAttackSkillIfPlayer(
    const openwow::data::dbc::DbcLoader & ,
    int *out_base_skill, int *out_modifier) const {

  if (!out_base_skill || !out_modifier)
    return;

  const std::uint32_t level = GetLevel();
  *out_base_skill = static_cast<int>(level) * 5;
  *out_modifier = 0;

  if (!owner_.IsPlayer()) {
    return;
  }

  const auto* player = static_cast<const CGPlayer_C*>(&owner_);

  const auto ranged = player->GetVisibleWeaponSlotMetadata(
      2u, true);
  if (!ranged.has_value() ||
      ranged->item_class != static_cast<std::uint32_t>(ItemClass::Weapon)) {
    return;
  }

  std::uint16_t skill_id = 0u;
  switch (ranged->subclass) {
    case 2u:
      skill_id = 46u;
      break;
    case 3u:
      skill_id = 227u;
      break;
    case 16u:
      skill_id = 237u;
      break;
    case 18u:
      skill_id = 226u;
      break;
    case 19u:
      skill_id = 228u;
      break;
    default:
      return;
  }

  const auto slot = player->FindActiveSkillSlot(skill_id);
  if (!slot.has_value()) {
    return;
  }

  const auto skill = player->GetSkill(*slot);
  *out_base_skill = static_cast<int>(skill.value);
  *out_modifier = static_cast<int>(skill.modifier);

}

float UnitStateRuntime::GetManaRegenRateFromSpirit(float gt_value, std::int32_t spirit,
                                           std::int32_t intellect) {
  auto safe_spirit = spirit < 0 ? 0 : spirit;
  auto safe_intel = intellect < 0 ? 0 : intellect;
  return gt_value * std::sqrt(static_cast<float>(safe_intel)) * static_cast<float>(safe_spirit);
}

float UnitStateRuntime::GetHealthRegenRateFromSpirit(float gt_per_spi, float gt_base, std::int32_t spirit,
                                             std::int32_t base_spirit_cap) {

  auto safe_spirit = spirit < 0 ? 0 : spirit;
  auto capped = base_spirit_cap < safe_spirit ? base_spirit_cap : safe_spirit;
  return gt_base * static_cast<float>(capped) +
         gt_per_spi * static_cast<float>(safe_spirit - capped);
}

float UnitStateRuntime::GetCritChanceFromAgility(float gt_per_agi, float gt_base, std::int32_t agility) {

  if (gt_per_agi == 0.0f)
    return 0.0f;
  auto safe_agi = agility < 0 ? 0 : agility;
  return (gt_per_agi * static_cast<float>(safe_agi) + gt_base) * 100.0f;
}

float UnitStateRuntime::GetSpellCritChanceFromIntellect(const float per_intellect,
                                                const float base,
                                                const std::int32_t intellect) {
  if (per_intellect == 0.0f) {
    return 0.0f;
  }
  return (per_intellect * static_cast<float>(std::max(0, intellect)) + base) *
         100.0f;
}

}
