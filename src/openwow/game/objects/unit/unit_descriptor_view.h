#pragma once

#include "openwow/game/objects/cgobject.h"
#include "openwow/game/update_fields.h"

#include <bit>
#include <cstdint>

namespace openwow::game {

class UnitDescriptorView final {
public:
  explicit UnitDescriptorView(const CGObject_C &object) noexcept
      : object_(object) {}

  [[nodiscard]] std::uint32_t Health() const {
    return object_.GetUInt32(UNIT_FIELD_HEALTH);
  }
  [[nodiscard]] std::uint32_t MaxHealth() const {
    return object_.GetUInt32(UNIT_FIELD_MAXHEALTH);
  }
  [[nodiscard]] std::uint32_t Power(std::uint8_t type) const {
    return type <= 6
               ? object_.GetUInt32(
                     static_cast<std::uint16_t>(UNIT_FIELD_POWER1 + type))
               : 0u;
  }
  [[nodiscard]] std::uint32_t MaxPower(std::uint8_t type) const {
    return type <= 6
               ? object_.GetUInt32(
                     static_cast<std::uint16_t>(UNIT_FIELD_MAXPOWER1 + type))
               : 0u;
  }

  [[nodiscard]] std::uint8_t PowerType() const {
    return PackedByte(UNIT_FIELD_BYTES_0, 3);
  }
  [[nodiscard]] std::uint8_t Race() const {
    return PackedByte(UNIT_FIELD_BYTES_0, 0);
  }
  [[nodiscard]] std::uint8_t Class() const {
    return PackedByte(UNIT_FIELD_BYTES_0, 1);
  }
  [[nodiscard]] std::uint8_t Gender() const {
    return PackedByte(UNIT_FIELD_BYTES_0, 2);
  }
  [[nodiscard]] std::uint8_t PetTalentPoints() const {
    return PackedByte(UNIT_FIELD_BYTES_1, 1);
  }
  [[nodiscard]] std::uint8_t VisibilityFlags() const {
    return PackedByte(UNIT_FIELD_BYTES_1, 2);
  }
  [[nodiscard]] std::uint8_t AnimationTier() const {
    return PackedByte(UNIT_FIELD_BYTES_1, 3);
  }
  [[nodiscard]] std::uint8_t SheathState() const {
    return PackedByte(UNIT_FIELD_BYTES_2, 0);
  }
  [[nodiscard]] std::uint8_t PvpFlags() const {
    return PackedByte(UNIT_FIELD_BYTES_2, 1);
  }
  [[nodiscard]] std::uint8_t PetFlags() const {
    return PackedByte(UNIT_FIELD_BYTES_2, 2);
  }

  [[nodiscard]] std::uint32_t Level() const {
    return object_.GetUInt32(UNIT_FIELD_LEVEL);
  }
  [[nodiscard]] std::uint32_t FactionTemplate() const {
    return object_.GetUInt32(UNIT_FIELD_FACTIONTEMPLATE);
  }
  [[nodiscard]] std::uint32_t UnitFlags() const {
    return object_.GetUInt32(UNIT_FIELD_FLAGS);
  }
  [[nodiscard]] std::uint32_t UnitFlags2() const {
    return object_.GetUInt32(UNIT_FIELD_FLAGS_2);
  }
  [[nodiscard]] std::uint32_t DynamicFlags() const {
    return object_.GetUInt32(UNIT_DYNAMIC_FLAGS);
  }
  [[nodiscard]] std::uint32_t NpcFlags() const {
    return object_.GetUInt32(UNIT_NPC_FLAGS);
  }
  [[nodiscard]] std::uint32_t AuraState() const {
    return object_.GetUInt32(UNIT_FIELD_AURASTATE);
  }
  [[nodiscard]] std::uint32_t PetNumber() const {
    return object_.GetUInt32(UNIT_FIELD_PETNUMBER);
  }
  [[nodiscard]] std::uint32_t PetNameTimestamp() const {
    return object_.GetUInt32(UNIT_FIELD_PET_NAME_TIMESTAMP);
  }
  [[nodiscard]] std::uint32_t PetExperience() const {
    return object_.GetUInt32(UNIT_FIELD_PETEXPERIENCE);
  }
  [[nodiscard]] std::uint32_t PetNextLevelExperience() const {
    return object_.GetUInt32(UNIT_FIELD_PETNEXTLEVELEXP);
  }
  [[nodiscard]] std::uint32_t PlayerFlags() const {
    return object_.GetUInt32(PLAYER_FLAGS);
  }
  [[nodiscard]] std::uint32_t TrackedCreatureMask() const {
    return object_.GetUInt32(PLAYER_TRACK_CREATURES);
  }

