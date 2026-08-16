#include "openwow/game/inventory/equipment/item_equip_check.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/reputation_info.h"

namespace openwow::game {
namespace {

bool ItemMatchesTrackedRequirementSpell(
    const data::dbc::SpellEntry& spell,
    const ItemTemplate& item_template) {
  if (spell.equipped_item_class < 0 ||
      static_cast<std::uint32_t>(spell.equipped_item_class) !=
          static_cast<std::uint32_t>(item_template.item_class)) {
    return false;
  }

  if (spell.equipped_item_sub_class_mask != 0) {
    const auto subclass_bit =
        std::uint32_t{1} << (item_template.subclass & 31u);
    if ((static_cast<std::uint32_t>(spell.equipped_item_sub_class_mask) &
         subclass_bit) == 0u) {
      return false;
    }
  }

  return spell.equipped_item_inv_type_mask != 0;
}

bool TrackedRequirementSpellAllowsSlot(
    const data::dbc::SpellEntry& spell,
    const std::int32_t target_slot,
    const bool has_relic_slot) {
  const auto inventory_mask =
      static_cast<std::uint32_t>(spell.equipped_item_inv_type_mask);
  for (std::uint32_t inventory_type = 0;
       inventory_type < kInventoryTypeToSlotMask.size(); ++inventory_type) {
    if ((inventory_mask & (std::uint32_t{1} << inventory_type)) != 0u &&
        CanInventoryTypeGoInSlot(
            inventory_type, static_cast<std::uint32_t>(target_slot),
            has_relic_slot)) {
      return true;
    }
  }

  return false;
}

bool PlayerHasTrackedItemRequirementOverride(
    const CGPlayer_C& player,
    const ItemTemplate& item_template,
    const std::int32_t target_slot,
    const bool has_relic_slot,
    const data::dbc::DbcLoader* dbc) {
  if (dbc == nullptr) {
    return false;
  }
  for (const auto spell_id : player.Casts().GetTrackedItemRequirementSpellIds()) {
    const auto* tracked_spell = dbc->spell().LookupEntry(spell_id);
    if (tracked_spell != nullptr &&
        ItemMatchesTrackedRequirementSpell(*tracked_spell, item_template) &&
        TrackedRequirementSpellAllowsSlot(
            *tracked_spell, target_slot, has_relic_slot)) {
      return true;
    }
  }

  return false;
}

bool IdentityMaskAllows(const std::int32_t mask, const std::uint8_t identity) {
  if (mask == -1) {
    return true;
  }
  if (identity == 0) {
    return false;
  }

  const auto identity_bit =
      std::uint32_t{1} << ((static_cast<std::uint32_t>(identity) - 1u) & 31u);
  return (static_cast<std::uint32_t>(mask) & identity_bit) != 0u;
}

bool IdentityMasksAllow(
    const std::int32_t class_mask,
    const std::int32_t race_mask,
    const std::uint8_t player_class,
    const std::uint8_t player_race,
    const openwow::data::dbc::DbcLoader* dbc) {
  const auto class_bits = static_cast<std::uint32_t>(class_mask);
  const auto race_bits = static_cast<std::uint32_t>(race_mask);
  const bool optimized_single_identity_path =
      (class_bits & (class_bits - 1u)) == 0u &&
      (race_bits & (race_bits - 1u)) == 0u;

  if (dbc != nullptr && !dbc->chr_classes().empty() &&
      !dbc->chr_races().empty()) {
    const auto race_bit_is_set = [race_bits](const std::uint32_t race_id) {
      return (race_bits &
              (std::uint32_t{1} << ((race_id - 1u) & 31u))) != 0u;
    };
    const auto class_bit_is_set = [class_bits](const std::uint32_t class_id) {
      return (class_bits &
              (std::uint32_t{1} << ((class_id - 1u) & 31u))) != 0u;
    };

    if (optimized_single_identity_path) {
      for (const auto& race : dbc->chr_races().entries()) {
        if ((race.flags & 1u) == 0u && race_bit_is_set(race.id) &&
            player_race != race.id) {
          return false;
        }
      }
      for (const auto& player_class_entry : dbc->chr_classes().entries()) {
        if (class_bit_is_set(player_class_entry.id) &&
            player_class != player_class_entry.id) {
          return false;
        }
      }
      return true;
    }

    bool race_mask_covers_all = true;
    bool race_matches_player = false;
    for (const auto& race : dbc->chr_races().entries()) {
      if ((race.flags & 1u) != 0u) {
        continue;
      }
      const bool allowed = race_bit_is_set(race.id);
      race_mask_covers_all = race_mask_covers_all && allowed;
      race_matches_player =
          race_matches_player || (allowed && player_race == race.id);
    }

    bool class_mask_covers_all = true;
    bool class_matches_player = false;
    for (const auto& player_class_entry : dbc->chr_classes().entries()) {
      const bool allowed = class_bit_is_set(player_class_entry.id);
      class_mask_covers_all = class_mask_covers_all && allowed;
      class_matches_player =
          class_matches_player ||
          (allowed && player_class == player_class_entry.id);
    }

    return (race_mask_covers_all || race_matches_player) &&
           (class_mask_covers_all || class_matches_player);
  }

  if (optimized_single_identity_path) {

    return (class_bits == 0u || IdentityMaskAllows(class_mask, player_class)) &&
           (race_bits == 0u || IdentityMaskAllows(race_mask, player_race));
  }

  return IdentityMaskAllows(class_mask, player_class) &&
         IdentityMaskAllows(race_mask, player_race);
}

}

bool PlayerClassHasRelicSlot(const std::uint8_t class_id) {
  return class_id == 2u || class_id == 6u || class_id == 7u ||
         class_id == 11u;
}

bool CanItemOccupyInventorySlot(
    const ItemTemplate& item,
    const std::int32_t target_slot,
    const CGPlayer_C* player,
    const openwow::data::dbc::DbcLoader* dbc) {
  if (target_slot < -1) {
    return false;
  }
  if (target_slot == -1) {
    return item.item_class == ItemClass::Projectile;
  }

  bool has_relic_slot =
      player != nullptr && PlayerClassHasRelicSlot(player->State().GetClass());
  if (player != nullptr && dbc != nullptr && !dbc->chr_classes().empty()) {
    const auto* class_entry = dbc->chr_classes().LookupEntry(player->State().GetClass());
    has_relic_slot =
        class_entry != nullptr && (class_entry->class_flags & 0x8u) != 0u;
  }
  const auto inventory_type = item.inventory_type;
  const bool is_weapon = item.item_class == ItemClass::Weapon;

  const bool needs_weapon_slot_override =
      is_weapon &&
      ((target_slot == InventorySlots::kOffHand &&
        (inventory_type == InventoryType::TwoHand ||
         inventory_type == InventoryType::MainHand)) ||
       (target_slot == InventorySlots::kMainHand &&
        inventory_type == InventoryType::OffHand));
  if (needs_weapon_slot_override) {
    if ((item.flags2 & 0x40000000u) != 0u || item.subclass == 13u ||
        player == nullptr) {
      return false;
    }

    return PlayerHasTrackedItemRequirementOverride(
        *player, item, target_slot, has_relic_slot, dbc);
  }

  return CanInventoryTypeGoInSlot(
      static_cast<std::uint32_t>(item.inventory_type),
      static_cast<std::uint32_t>(target_slot),
      has_relic_slot);
}

bool PlayerMeetsItemEquipRequirements(
    const ItemTemplate& item,
    const std::uint32_t target_slot,
    const ItemEquipRequirementContext& context) {
  if (item.required_level > 1u &&
      item.required_level > context.player_level) {
    return false;
  }

  if (context.proficiency_mask != 0u &&
      (context.proficiency_mask &
       (std::uint32_t{1} << (item.subclass & 31u))) == 0u) {
    return false;
  }

  if (target_slot == InventorySlots::kOffHand &&
      (item.inventory_type == InventoryType::Weapon ||
       item.inventory_type == InventoryType::OffHand) &&
      !context.can_dual_wield) {
    return false;
  }

  if (!IdentityMasksAllow(
          item.allowable_class, item.allowable_race,
          context.player_class, context.player_race, context.dbc)) {
    return false;
  }

  if (item.required_reputation_faction != 0u) {
    if (item.required_reputation_rank >= kStandingMin.size() ||
        context.faction_standing <
            kStandingMin[item.required_reputation_rank]) {
      return false;
    }
  }

  return true;
}

}
