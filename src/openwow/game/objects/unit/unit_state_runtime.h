#pragma once

#include "openwow/game/objects/unit/unit_descriptor_view.h"
#include "openwow/game/unit_defines.h"

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
template <typename T> class DbcStore;
struct AreaGroupEntry;
struct AreaTableEntry;
}

namespace openwow::game {

class CGUnit_C;
class ObjectManager;

struct ResistanceDisplayValues {
  std::int32_t base_value = 0;
  std::int32_t clamped_total = 0;
  std::int32_t positive_modifier = 0;
  std::int32_t negative_modifier = 0;
};

struct CreatureClassMetadata {
  std::uint8_t pet_food_mask = 0;
  std::uint8_t family_flags = 0;
  std::uint8_t combat_class = 0xFF;
  std::uint8_t unknown3 = 0;
  std::uint8_t unknown4 = 0;
  std::uint8_t unknown5 = 0;
};

class UnitStateRuntime final {
public:
  explicit UnitStateRuntime(CGUnit_C& owner) noexcept;

  [[nodiscard]] std::uint32_t GetHealth() const;
  [[nodiscard]] std::uint32_t GetMaxHealth() const;
  [[nodiscard]] std::uint32_t GetPower(std::uint8_t power_type) const;
  [[nodiscard]] std::uint32_t GetMaxPower(std::uint8_t power_type) const;
  [[nodiscard]] std::uint8_t GetPowerType() const;
  [[nodiscard]] std::uint32_t GetLevel() const;
  [[nodiscard]] std::uint8_t GetRace() const;
  [[nodiscard]] std::uint8_t GetClass() const;
  [[nodiscard]] std::uint8_t GetGender() const;
  [[nodiscard]] std::uint32_t GetFactionTemplate() const;
  [[nodiscard]] std::uint32_t GetUnitFlags() const;
  [[nodiscard]] std::uint32_t GetUnitFlags2() const;
  [[nodiscard]] std::uint32_t GetDynamicFlags() const;
  [[nodiscard]] std::uint32_t GetNpcFlags() const;
  [[nodiscard]] bool HasUnitFlag(std::uint32_t flag) const;

  [[nodiscard]] ObjectGuid GetTarget() const;
  [[nodiscard]] ObjectGuid GetSummonedBy() const;
  [[nodiscard]] ObjectGuid GetCharmedBy() const;
  [[nodiscard]] ObjectGuid GetCharmedUnitGUID() const;
  [[nodiscard]] ObjectGuid GetPrimaryControlledUnitGUID() const;
  [[nodiscard]] ObjectGuid GetCharmedByOrCreatedByGUID() const;
  [[nodiscard]] bool HasCharmedByGUID() const;
  [[nodiscard]] ObjectGuid GetCreatedBy() const;
  [[nodiscard]] ObjectGuid GetPetGUID() const;
  [[nodiscard]] ObjectGuid GetCritterGUID() const;

  [[nodiscard]] float GetCombatReach() const;
  [[nodiscard]] float GetBoundingRadius() const;
  [[nodiscard]] DamageRange GetMainHandDamage() const;
  [[nodiscard]] DamageRange GetOffHandDamage() const;
  [[nodiscard]] DamageRange GetRangedDamage() const;
  [[nodiscard]] float GetMinDamage() const;
  [[nodiscard]] float GetMaxDamage() const;
  [[nodiscard]] float GetMinOffHandDamage() const;
  [[nodiscard]] float GetMaxOffHandDamage() const;
  [[nodiscard]] std::uint32_t GetAttackSpeed(AttackSlot slot) const;
  [[nodiscard]] std::uint32_t GetBaseAttackTime() const;
  [[nodiscard]] std::uint32_t GetRangedAttackTime() const;

  [[nodiscard]] std::uint32_t GetPetNumber() const;
  [[nodiscard]] std::uint32_t GetPetNameTimestamp() const;
  [[nodiscard]] std::uint32_t GetPetExperience() const;
  [[nodiscard]] std::uint32_t GetPetNextLevelExp() const;
  [[nodiscard]] std::uint8_t GetPetTalentPoints() const;
  [[nodiscard]] std::uint8_t GetVisFlags() const;
  [[nodiscard]] std::uint8_t GetAnimTier() const;
  [[nodiscard]] std::uint8_t GetSheathState() const;
  [[nodiscard]] std::uint8_t GetPvPFlags() const;
  [[nodiscard]] std::uint8_t GetPetFlags() const;

