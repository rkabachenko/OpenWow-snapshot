
#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/aura_manager.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_use_requirements.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/rune_handler.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_usability.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace openwow::game {

inline std::uint32_t CountCarriedItemsOfEntry(
    const PlayerInventoryReplica& inventory, std::uint32_t item_id);

namespace detail {

inline constexpr std::array<std::uint8_t, 3> kEquippedItemRequirementSlots = {
    15u,
    16u,
    17u,
};

inline constexpr std::size_t kActivePlayerPetTameStatusByteOffset = 4196u;

struct EquippedItemMasks {
  std::uint32_t class_mask = 0;
  std::uint32_t subclass_mask = 0;
  std::uint32_t inv_type_mask = 0;
};

inline EquippedItemMasks BuildEquippedItemMasks(
    const PlayerInventoryReplica& inventory, const ItemDefinitions& item_definitions) {
  EquippedItemMasks masks;

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kMaxEquipSlots; ++slot) {
    const auto *item = inventory.GetEquipSlot(slot);
    if (item == nullptr || item->IsEmpty()) {
      continue;
    }

    const auto *tmpl = item_definitions.GetItem(item->entry);
    if (tmpl == nullptr) {
      continue;
    }

    const auto item_class = static_cast<std::uint32_t>(tmpl->item_class);
    if (item_class < 32) {
      masks.class_mask |= 1u << item_class;
    }
    if (tmpl->subclass < 32) {
      masks.subclass_mask |= 1u << tmpl->subclass;
    }
    const auto inventory_type = static_cast<std::uint32_t>(tmpl->inventory_type);
    if (inventory_type < 32) {
      masks.inv_type_mask |= 1u << inventory_type;
    }
  }

  return masks;
}

inline std::uint8_t ReadDescriptorByte(const CGPlayer_C &player, const std::size_t byte_offset) {
  constexpr auto kBitsPerByte = 8u;
  constexpr auto kBytesPerDword = sizeof(std::uint32_t);

  const auto field_index = static_cast<std::uint16_t>(byte_offset / kBytesPerDword);
  const auto shift = static_cast<std::uint32_t>((byte_offset % kBytesPerDword) * kBitsPerByte);
  return static_cast<std::uint8_t>((player.GetUInt32(field_index) >> shift) & 0xFFu);
}

inline void PopulateEquippedItemRequirementSlots(
                                                 const PlayerInventoryReplica& inventory,
                                                 const CGPlayer_C &player,
                                                 PlayerStateSnapshot *snapshot) {
  if (snapshot == nullptr) {
    return;
  }

  for (std::size_t index = 0; index < kEquippedItemRequirementSlots.size(); ++index) {
    const auto slot = kEquippedItemRequirementSlots[index];
    if (const auto metadata = player.GetVisibleItemTemplateMetadata(slot);
        metadata.has_value()) {
      auto slot_metadata = PlayerStateSnapshot::EquippedItemMetadata{
          true,
          metadata->item_class,
          metadata->subclass,
          metadata->inventory_type,
      };
      if (const auto *item = inventory.GetEquipSlot(slot);
          item != nullptr && !item->IsEmpty() &&
          metadata->item_class == static_cast<std::uint32_t>(ItemClass::Weapon)) {
        slot_metadata.passes_weapon_state_check =
            ItemPassesEquippedWeaponSpellCheck(*item, *metadata);
      }
      snapshot->equipped_item_slots[index] = slot_metadata;
    }
  }
}

inline std::uint32_t ResolveSpellModifierFamily(const CGPlayer_C &player,
                                                const openwow::data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr) {
    return 0;
  }

  const auto *chr_class = dbc->chr_classes().LookupEntry(player.State().GetClass());
  return chr_class != nullptr ? chr_class->spell_family : 0;
}

inline openwow::data::dbc::SpellEntry
BuildSpellModifierBridgeEntry(const SpellUsabilityInfo &info) {
  openwow::data::dbc::SpellEntry spell{};
  spell.spell_family_name = info.spell_family_name;
  spell.spell_family_flags = info.spell_family_flags;
  spell.attributes_ex3 = info.attributes_ex3;
  return spell;
}

