
#include "openwow/game/spell_usability.h"

#include "openwow/game/spell_shapeshift_mask.h"

namespace openwow::game {

const char *PowerTypeToString(PowerType pt) {
  switch (pt) {
  case PowerType::kMana:
    return "Mana";
  case PowerType::kRage:
    return "Rage";
  case PowerType::kFocus:
    return "Focus";
  case PowerType::kEnergy:
    return "Energy";
  case PowerType::kHappiness:
    return "Happiness";
  case PowerType::kRunes:
    return "Runes";
  case PowerType::kRunicPower:
    return "Runic Power";
  case PowerType::kHealth:
    return "Health";
  case PowerType::kMax:
    break;
  }
  return "Unknown";
}

namespace usability_attr {

constexpr std::uint32_t kAttrEx5UsableWhileStunned = 0x00080000;
constexpr std::uint32_t kAttrEx2PetTameSpell = 0x00010000;
}

namespace {

constexpr std::uint8_t kPetTameInvalidCreatureBit = 0x01u;
constexpr std::uint32_t kWeaponItemClass = 2u;

std::uint8_t ScaleRequiredRuneCount(const std::uint8_t base_count, const std::int32_t cost_pct) {
  if (base_count == 0 || cost_pct <= 100) {
    return base_count;
  }

  return static_cast<std::uint8_t>((static_cast<std::int32_t>(base_count) * cost_pct) / 100);
}

bool HasRuneRequirement(const RuneCost &cost) {
  return cost.blood != 0 || cost.unholy != 0 || cost.frost != 0;
}

bool HasRequiredReadyRunes(const SpellUsabilityInfo &spell, const PlayerStateSnapshot &player) {
  if (!spell.has_rune_cost) {
    return false;
  }
  if (spell.rune_cost_pct == 0) {
    return true;
  }

  const RuneCost effective_cost = {
      ScaleRequiredRuneCount(spell.rune_cost.blood, spell.rune_cost_pct),
      ScaleRequiredRuneCount(spell.rune_cost.unholy, spell.rune_cost_pct),
      ScaleRequiredRuneCount(spell.rune_cost.frost, spell.rune_cost_pct),
  };

  if (!HasRuneRequirement(effective_cost)) {
    return true;
  }
  if (!player.has_rune_data) {
    return false;
  }

  int death_available = player.ready_runes.death;

  const int blood_deficit = static_cast<int>(effective_cost.blood) - player.ready_runes.blood;
  if (blood_deficit > 0) {
    death_available -= blood_deficit;
    if (death_available < 0) {
      return false;
    }
  }

  const int unholy_deficit = static_cast<int>(effective_cost.unholy) - player.ready_runes.unholy;
  if (unholy_deficit > 0) {
    death_available -= unholy_deficit;
    if (death_available < 0) {
      return false;
    }
  }

  const int frost_deficit = static_cast<int>(effective_cost.frost) - player.ready_runes.frost;
  if (frost_deficit > 0) {
    death_available -= frost_deficit;
    if (death_available < 0) {
      return false;
    }
  }

  return true;
}

bool MatchesEquippedItemRequirement(const SpellUsabilityInfo &spell,
                                    const PlayerStateSnapshot::EquippedItemMetadata &item) {
  if (!item.present) {
    return false;
  }

  const auto required_class = static_cast<std::uint32_t>(spell.equipped_item_class);
  if (item.item_class != required_class) {
    return false;
  }

  if (required_class == kWeaponItemClass && !item.passes_weapon_state_check) {
    return false;
  }

  if (spell.equipped_item_subclass_mask != 0) {
    if (item.subclass >= 32) {
      return false;
    }
    const auto subclass_bit = 1u << item.subclass;
    if ((static_cast<std::uint32_t>(spell.equipped_item_subclass_mask) & subclass_bit) == 0) {
      return false;
    }
  }

  if (spell.equipped_item_inv_type_mask != 0) {
    if (item.inventory_type >= 32) {
      return false;
    }
    const auto inventory_type_bit = 1u << item.inventory_type;
    if ((static_cast<std::uint32_t>(spell.equipped_item_inv_type_mask) & inventory_type_bit) == 0) {
      return false;
    }
  }

  return true;
}

}

bool SpellUsabilityChecker::CheckKnown(const SpellUsabilityInfo &spell) {
  return spell.is_known;
}

bool SpellUsabilityChecker::CheckCooldown(const SpellUsabilityInfo &spell) {
  return !spell.on_cooldown && !spell.on_gcd;
}

std::uint32_t SpellUsabilityChecker::ComputePowerCost(const SpellUsabilityInfo &spell,
                                                      const PlayerStateSnapshot &player) {
  std::uint32_t cost = spell.mana_cost;

  if (spell.mana_cost_pct > 0 && spell.power_type == PowerType::kMana) {
    cost = (player.max_mana * spell.mana_cost_pct) / 100;
  }
  return cost;
}

std::uint32_t SpellUsabilityChecker::GetCurrentPower(PowerType type,
                                                     const PlayerStateSnapshot &player) {
  switch (type) {
  case PowerType::kMana:
    return player.mana;
  case PowerType::kRage:
    return player.rage;
  case PowerType::kFocus:
    return player.focus;
  case PowerType::kEnergy:
    return player.energy;
  case PowerType::kRunes:
    return 0;
  case PowerType::kRunicPower:
    return player.runic_power;
  case PowerType::kHappiness:
    return 100;
  case PowerType::kHealth:
    return player.health;
  case PowerType::kMax:
    break;
  }
  return 0;
}

bool SpellUsabilityChecker::CheckPower(const SpellUsabilityInfo &spell,
                                       const PlayerStateSnapshot &player) {
  if (spell.power_type == PowerType::kRunes) {
    return HasRequiredReadyRunes(spell, player);
  }

  const std::uint32_t cost = ComputePowerCost(spell, player);
  if (cost == 0)
    return true;

  const std::uint32_t current = GetCurrentPower(spell.power_type, player);
  if (spell.power_type == PowerType::kHealth) {
    return cost < current;
  }
  return current >= cost;
}

bool SpellUsabilityChecker::CheckLevel(const SpellUsabilityInfo &spell,
                                       const PlayerStateSnapshot &player) {
  if (spell.spell_level == 0 && spell.base_level == 0)
    return true;
  return player.level >= spell.spell_level;
}

bool SpellUsabilityChecker::CheckShapeshift(const SpellUsabilityInfo &spell,
                                            const PlayerStateSnapshot &player) {
  return CheckStanceRequirement(spell, player, nullptr);
}

bool SpellUsabilityChecker::CheckEquippedItem(const SpellUsabilityInfo &spell,
                                              const PlayerStateSnapshot &player) {

  if (spell.equipped_item_class < 0)
    return true;

  for (const auto &item : player.equipped_item_slots) {
    if (MatchesEquippedItemRequirement(spell, item)) {
      return true;
    }
  }

  return false;
}

bool SpellUsabilityChecker::CheckRange(const SpellUsabilityInfo &spell) {

  if (spell.target_distance < 0.0f)
    return true;

  if (spell.max_range <= 0.0f)
    return true;

  return spell.target_distance <= spell.max_range;
}

bool SpellUsabilityChecker::CheckCasterState(const SpellUsabilityInfo &spell,
                                             const PlayerStateSnapshot &player) {
  if (player.is_dead)
    return false;

  if (player.is_stunned) {

    if ((spell.attributes_ex5 & usability_attr::kAttrEx5UsableWhileStunned) == 0) {
      return false;
    }
  }

  if (player.is_silenced) {

    return false;
  }

  if (player.is_pacified)
    return false;

  return true;
}

UsabilityResult SpellUsabilityChecker::ComputeUsability(const SpellUsabilityInfo &spell,
                                                        const PlayerStateSnapshot &player) {
  UsabilityResult result;
  result.power_type = spell.power_type;
  result.power_cost = ComputePowerCost(spell, player);

  if (spell.is_passive) {
    result.is_usable = false;
    result.reason = UsabilityReason::kPassiveSpell;
    return result;
  }

  if (!CheckKnown(spell)) {
    result.is_usable = false;
    result.reason = UsabilityReason::kNotKnown;
    return result;
  }

  if (!CheckCasterState(spell, player)) {
    result.is_usable = false;
    if (player.is_dead)
      result.reason = UsabilityReason::kCasterDead;
    else if (player.is_stunned)
      result.reason = UsabilityReason::kCasterStunned;
    else if (player.is_silenced)
      result.reason = UsabilityReason::kCasterSilenced;
    else
      result.reason = UsabilityReason::kCasterPacified;
    return result;
  }

  if (!CheckCooldown(spell)) {
    result.is_usable = false;
    result.reason = UsabilityReason::kOnCooldown;
    return result;
  }

  if (!CheckPower(spell, player)) {
    result.is_usable = false;
    result.not_enough_power = true;
    result.reason = UsabilityReason::kNotEnoughPower;
    return result;
  }

  if (!CheckLevel(spell, player)) {
    result.is_usable = false;
    result.reason = UsabilityReason::kLevelTooLow;
    return result;
  }

  if (!CheckShapeshift(spell, player)) {
    result.is_usable = false;
    result.reason = UsabilityReason::kWrongShapeshift;
    return result;
  }

  if (!CheckEquippedItem(spell, player)) {
    result.is_usable = false;
    result.reason = UsabilityReason::kMissingEquippedItem;
    return result;
  }

  if (!CheckRange(spell)) {
    result.is_usable = false;
    result.out_of_range = true;
    result.reason = UsabilityReason::kOutOfRange;
    return result;
  }

  result.is_usable = true;
  result.reason = UsabilityReason::kUsable;
  return result;
}

namespace stance_attr {
constexpr std::uint32_t kSpellAttr0NotShapeshift = 0x00010000u;
constexpr std::uint32_t kSpellAttr0OnlyStealthed = 0x00020000u;
constexpr std::uint32_t kSpellAttr2NotNeedShapeshift = 0x00080000u;
constexpr std::uint32_t kShapeshiftFormFlag0x400 = 0x00000400u;
}

bool SpellUsabilityChecker::CheckStanceRequirement(
    const SpellUsabilityInfo &spell,
    const PlayerStateSnapshot &player,
    SpellCastError *out_error) {
  const auto allowed = MakeSpellShapeshiftMask(spell.stances, spell.stances_high);
  const auto disallowed = MakeSpellShapeshiftMask(spell.stances_not, spell.stances_not_high);

  const auto form_id = player.shapeshift_form;
  const int zero_based = static_cast<int>(form_id) - 1;

  if (form_id == 0) {
    if ((spell.attributes & stance_attr::kSpellAttr0NotShapeshift) != 0 ||
        (spell.attributes_ex2 &
         stance_attr::kSpellAttr2NotNeedShapeshift) != 0 ||
        SpellShapeshiftMaskEmpty(allowed)) {
      return true;
    }
    if (out_error != nullptr) {
      *out_error = SpellCastError::kOnlyShapeshift;
    }
    return false;
  }

  if (SpellShapeshiftMaskHasZeroBasedFormIndex(disallowed, zero_based)) {

    if (!player.has_aura_ignore_shapeshift) {
      if (out_error)
        *out_error = SpellCastError::kNotShapeshift;
      return false;
    }
    return true;
  }

  if (SpellShapeshiftMaskHasZeroBasedFormIndex(allowed, zero_based)) {
    return true;
  }

  if (player.shapeshift_form_is_turn_sensitive) {

    if ((spell.attributes & stance_attr::kSpellAttr0NotShapeshift) != 0) {
      if (!player.has_aura_ignore_shapeshift) {
        if (out_error)
          *out_error = SpellCastError::kNotShapeshift;
        return false;
      }
      return true;
    }

    if ((player.shapeshift_form_flags & stance_attr::kShapeshiftFormFlag0x400) != 0) {
      if (!player.has_aura_ignore_shapeshift) {
        if (out_error)
          *out_error = SpellCastError::kNotShapeshift;
        return false;
      }
      return true;
    }
  } else {

    if ((spell.attributes_ex2 & stance_attr::kSpellAttr2NotNeedShapeshift) != 0) {
      return true;
    }
  }

  if (!SpellShapeshiftMaskEmpty(allowed)) {
    if (!player.has_aura_ignore_shapeshift) {
      if (out_error)
        *out_error = SpellCastError::kOnlyShapeshift;
      return false;
    }
  }

  return true;
}

bool SpellUsabilityChecker::CheckStealthedRequirement(
    const SpellUsabilityInfo &spell,
    const PlayerStateSnapshot &player,
    SpellCastError *out_error) {

  if ((spell.attributes & stance_attr::kSpellAttr0OnlyStealthed) == 0) {
    return true;
  }

  if (player.is_stealthed) {
    return true;
  }

  if (player.has_aura_ignore_shapeshift) {
    return true;
  }

  if (out_error)
    *out_error = SpellCastError::kOnlyStealthed;
  return false;
}

bool SpellUsabilityChecker::CheckCasterRequirements(const SpellUsabilityInfo &spell,
                                                    const PlayerStateSnapshot &player,
                                                    SpellCastError *out_error) {

  auto fail = [&](SpellCastError err) -> bool {
    if (out_error)
      *out_error = err;
    return false;
  };

  if (spell.spell_id == 0) {
    return fail(SpellCastError::kAffectingCombat);
  }

  if (player.shapeshift_form != 0) {

    if (!spell.usable_in_shapeshift) {
      return fail(SpellCastError::kNotShapeshift);
    }
    if ((spell.attributes_ex6 & 0x02) != 0) {
      return fail(SpellCastError::kOnlyInArena);
    }
  }

  for (std::uint32_t i = 0; i < 3; ++i) {
    if (i >= spell.effect_count)
      break;

    const auto effect_id = spell.effect_ids[i];
    const auto aura_id = spell.effect_aura_ids[i];
    const auto effect_mechanic = spell.effect_mechanic_ids[i];
    const auto aura_mechanic = spell.aura_mechanic_ids[i];

    if (effect_id == 56 || effect_id == 55 || aura_id == 6 || aura_id == 2) {
      if (!spell.has_target) {
        if (spell.has_caster_target) {
          return fail(SpellCastError::kOutOfRange);
        }
        return fail(SpellCastError::kLineOfSight);
      }
    }

    if (effect_mechanic == 5 || aura_mechanic == 5) {
      if (!spell.has_target) {
        return fail(SpellCastError::kNoPet);
      }
    }
  }

  constexpr std::uint32_t kAuraInterruptFlagNotSheathed = 0x200u;
  constexpr std::uint32_t kChannelInterruptFlagNotSheathed = 0x200u;

  constexpr std::uint32_t kAttributesExChannelArmMask = 0x44u;
  constexpr std::int32_t kSheatheStateSheathed = 0;

  if ((spell.aura_interrupt_flags & kAuraInterruptFlagNotSheathed) != 0 &&
      player.sheathe_state == kSheatheStateSheathed) {
    return fail(SpellCastError::kNotUnsheathed);
  }
  if ((spell.attributes_ex & kAttributesExChannelArmMask) != 0 &&
      (spell.channel_interrupt_flags & kChannelInterruptFlagNotSheathed) != 0 &&
      player.sheathe_state == kSheatheStateSheathed) {
    return fail(SpellCastError::kNotUnsheathed);
  }

  if (((spell.aura_interrupt_flags & 0x40) != 0 && !player.is_mounted) ||
      ((spell.attributes_ex & 0x44) != 0 &&
       (spell.channel_interrupt_flags & 0x40) != 0 && !player.is_mounted)) {
    return fail(SpellCastError::kOnlyMounted);
  }

  if ((spell.attributes & 0x1000000) == 0 && player.is_mounted &&
      !player.can_act_while_mounted &&
      !player.can_change_movement_direction) {
    return fail(SpellCastError::kNotMounted);
  }

  if ((spell.attributes & 0x8000000) == 0 && player.is_channeling &&
      !player.is_auto_attacking) {
    return fail(SpellCastError::kNotStanding);
  }

  if ((spell.attributes & 0x1000) != 0 && !player.is_daytime) {
    return fail(SpellCastError::kOnlyDaytime);
  }
  if ((spell.attributes & 0x2000) != 0 && player.is_daytime) {
    return fail(SpellCastError::kOnlyNighttime);
  }

  if (((spell.aura_interrupt_flags & 0x100) != 0 ||
       ((spell.attributes_ex & 0x44) != 0 &&
        (spell.channel_interrupt_flags & 0x100) != 0)) &&
      !player.is_stealthed) {
    return fail(SpellCastError::kOnlyUnderwater);
  }
  if (((spell.aura_interrupt_flags & 0x80) != 0 ||
       ((spell.attributes_ex & 0x44) != 0 &&
        (spell.channel_interrupt_flags & 0x80) != 0)) &&
      player.is_stealthed) {
    return fail(SpellCastError::kOnlyAbovewater);
  }

  {
    SpellCastError stance_err{};
    if (!CheckStanceRequirement(spell, player, &stance_err)) {
      if (stance_err == SpellCastError::kOnlyShapeshift ||
          !player.shapeshift_form_is_turn_sensitive) {
        return fail(stance_err);
      }
    }
  }

  {
    SpellCastError stealth_err{};
    if (!CheckStealthedRequirement(spell, player, &stealth_err)) {
      return fail(stealth_err);
    }
  }

  if (spell.required_faction_id != 0 &&
      player.reputation_standing < spell.required_reputation_level) {
    return fail(SpellCastError::kReputation);
  }

  if (spell.area_group_id > 0 && !player.is_in_area_group) {
    return fail(SpellCastError::kIncorrectArea);
  }

  if ((spell.attributes & 0xC000) != 0) {
    if (!player.is_indoors) {

      if ((spell.attributes & 0x4000) != 0) {
        return fail(SpellCastError::kOnlyIndoors);
      }
    } else {

      if ((spell.attributes & 0x8000) != 0) {
        if (player.has_aura_type_78) {
          return fail(SpellCastError::kNoMountsAllowed);
        }
        return fail(SpellCastError::kOnlyOutdoors);
      }
    }
  }

  if ((spell.attributes_ex3 & 0x800) != 0 && player.map_type != 3) {
    return fail(SpellCastError::kOnlyBattlegrounds);
  }
  if ((spell.attributes_ex6 & 0x800) != 0 && player.map_type == 2) {
    return fail(SpellCastError::kNotInRaidInstance);
  }

  if ((spell.attributes_ex2 & usability_attr::kAttrEx2PetTameSpell) != 0 &&
      (player.pet_tame_status_flags & kPetTameInvalidCreatureBit) != 0) {
    return fail(SpellCastError::kDontReport);
  }

  if (out_error)
    *out_error = SpellCastError::kNone;
  return true;
}

}