  [[nodiscard]] std::uint32_t GetVirtualItemSlotEntry(std::uint8_t slot) const;
  [[nodiscard]] std::uint32_t GetAuraState() const;
  [[nodiscard]] bool HasAuraState(std::uint32_t state) const;

  [[nodiscard]] float GetMaxHealthModifier() const;
  [[nodiscard]] float GetHoverHeight() const;
  [[nodiscard]] std::int32_t GetResistanceBuffModPositive(std::uint8_t school) const;
  [[nodiscard]] std::int32_t GetResistanceBuffModNegative(std::uint8_t school) const;
  [[nodiscard]] ResistanceDisplayValues GetResistanceDisplayValues(std::uint8_t school) const;
  [[nodiscard]] std::int32_t GetPowerCostModifier(std::uint8_t school) const;
  [[nodiscard]] float GetPowerCostMultiplier(std::uint8_t school) const;
  [[nodiscard]] float GetMinRangedDamage() const;
  [[nodiscard]] float GetMaxRangedDamage() const;
  [[nodiscard]] std::int32_t GetMeleeAttackPower() const;
  [[nodiscard]] std::int32_t GetRangedAttackPower() const;
  [[nodiscard]] std::int32_t GetAttackPower() const;
  [[nodiscard]] std::int32_t GetArmor() const;
  [[nodiscard]] std::int32_t GetResistance(std::uint8_t school) const;
  [[nodiscard]] std::int32_t GetStat(std::uint8_t stat) const;
  [[nodiscard]] std::int32_t GetNonNegativeStat(std::uint8_t stat) const;
  [[nodiscard]] std::int32_t GetPosStat(std::uint8_t stat) const;
  [[nodiscard]] std::int32_t GetNegStat(std::uint8_t stat) const;
  [[nodiscard]] float GetMeleeHaste() const;
  [[nodiscard]] float GetRangedHaste() const;
  [[nodiscard]] float GetSpellHaste() const;
  [[nodiscard]] std::int32_t GetBaseMana() const;
  [[nodiscard]] std::int32_t GetBaseHealth() const;
  [[nodiscard]] std::uint32_t GetActivePlayerTrackCreatureMask() const;

  [[nodiscard]] bool IsInCombat() const;
  [[nodiscard]] bool IsDead() const;
  [[nodiscard]] bool IsDeadOrGhost() const;
  [[nodiscard]] bool IsLootableCorpseAt(std::uint32_t current_tick_ms) const;
  [[nodiscard]] bool IsLootableCorpseNow() const;
  [[nodiscard]] bool IsFeignDeath() const;
  [[nodiscard]] bool HasUnitState(UnitStateFlag state) const;
  [[nodiscard]] bool IsSitting() const;
  [[nodiscard]] bool IsInAnySittingStandState() const;
  [[nodiscard]] bool IsStealth() const;
  [[nodiscard]] bool IsTapped() const;
  [[nodiscard]] bool IsTappedByPlayer() const;
  [[nodiscard]] bool IsTappedByAllThreatList() const;
  [[nodiscard]] bool IsTappedByOther() const;
  [[nodiscard]] bool IsConfused() const;
  [[nodiscard]] bool IsFleeing() const;
  [[nodiscard]] bool IsPossessed() const;
  [[nodiscard]] bool IsNotSelectable() const;
  [[nodiscard]] bool IsSkinnable() const;
  [[nodiscard]] bool IsDisarmed() const;
  [[nodiscard]] bool IsSilenced() const;
  [[nodiscard]] bool IsPacified() const;
  [[nodiscard]] bool IsStunned() const;
  [[nodiscard]] bool IsTaxiFlight() const;
  [[nodiscard]] bool IsPvPFreeForAll() const;
  [[nodiscard]] bool IsPvP() const;