inline std::int32_t ComputeEffectiveRuneCostPct(const SpellUsabilityInfo &info,
                                                const CGPlayer_C &player,
                                                const AuraManager *aura_manager,
                                                const openwow::data::dbc::DbcLoader *dbc) {
  std::int32_t cost_pct = 100;
  cost_pct += GetMinimumPowerCostModifierForSchoolMask(player, info.school_mask);
  cost_pct = static_cast<std::int32_t>(
      std::lrintf(static_cast<float>(cost_pct) *
                  GetMinimumPowerCostMultiplierForSchoolMask(player, info.school_mask)));

  if (aura_manager == nullptr) {
    return cost_pct;
  }

  const auto active_spell_family = ResolveSpellModifierFamily(player, dbc);
  if (active_spell_family == 0) {
    return cost_pct;
  }

  auto spell = BuildSpellModifierBridgeEntry(info);
  (void)aura_manager->ApplySpellModifierDeltas(
      active_spell_family, spell, SpellModOp::kCost, &cost_pct);
  return cost_pct;
}

}

inline PlayerStateSnapshot BuildUnitUsabilitySnapshot(
    const CGUnit_C &unit,
    const openwow::data::dbc::DbcLoader *dbc = nullptr) {
  PlayerStateSnapshot snapshot;
  snapshot.level = unit.State().GetLevel();
  snapshot.is_dead = unit.State().IsDead();
  snapshot.is_moving = unit.Movement().IsMoving();
  snapshot.is_stunned = unit.State().IsStunned();
  snapshot.is_silenced = unit.State().IsSilenced();
  snapshot.is_pacified = unit.State().IsPacified();
  snapshot.health = unit.State().GetHealth();
  snapshot.max_mana = unit.State().GetMaxPower(static_cast<std::uint8_t>(PowerType::kMana));
  snapshot.mana = unit.State().GetPower(static_cast<std::uint8_t>(PowerType::kMana));
  snapshot.rage = unit.State().GetPower(static_cast<std::uint8_t>(PowerType::kRage));
  snapshot.focus = unit.State().GetPower(static_cast<std::uint8_t>(PowerType::kFocus));
  snapshot.energy = unit.State().GetPower(static_cast<std::uint8_t>(PowerType::kEnergy));
  snapshot.runic_power = unit.State().GetPower(static_cast<std::uint8_t>(PowerType::kRunicPower));

  snapshot.shapeshift_form = unit.State().SuppressesCurrentFormSpellQueries()
                                 ? 0u
                                 : unit.Animation().GetShapeshiftForm();
  if (snapshot.shapeshift_form != 0 && dbc != nullptr) {
    if (const auto *form = dbc->spell_shapeshift_form().LookupEntry(
            snapshot.shapeshift_form);
        form != nullptr) {
      snapshot.shapeshift_form_flags = form->flags;
      snapshot.shapeshift_form_is_turn_sensitive =
          (form->flags & 0x1u) == 0u;
    }
  }
  snapshot.is_mounted = unit.Mount().IsMountedStateActive(unit);
  snapshot.can_act_while_mounted = unit.State().CanActWhileMounted();
  snapshot.sheathe_state = unit.Animation().GetCachedSheatheState();
  const auto position = unit.GetPosition();
  if (const auto* world_environment = unit.world_environment();
      world_environment != nullptr) {
    if (const auto outdoors =
            world_environment->QueryOutdoorStateAtWorldPosition(
                position.x, position.y, position.z);
        outdoors.has_value()) {
      snapshot.is_indoors = !*outdoors;
    } else {
      snapshot.is_indoors = world_environment->IsIndoors();
    }
  }
  return snapshot;
}

inline PlayerStateSnapshot BuildPlayerUsabilitySnapshot(
    const CGPlayer_C &player, const PlayerInventoryReplica& inventory,
    const ItemDefinitions& item_definitions,
    const RuneHandler *rune_handler = nullptr,
    const openwow::data::dbc::DbcLoader *dbc = nullptr) {

  auto snapshot = BuildUnitUsabilitySnapshot(player, dbc);
  const auto masks = detail::BuildEquippedItemMasks(inventory, item_definitions);
  snapshot.equipped_item_class_mask = masks.class_mask;
  snapshot.equipped_item_subclass_mask = masks.subclass_mask;
  snapshot.equipped_item_inv_type_mask = masks.inv_type_mask;
  snapshot.pet_tame_status_flags =
      detail::ReadDescriptorByte(player, detail::kActivePlayerPetTameStatusByteOffset);
  detail::PopulateEquippedItemRequirementSlots(inventory, player, &snapshot);
  if (rune_handler != nullptr) {
    snapshot.ready_runes.blood =
        static_cast<std::uint8_t>(rune_handler->CountReadyRunes(RuneType::kBlood));
    snapshot.ready_runes.unholy =
        static_cast<std::uint8_t>(rune_handler->CountReadyRunes(RuneType::kUnholy));
    snapshot.ready_runes.frost =
        static_cast<std::uint8_t>(rune_handler->CountReadyRunes(RuneType::kFrost));
    snapshot.ready_runes.death =
        static_cast<std::uint8_t>(rune_handler->CountReadyRunes(RuneType::kDeath));
    snapshot.has_rune_data = true;
  }
  return snapshot;
}