  [[nodiscard]] float CombatReach() const { return object_.GetFloat(UNIT_FIELD_COMBATREACH); }
  [[nodiscard]] float BoundingRadius() const { return object_.GetFloat(UNIT_FIELD_BOUNDINGRADIUS); }
  [[nodiscard]] float MinDamage() const { return object_.GetFloat(UNIT_FIELD_MINDAMAGE); }
  [[nodiscard]] float MaxDamage() const { return object_.GetFloat(UNIT_FIELD_MAXDAMAGE); }
  [[nodiscard]] float MinOffHandDamage() const { return object_.GetFloat(UNIT_FIELD_MINOFFHANDDAMAGE); }
  [[nodiscard]] float MaxOffHandDamage() const { return object_.GetFloat(UNIT_FIELD_MAXOFFHANDDAMAGE); }
  [[nodiscard]] float MinRangedDamage() const { return object_.GetFloat(UNIT_FIELD_MINRANGEDDAMAGE); }
  [[nodiscard]] float MaxRangedDamage() const { return object_.GetFloat(UNIT_FIELD_MAXRANGEDDAMAGE); }
  [[nodiscard]] std::uint32_t AttackTime(std::uint8_t slot) const {
    if (slot <= 1u) {
      return object_.GetUInt32(static_cast<std::uint16_t>(UNIT_FIELD_BASEATTACKTIME + slot));
    }
    return slot == 2u ? object_.GetUInt32(UNIT_FIELD_RANGEDATTACKTIME) : 0u;
  }