  [[nodiscard]] bool HasCreatureTemplateTypeFlag(std::uint32_t flag) const;
  [[nodiscard]] bool CanActWhileMounted() const;
  [[nodiscard]] bool IsCivilian() const;
  [[nodiscard]] bool CanBeAssistedByPlayerSpell() const;
  [[nodiscard]] bool HasDeadInteractTypeFlag() const;
  [[nodiscard]] bool IsLinkAll() const;
  [[nodiscard]] bool HasNoShadowBlob() const;
  [[nodiscard]] bool HasHideFactionTooltipFlag() const;
  [[nodiscard]] ClassificationRank GetClassificationRank() const;
  [[nodiscard]] bool ShouldSuppressDeathCombatLog() const;
  [[nodiscard]] float GetCreatureHealthModifier() const;
  [[nodiscard]] float GetCreaturePowerModifier() const;

  [[nodiscard]] CreatureTypeId GetCreatureType() const;
  [[nodiscard]] bool IsHunterPet() const;
  [[nodiscard]] bool HasQuestGiverWithActiveOverlay() const;
  [[nodiscard]] bool IsPvPFlaggedRacialLeader() const;
  [[nodiscard]] const char* GetCreatureSubnameForDisplay() const;
  [[nodiscard]] std::uint32_t GetPetHappinessLevel() const;

  void GetRangedAttackSkillIfPlayer(const data::dbc::DbcLoader& dbc,
                                    int* out_base_skill, int* out_modifier) const;
  [[nodiscard]] static float GetManaRegenRateFromSpirit(float gt_value, std::int32_t spirit,
                                                         std::int32_t intellect);
  [[nodiscard]] static float GetHealthRegenRateFromSpirit(float gt_per_spi, float gt_base,
                                                           std::int32_t spirit,
                                                           std::int32_t base_spirit_cap);
  [[nodiscard]] static float GetCritChanceFromAgility(float gt_per_agi, float gt_base,
                                                       std::int32_t agility);
  [[nodiscard]] static float GetSpellCritChanceFromIntellect(float gt_per_int, float gt_base,
                                                              std::int32_t intellect);
  [[nodiscard]] static bool IsAreaInAreaGroup(
      const data::dbc::DbcStore<data::dbc::AreaTableEntry>& areas,
      const data::dbc::DbcStore<data::dbc::AreaGroupEntry>& groups,
      std::int32_t area_id, std::int32_t group_id, std::uint32_t* out_count);
  [[nodiscard]] static bool IsStaleGuid(const ObjectManager& objects, ObjectGuid guid);

  [[nodiscard]] std::uint32_t GetSpellStateFlags() const noexcept { return spell_state_flags_; }
  void SetSpellStateFlags(std::uint32_t flags) noexcept { spell_state_flags_ = flags; }
  [[nodiscard]] bool HasSpellStateFlags(std::uint32_t flags) const noexcept {
    return (spell_state_flags_ & flags) != 0u;
  }
  void AddSpellStateFlags(std::uint32_t flags) noexcept { spell_state_flags_ |= flags; }
  void ClearSpellStateFlags(std::uint32_t flags) noexcept { spell_state_flags_ &= ~flags; }
  void ReplaceSpellStateFlags(std::uint32_t mask, std::uint32_t flags) noexcept {
    spell_state_flags_ = (spell_state_flags_ & ~mask) | flags;
  }
  [[nodiscard]] bool HasForcedVehicleTransition() const noexcept;
  void SetForcedVehicleTransition(bool enabled) noexcept;
  void SetComboPointAuraActive(bool active) noexcept;
  [[nodiscard]] bool SuppressesCurrentFormSpellQueries() const noexcept {
    return auto_learn_processed_;
  }
  void SetAutoLearnProcessed() noexcept { auto_learn_processed_ = true; }
  void ClearAutoLearnProcessed() noexcept { auto_learn_processed_ = false; }

  void ResetCreatureMetadata() noexcept;
  [[nodiscard]] const CreatureClassMetadata& CreatureMetadata() const noexcept {
    return creature_metadata_;
  }
  void SetCreatureMetadata(const CreatureClassMetadata& metadata) noexcept {
    creature_metadata_ = metadata;
  }
  [[nodiscard]] std::uint8_t CreatureCombatClass() const noexcept {
    return creature_metadata_.combat_class;
  }

private:
  CGUnit_C& owner_;
  UnitDescriptorView descriptor_;
  std::uint32_t spell_state_flags_{0};
  bool auto_learn_processed_{false};
  CreatureClassMetadata creature_metadata_;
};

}