inline SpellUsabilityInfo BuildActionSpellUsabilityInfo(
    const SpellQueryResult &query, const CGPlayer_C *active_player = nullptr,
    const AuraManager *aura_manager = nullptr, const openwow::data::dbc::DbcLoader *dbc = nullptr) {
  SpellUsabilityInfo info;
  info.spell_id = query.spellId;
  info.base_level = query.requiredLevel;
  info.spell_level = query.requiredLevel;
  info.mana_cost = query.manaCost;
  info.power_type = query.powerType;
  info.rune_cost = query.runeCost;
  info.has_rune_cost = query.hasRuneCost;
  info.is_passive = query.isPassive;
  info.is_known = query.isKnown;
  info.stances = query.stances;
  info.stances_high = query.stancesHigh;
  info.stances_not = query.stancesNot;
  info.stances_not_high = query.stancesNotHigh;
  info.equipped_item_class = query.equippedItemClass;
  info.equipped_item_subclass_mask = query.equippedItemSubclassMask;
  info.equipped_item_inv_type_mask = query.equippedItemInvTypeMask;
  info.attributes = query.attributes;
  info.attributes_ex = query.attributesEx;
  info.attributes_ex2 = query.attributesEx2;
  info.attributes_ex3 = query.attributesEx3;
  info.attributes_ex5 = query.attributesEx5;
  info.attributes_ex6 = query.attributesEx6;
  info.aura_interrupt_flags = query.attributes2;
  info.channel_interrupt_flags = query.attributes3;
  info.spell_family_name = query.spellFamilyName;
  info.spell_family_flags = query.spellFamilyFlags;
  info.school_mask = query.schoolMask;
  info.max_range = query.range;
  info.target_distance = -1.0f;
  info.usable_in_shapeshift = query.usableInShapeshift;
  info.castable_while_mounted = query.castableWhileMounted;
  info.castable_while_moving = query.castableWhileMoving;

  if (dbc != nullptr) {
    if (const auto *spell = dbc->spell().LookupEntry(query.spellId); spell != nullptr) {
      if (info.power_type == PowerType::kMana &&
          spell->power_type == static_cast<std::uint32_t>(PowerType::kRunes)) {
        info.power_type = PowerType::kRunes;
      }
      if (!info.has_rune_cost && spell->rune_cost_id != 0) {
        if (const auto *rune_cost = dbc->spell_rune_cost().LookupEntry(spell->rune_cost_id);
            rune_cost != nullptr) {
          info.rune_cost = {
              static_cast<std::uint8_t>(rune_cost->blood),
              static_cast<std::uint8_t>(rune_cost->unholy),
              static_cast<std::uint8_t>(rune_cost->frost),
          };
          info.has_rune_cost = true;
        }
      }
      info.spell_family_name = spell->spell_family_name;
      info.spell_family_flags = spell->spell_family_flags;
      info.attributes_ex3 = spell->attributes_ex3;
      info.school_mask = spell->school_mask;
    }
  }

  if (active_player != nullptr && info.power_type == PowerType::kRunes && info.has_rune_cost) {
    info.rune_cost_pct =
        detail::ComputeEffectiveRuneCostPct(info, *active_player, aura_manager, dbc);
  }

  return info;
}

struct PredictedCooldownWindow {
  double start_time_s = 0.0;
  double duration_s = 0.0;

  [[nodiscard]] double ExpiresAt() const {
    return start_time_s + duration_s;
  }
};