  [[nodiscard]] std::uint32_t VirtualItemSlotEntry(std::uint8_t slot) const {
    return slot <= 2u
               ? object_.GetUInt32(static_cast<std::uint16_t>(UNIT_VIRTUAL_ITEM_SLOT_ID + slot))
               : 0u;
  }
  [[nodiscard]] float MaxHealthModifier() const { return object_.GetFloat(UNIT_FIELD_MAXHEALTHMODIFIER); }
  [[nodiscard]] float HoverHeight() const { return object_.GetFloat(UNIT_FIELD_HOVERHEIGHT); }
  [[nodiscard]] std::int32_t Resistance(std::uint8_t school) const {
    return school <= 6u ? Signed(UNIT_FIELD_RESISTANCES, school) : 0;
  }
  [[nodiscard]] std::int32_t PositiveResistanceModifier(std::uint8_t school) const {
    return school <= 6u ? Signed(UNIT_FIELD_RESISTANCEBUFFMODSPOSITIVE, school) : 0;
  }
  [[nodiscard]] std::int32_t NegativeResistanceModifier(std::uint8_t school) const {
    return school <= 6u ? Signed(UNIT_FIELD_RESISTANCEBUFFMODSNEGATIVE, school) : 0;
  }
  [[nodiscard]] std::int32_t PowerCostModifier(std::uint8_t school) const {
    return school <= 6u ? Signed(UNIT_FIELD_POWER_COST_MODIFIER, school) : 0;
  }
  [[nodiscard]] float PowerCostMultiplier(std::uint8_t school) const {
    return school <= 6u ? Float(UNIT_FIELD_POWER_COST_MULTIPLIER, school) : 0.0f;
  }
  [[nodiscard]] std::int32_t Stat(std::uint8_t stat) const {
    return stat <= 4u ? Signed(UNIT_FIELD_STAT0, stat) : 0;
  }
  [[nodiscard]] std::int32_t PositiveStat(std::uint8_t stat) const {
    return stat <= 4u ? Signed(UNIT_FIELD_POSSTAT0, stat) : 0;
  }
  [[nodiscard]] std::int32_t NegativeStat(std::uint8_t stat) const {
    return stat <= 4u ? Signed(UNIT_FIELD_NEGSTAT0, stat) : 0;
  }
  [[nodiscard]] std::int32_t MeleeAttackPowerBase() const { return Signed(UNIT_FIELD_ATTACK_POWER); }
  [[nodiscard]] std::int32_t MeleeAttackPowerModifier() const { return Signed(UNIT_FIELD_ATTACK_POWER_MODS); }
  [[nodiscard]] std::int32_t RangedAttackPowerBase() const { return Signed(UNIT_FIELD_RANGED_ATTACK_POWER); }
  [[nodiscard]] std::int32_t RangedAttackPowerModifier() const { return Signed(UNIT_FIELD_RANGED_ATTACK_POWER_MODS); }
  [[nodiscard]] float MeleeHaste() const { return object_.GetFloat(UNIT_FIELD_ATTACK_POWER_MULTIPLIER); }
  [[nodiscard]] float RangedHaste() const { return object_.GetFloat(UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER); }
  [[nodiscard]] float SpellHaste() const { return object_.GetFloat(UNIT_MOD_CAST_SPEED); }
  [[nodiscard]] std::int32_t BaseMana() const { return Signed(UNIT_FIELD_BASE_MANA); }
  [[nodiscard]] std::int32_t BaseHealth() const { return Signed(UNIT_FIELD_BASE_HEALTH); }

  [[nodiscard]] ObjectGuid Target() const {
    return object_.GetGuidField(UNIT_FIELD_TARGET);
  }
  [[nodiscard]] ObjectGuid SummonedBy() const {
    return object_.GetGuidField(UNIT_FIELD_SUMMONEDBY);
  }
  [[nodiscard]] ObjectGuid CharmedBy() const {
    return object_.GetGuidField(UNIT_FIELD_CHARMEDBY);
  }
  [[nodiscard]] ObjectGuid CharmedUnit() const {
    return object_.GetGuidField(UNIT_FIELD_CHARM);
  }
  [[nodiscard]] ObjectGuid CreatedBy() const {
    return object_.GetGuidField(UNIT_FIELD_CREATEDBY);
  }
  [[nodiscard]] ObjectGuid Pet() const {
    return object_.GetGuidField(UNIT_FIELD_SUMMON);
  }
  [[nodiscard]] ObjectGuid Critter() const {
    return object_.GetGuidField(UNIT_FIELD_CRITTER);
  }

private:
  [[nodiscard]] std::int32_t Signed(const std::uint16_t field,
                                    const std::uint8_t offset = 0u) const {
    return std::bit_cast<std::int32_t>(object_.GetUInt32(
        static_cast<std::uint16_t>(field + offset)));
  }
  [[nodiscard]] float Float(const std::uint16_t field,
                            const std::uint8_t offset) const {
    return object_.GetFloat(static_cast<std::uint16_t>(field + offset));
  }
  [[nodiscard]] std::uint8_t PackedByte(const std::uint16_t field,
                                        const std::uint8_t byte_index) const {
    const auto shift = static_cast<std::uint32_t>(byte_index) * 8u;
    return static_cast<std::uint8_t>(
        (object_.GetUInt32(field) >> shift) & 0xFFu);
  }

  const CGObject_C &object_;
};

}