inline std::optional<PredictedCooldownWindow> PredictRuneDrivenSpellCooldown(
    const std::uint32_t spell_id, const CGPlayer_C *active_player,
    const AuraManager *aura_manager, const openwow::data::dbc::DbcLoader *dbc,
    const RuneHandler &rune_handler, const std::uint32_t current_tick_ms,
    const double current_time_s) {
  if (spell_id == 0 || dbc == nullptr) {
    return std::nullopt;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr ||
      spell->power_type != static_cast<std::uint32_t>(PowerType::kRunes) ||
      spell->rune_cost_id == 0) {
    return std::nullopt;
  }

  const auto *rune_cost = dbc->spell_rune_cost().LookupEntry(spell->rune_cost_id);
  if (rune_cost == nullptr) {
    return std::nullopt;
  }

  SpellUsabilityInfo info;
  info.spell_id = spell_id;
  info.power_type = PowerType::kRunes;
  info.rune_cost = {
      static_cast<std::uint8_t>(rune_cost->blood),
      static_cast<std::uint8_t>(rune_cost->unholy),
      static_cast<std::uint8_t>(rune_cost->frost),
  };
  info.has_rune_cost = true;
  info.spell_family_name = spell->spell_family_name;
  info.spell_family_flags = spell->spell_family_flags;
  info.attributes_ex3 = spell->attributes_ex3;
  info.school_mask = spell->school_mask;

  if (active_player != nullptr) {
    info.rune_cost_pct =
        detail::ComputeEffectiveRuneCostPct(info, *active_player, aura_manager, dbc);
  }

  const auto prediction =
      rune_handler.PredictSpellCooldown(info.rune_cost, info.rune_cost_pct);
  if (!prediction.has_value()) {
    return std::nullopt;
  }

  const auto remaining_ms =
      static_cast<std::int32_t>(prediction->ready_tick_ms - current_tick_ms);
  if (remaining_ms < 0) {
    return std::nullopt;
  }

  return PredictedCooldownWindow{
      .start_time_s = current_time_s +
                      static_cast<double>(
                          remaining_ms -
                          static_cast<std::int32_t>(prediction->duration_ms)) /
                          1000.0,
      .duration_s = static_cast<double>(prediction->duration_ms) / 1000.0,
  };
}

inline const ItemInstance *FindInventoryItemByEntry(
    const PlayerInventoryReplica& inventory, std::uint32_t item_id) {

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kMaxEquipSlots; ++slot) {
    const auto *item = inventory.GetEquipSlot(slot);
    if (item != nullptr && item->entry == item_id) {
      return item;
    }
  }

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    const auto *item = inventory.GetBackpackSlot(slot);
    if (item != nullptr && item->entry == item_id) {
      return item;
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto *bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }
    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      const auto *item = inventory.GetBagSlot(bag, slot);
      if (item != nullptr && item->entry == item_id) {
        return item;
      }
    }
  }

  return nullptr;
}

inline std::uint32_t CountCarriedItemsOfEntry(
    const PlayerInventoryReplica& inventory, std::uint32_t item_id) {
  return inventory.GetItemCount(item_id);
}

inline std::uint32_t ResolveItemUseSpell(
    const ItemDefinitions& item_definitions, std::uint32_t item_id) {
  const auto *tmpl = item_definitions.GetItem(item_id);
  if (tmpl == nullptr) {
    return 0;
  }

  const auto *spell = FindFirstOnUseSpell(*tmpl);
  return spell != nullptr ? spell->spell_id : 0;
}

inline std::uint32_t ResolveItemUseSpellWithEquippedFallback(
    const PlayerInventoryReplica& inventory,
    const ItemDefinitions& item_definitions,
    const std::uint32_t item_id,
    const data::dbc::DbcLoader *dbc = SpellbookSystem::Get().GetDbcLoader()) {
  const auto *item_template = item_definitions.GetItem(item_id);
  if (item_template == nullptr) {
    return 0;
  }

  return ResolveItemUseSpellIdWithEquippedFallback(
      inventory, item_id, item_template, dbc);
}

inline bool ItemPassesPlayerRequirements(const WorldSession *session,
                                          const ItemDefinitions& item_definitions,
                                          const CGPlayer_C &player,
                                          std::uint32_t item_id) {
  const auto *tmpl = item_definitions.GetItem(item_id);
  if (tmpl == nullptr) {
    return false;
  }

  const auto requirements = BuildItemUseRequirementView(*tmpl);
  const auto proficiency =
      session != nullptr
          ? session->session().GetProficiencyMask(
                static_cast<std::uint8_t>(requirements.item_class))
          : 0u;
  if (!PlayerMeetsItemUseRequirements(
          player, requirements,
          session != nullptr ? session->item_use_requirement_sources()
                             : ItemUseRequirementSources{},
          proficiency)) {
    return false;
  }

  if (tmpl->item_class == ItemClass::Consumable) {
    return true;
  }
  return std::any_of(
      tmpl->spells.begin(), tmpl->spells.end(),
      [](const ItemSpellData& spell) {
        return spell.spell_id != 0 && spell.trigger == 0;
      });
}

}
