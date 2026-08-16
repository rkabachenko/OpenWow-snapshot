#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/dressup_model.h"

#include "openwow/game/inventory/items/item_link_parser.h"

#include <algorithm>
#include <cstdlib>

namespace openwow::game {

namespace {

int g_lastWeaponSlot = kDressUpSlotMainhand;

constexpr std::uint8_t kInventoryTypeWeapon = 13;
constexpr std::uint8_t kInventoryTypeShield = 14;
constexpr std::uint8_t kInventoryTypeRanged = 15;
constexpr std::uint8_t kInventoryTypeTwoHand = 17;
constexpr std::uint8_t kInventoryTypeMainHand = 21;
constexpr std::uint8_t kInventoryTypeOffHand = 22;
constexpr std::uint8_t kInventoryTypeHoldable = 23;

bool CanEquipInOffHand(const DressUpCanEquipItemInSlotFn& can_equip_item_in_slot,
                       std::uint32_t item_id) {
  return can_equip_item_in_slot &&
         can_equip_item_in_slot(item_id,
                                static_cast<std::uint32_t>(kDressUpSlotOffhand));
}

}

int DressUpModel_GetSlotForInventoryType(uint32_t inventoryType) {
    switch (inventoryType) {
        case 1:  return 0;
        case 3:  return 1;
        case 4:  return 2;
        case 5:
        case 20: return 3;
        case 6:  return 4;
        case 7:  return 5;
        case 8:  return 6;
        case 9:  return 7;
        case 10: return 8;
        case 19: return 9;
        case 16: return 10;

        case 13:
        case 14:
        case 15:
        case 17:
        case 21:
        case 22:
        case 23:
        case 25:
        case 26:
            return -2;

        case 2:
        case 11:
        case 12:
        case 28:
        default:
            return -1;
    }
}

int DressUpModelSlotToVisibleItemSlot(int modelSlot) {
    switch (modelSlot) {
        case 0:  return 0;
        case 1:  return 2;
        case 2:  return 3;
        case 3:  return 4;
        case 4:  return 5;
        case 5:  return 6;
        case 6:  return 7;
        case 7:  return 8;
        case 8:  return 9;
        case 9:  return 18;
        case 10: return 14;
        default: return -1;
    }
}

int DressUpModel_GetNextWeaponSlot() {
    return g_lastWeaponSlot;
}

void DressUpModel_ResetWeaponSlotCycle() {
  g_lastWeaponSlot = kDressUpSlotMainhand;
}

void DressUpModel_SetNextWeaponSlot(const int slot) {
  g_lastWeaponSlot = slot == kDressUpSlotOffhand ? kDressUpSlotOffhand
                                                 : kDressUpSlotMainhand;
}

bool DressUpModel_AreWeaponOverridesCompatible(
    const DressUpWeaponCompatibilityInputs& inputs,
    const DressUpCanEquipItemInSlotFn& can_equip_item_in_slot) {
  const auto mainhand_inventory_type = inputs.mainhand_inventory_type;
  const auto offhand_inventory_type = inputs.offhand_inventory_type;

  if (mainhand_inventory_type != kInventoryTypeWeapon &&
      mainhand_inventory_type != kInventoryTypeMainHand) {
    return mainhand_inventory_type == kInventoryTypeTwoHand &&
           offhand_inventory_type != kInventoryTypeRanged &&
           CanEquipInOffHand(can_equip_item_in_slot,
                             inputs.pending_mainhand_item_id);
  }

  if (offhand_inventory_type == kInventoryTypeShield ||
      offhand_inventory_type == kInventoryTypeHoldable) {
    return true;
  }

  if (offhand_inventory_type == kInventoryTypeWeapon ||
      offhand_inventory_type == kInventoryTypeOffHand) {
    return inputs.uncaught_exception_active;
  }

  return (offhand_inventory_type == kInventoryTypeMainHand ||
          offhand_inventory_type == kInventoryTypeTwoHand) &&
         CanEquipInOffHand(can_equip_item_in_slot,
                           inputs.pending_offhand_item_id);
}

bool DressUpModel_ParseItemLink(std::string_view link,
                                DressUpParsedItemLink& out) {
  const auto parsed = ItemLinkParser::Parse(std::string(link));
  if (!parsed.has_value()) {
    return false;
  }

  out = DressUpParsedItemLink{};
  out.item_id = parsed->itemId;
  out.enchant_id = parsed->enchantId;
  out.gem_enchant_ids = parsed->gemIds;
  out.extra_enchant_id = parsed->extraEnchantId;
  return out.item_id != 0;
}

std::uint32_t DressUpModel_ResolveItemLinkAuraId(
    const ItemTemplate& item,
    const DressUpParsedItemLink& link_fields,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry>*
        item_display_info,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemVisualsEntry>*
        item_visuals,
    const openwow::data::dbc::DbcStore<
        openwow::data::dbc::SpellItemEnchantmentEntry>*
        spell_item_enchantments) {
  if (item.display_id != 0 && item_display_info != nullptr &&
      item_visuals != nullptr) {
    if (const auto* display = item_display_info->LookupEntry(item.display_id);
        display != nullptr && display->item_visuals_id != 0) {
      if (const auto* visuals =
              item_visuals->LookupEntry(display->item_visuals_id);
          visuals != nullptr &&
          std::any_of(visuals->slot.begin(), visuals->slot.end(),
                      [](const std::uint32_t slot) { return slot != 0; })) {
        return 0;
      }
    }
  }

  if (spell_item_enchantments == nullptr) {
    return 0;
  }

  const std::array<std::uint32_t, 12> scanned_values = {
      link_fields.enchant_id,
      0,
      link_fields.gem_enchant_ids[0],
      link_fields.gem_enchant_ids[1],
      link_fields.gem_enchant_ids[2],
      static_cast<std::uint32_t>(link_fields.extra_enchant_id),
      0,
      0,
      0,
      0,
      0,
      0,
  };

  for (const std::uint32_t value : scanned_values) {
    if (value == 0) {
      continue;
    }

    if (const auto* enchant = spell_item_enchantments->LookupEntry(value);
        enchant != nullptr && enchant->aura_id != 0) {
      return enchant->aura_id;
    }
  }

  return 0;
}

std::optional<std::uint32_t> DressUpModel_ResolveEnchantLinkItemId(
    const std::uint32_t spell_id,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellEntry>* spells) {
  if (spell_id == 0 || spells == nullptr) {
    return std::nullopt;
  }

  const auto* spell = spells->LookupEntry(spell_id);
  if (spell == nullptr || (spell->attributes & 0x20u) == 0 ||
      spell->effect_item_type[0] == 0) {
    return std::nullopt;
  }

  return spell->effect_item_type[0];
}

bool DressUpModel_IsWeaponInventoryType(const std::uint32_t inventory_type) {
  switch (inventory_type) {
    case 13:
    case 14:
    case 15:
    case 17:
    case 21:
    case 22:
    case 23:
    case 25:
    case 26:
      return true;
    default:
      return false;
  }
}

int DressUpModel_GetPreviewSlotForInventoryType(
    const std::uint32_t inventory_type) {
  switch (inventory_type) {
    case 1:
      return 1;
    case 3:
      return 2;
    case 4:
      return 3;
    case 5:
    case 20:
      return 4;
    case 6:
      return 5;
    case 7:
      return 6;
    case 8:
      return 7;
    case 9:
      return 8;
    case 10:
      return 9;
    case 19:
      return 10;
    case 16:
      return 11;
    default:
      return -1;
  }
}

namespace {

int ConsumeWeaponPreviewSlot() {
  const int slot = g_lastWeaponSlot;
  g_lastWeaponSlot =
      (g_lastWeaponSlot == kDressUpSlotMainhand) ? kDressUpSlotOffhand
                                                 : kDressUpSlotMainhand;
  return slot;
}

}

int DressUpModel_SelectWeaponPreviewSlot(
    const std::uint32_t inventory_type,
    const bool has_mainhand_override,
    const std::uint8_t mainhand_inventory_type,
    const bool can_equip_in_offhand) {
  switch (inventory_type) {
    case 14:
    case 15:
    case 22:
    case 23:
      g_lastWeaponSlot = kDressUpSlotMainhand;
      return kDressUpSlotOffhand;
    case 25:
    case 26:
      g_lastWeaponSlot = kDressUpSlotMainhand;
      return kDressUpSlotMainhand;
    case 17:
    case 21:
      if (can_equip_in_offhand) {
        return ConsumeWeaponPreviewSlot();
      }
      g_lastWeaponSlot = kDressUpSlotMainhand;
      return kDressUpSlotMainhand;
    case 13:
      if (has_mainhand_override &&
          (mainhand_inventory_type == 13 || mainhand_inventory_type == 17 ||
           mainhand_inventory_type == 21) &&
          can_equip_in_offhand) {
        return ConsumeWeaponPreviewSlot();
      }
      g_lastWeaponSlot = kDressUpSlotMainhand;
      return kDressUpSlotMainhand;
    default:
      g_lastWeaponSlot = kDressUpSlotMainhand;
      return kDressUpSlotMainhand;
  }
}

std::optional<std::uint32_t> DressUpModel_DetectEnchantScroll(
    const ItemTemplate& item,
    const DressUpSpellLookupFn& lookup_spell) {
  if (static_cast<std::uint32_t>(item.inventory_type) != 0) {
    return std::nullopt;
  }
  if (!lookup_spell) {
    return std::nullopt;
  }

  for (std::uint32_t i = 0; i < 5; ++i) {
    const auto spell_id = item.spells[i].spell_id;
    if (spell_id == 0) {
      continue;
    }

    const auto outer = lookup_spell(spell_id);
    if (!outer.has_value()) {
      continue;
    }

    if (outer->effect_0 != kSpellEffectLearnSpell) {
      continue;
    }

    auto sub_spell_id = outer->effect_trigger_spell_0;
    if (sub_spell_id == 0) {
      for (std::uint32_t j = 0; j < 5; ++j) {
        if (item.spells[j].trigger == kSpellTriggerLearnSpellId) {
          sub_spell_id = item.spells[j].spell_id;
          break;
        }
      }
    }

    if (sub_spell_id == 0) {
      continue;
    }

    const auto sub = lookup_spell(sub_spell_id);
    if (!sub.has_value()) {
      continue;
    }

    if (sub->effect_0 == kSpellEffectEnchantItem) {
      return sub->effect_item_type_0;
    }
  }

  return std::nullopt;
}

namespace {

void EquipArmorSlot(
    int slot, std::uint32_t display_id, std::int32_t enchant_id,
    bool has_character_model,
    const std::function<void(int, std::uint32_t, std::int32_t)>& equip_fn) {
  if (has_character_model && equip_fn) {
    equip_fn(slot, display_id, enchant_id);
  }
}

}

std::optional<DressUpTryOnItemOutput> DressUpModel_TryOnItemTyped(
    const DressUpTryOnItemInput& input,
    const DressUpTryOnCallbacks& callbacks) {

  if (!callbacks.lookup_item) {
    return std::nullopt;
  }

  const auto* item = callbacks.lookup_item(input.item_id);
  if (item == nullptr) {
    return std::nullopt;
  }

  if (item->display_id == 0 || !input.has_backing_model) {
    return std::nullopt;
  }

  const std::int32_t enchant_id =
      (input.enchant_id < 0) ? 0 : input.enchant_id;

  const auto inv_type = static_cast<std::uint32_t>(item->inventory_type);
  if (inv_type == 0) {
    const auto target = DressUpModel_DetectEnchantScroll(*item,
                                                         callbacks.lookup_spell);
    if (target.has_value() && callbacks.resolve_and_dispatch) {
      callbacks.resolve_and_dispatch(*target, 0, -1);
    }

    if (target.has_value()) {
      return std::nullopt;
    }

  }

  DressUpTryOnItemOutput output{};
  output.mainhand_override = input.mainhand_override;
  output.offhand_override = input.offhand_override;
  output.next_weapon_slot = g_lastWeaponSlot;
  output.pending_mainhand_item_id = 0;
  output.pending_offhand_item_id = 0;

  switch (inv_type) {
    case 1:
      EquipArmorSlot(0, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 2:
    case 11:
    case 12:
    case 28:
      return output;

    case 3:
      EquipArmorSlot(1, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 4:
      EquipArmorSlot(2, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 5:
    case 20:
      EquipArmorSlot(3, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 6:
      EquipArmorSlot(4, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 7:
      EquipArmorSlot(5, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 8:
      EquipArmorSlot(6, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 9:
      EquipArmorSlot(7, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 10:
      EquipArmorSlot(8, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 13:
    case 14:
    case 15:
    case 17:
    case 21:
    case 22:
    case 23:
    case 25:
    case 26: {

      const bool is_active_player =
          callbacks.is_active_player
              ? callbacks.is_active_player(input.bound_guid)
              : false;
      const bool can_dual_wield =
          is_active_player && callbacks.can_dual_wield
              ? callbacks.can_dual_wield()
              : false;

      int target_slot = input.force_slot;
      if (target_slot == -1) {

        switch (inv_type) {
          case 13:

            if (input.mainhand_override.IsOccupied() &&
                (input.mainhand_override.inventory_type == 13 ||
                 input.mainhand_override.inventory_type == 21 ||
                 input.mainhand_override.inventory_type == 17) &&
                is_active_player && can_dual_wield) {

              target_slot = g_lastWeaponSlot;
              output.next_weapon_slot =
                  (g_lastWeaponSlot == kDressUpSlotMainhand)
                      ? kDressUpSlotOffhand
                      : kDressUpSlotMainhand;
            } else {
              target_slot = kDressUpSlotMainhand;

            }
            break;

          case 14:
          case 15:
          case 22:
          case 23:
            target_slot = kDressUpSlotOffhand;
            output.next_weapon_slot = kDressUpSlotMainhand;
            break;

          case 17:
          case 21:
            if (callbacks.can_equip_in_slot &&
                callbacks.can_equip_in_slot(input.item_id, kDressUpSlotOffhand)) {

              target_slot = g_lastWeaponSlot;
              output.next_weapon_slot =
                  (g_lastWeaponSlot == kDressUpSlotMainhand)
                      ? kDressUpSlotOffhand
                      : kDressUpSlotMainhand;
            } else {
              target_slot = kDressUpSlotMainhand;
              output.next_weapon_slot = kDressUpSlotMainhand;
            }
            break;

          case 25:
          case 26:
            target_slot = kDressUpSlotMainhand;
            output.next_weapon_slot = kDressUpSlotMainhand;
            break;

          default:
            return output;
        }
      }

      if (target_slot == kDressUpSlotMainhand) {
        output.pending_mainhand_item_id = input.item_id;

        if (input.offhand_override.IsOccupied()) {

          DressUpWeaponCompatibilityInputs compat{};
          compat.mainhand_inventory_type =
              static_cast<std::uint8_t>(inv_type);
          compat.offhand_inventory_type =
              input.offhand_override.inventory_type;
          compat.pending_mainhand_item_id = input.item_id;
          compat.pending_offhand_item_id = 0;
          compat.uncaught_exception_active = can_dual_wield;

          const auto can_equip_fn =
              callbacks.can_equip_in_slot
                  ? DressUpCanEquipItemInSlotFn(
                        [&](std::uint32_t item_id_arg,
                            std::uint32_t slot_id_arg) -> bool {
                          return callbacks.can_equip_in_slot(item_id_arg,
                                                            slot_id_arg);
                        })
                  : DressUpCanEquipItemInSlotFn{};

          if (!DressUpModel_AreWeaponOverridesCompatible(compat,
                                                          can_equip_fn)) {

            if (callbacks.clear_weapon_slot) {
              callbacks.clear_weapon_slot(
                  kDressUpSlotOffhand,
                  input.offhand_override.sheath,
                  input.offhand_override.inventory_type == 14);
            }
            output.offhand_override.Clear();
            output.pending_offhand_item_id = 0;
          }
        }

        auto& target_override = output.mainhand_override;

        if (target_override.IsOccupied() && callbacks.clear_weapon_slot) {
          callbacks.clear_weapon_slot(
              target_slot, target_override.sheath,
              target_override.inventory_type == 14);
        }

        target_override.SetFromItem(*item);

        if (callbacks.dispatch_weapon) {
          DressUpTryOnCallbacks::WeaponDispatchParams params{};
          params.slot = target_slot;
          params.display_id = item->display_id;
          params.sheath = item->sheath;
          params.enchant_id = enchant_id;
          params.is_shield = (inv_type == 14);
          params.is_ranged_right = (inv_type == 26);
          callbacks.dispatch_weapon(params);
        }
        output.did_equip = true;
      }

      else {
        output.pending_offhand_item_id = input.item_id;

        if (input.mainhand_override.IsOccupied()) {
          DressUpWeaponCompatibilityInputs compat{};
          compat.mainhand_inventory_type =
              input.mainhand_override.inventory_type;
          compat.offhand_inventory_type =
              static_cast<std::uint8_t>(inv_type);
          compat.pending_mainhand_item_id = 0;
          compat.pending_offhand_item_id = input.item_id;
          compat.uncaught_exception_active = can_dual_wield;

          const auto can_equip_fn =
              callbacks.can_equip_in_slot
                  ? DressUpCanEquipItemInSlotFn(
                        [&](std::uint32_t item_id_arg,
                            std::uint32_t slot_id_arg) -> bool {
                          return callbacks.can_equip_in_slot(item_id_arg,
                                                            slot_id_arg);
                        })
                  : DressUpCanEquipItemInSlotFn{};

          if (!DressUpModel_AreWeaponOverridesCompatible(compat,
                                                          can_equip_fn)) {
            if (callbacks.clear_weapon_slot) {
              callbacks.clear_weapon_slot(
                  kDressUpSlotMainhand,
                  input.mainhand_override.sheath,
                  input.mainhand_override.inventory_type == 14);
            }
            output.mainhand_override.Clear();
            output.pending_mainhand_item_id = 0;
          }
        }

        auto& target_override = output.offhand_override;

        if (target_override.IsOccupied() && callbacks.clear_weapon_slot) {
          callbacks.clear_weapon_slot(
              target_slot, target_override.sheath,
              target_override.inventory_type == 14);
        }

        target_override.SetFromItem(*item);

        if (callbacks.dispatch_weapon) {
          DressUpTryOnCallbacks::WeaponDispatchParams params{};
          params.slot = target_slot;
          params.display_id = item->display_id;
          params.sheath = item->sheath;
          params.enchant_id = enchant_id;
          params.is_shield = (inv_type == 14);
          params.is_ranged_right = (inv_type == 26);
          callbacks.dispatch_weapon(params);
        }
        output.did_equip = true;
      }
      return output;
    }

    case 16:
      EquipArmorSlot(10, item->display_id, enchant_id,
                     input.has_character_model, callbacks.equip_armor_slot);
      output.did_equip = true;
      return output;

    case 19:
      if (input.has_character_model) {

        if (callbacks.equip_armor_slot) {
          callbacks.equip_armor_slot(9, item->display_id, enchant_id);
        }
        output.did_equip = true;

        if (callbacks.get_display_info_flags) {
          const auto flags = callbacks.get_display_info_flags(item->display_id);
          if ((flags & kItemDisplayInfoFlagGuildTabard) != 0) {
            if (callbacks.apply_guild_tabard) {
              callbacks.apply_guild_tabard(input.bound_guid);
            }
          }
        }
      }
      return output;

    default:
      if (callbacks.display_system_message) {
        callbacks.display_system_message(kSystemMsgNotEquippable);
      }
      return output;
  }
}

bool DressUpModel_OnItemTemplateCacheLoaded(
    const std::uint32_t itemEntryId,
    const std::uint32_t modelThis,
    const bool success,
    const DressUpResolveTryOnFn& resolve_and_try_on) {
  if (!success) {
    return false;
  }

  if (resolve_and_try_on) {

    resolve_and_try_on(modelThis, itemEntryId, 0,
                       -1);
  }
  return true;
}

namespace {

constexpr std::uint32_t kEquipSlotCount = 19;

constexpr std::uint32_t kUnitFlagSheath  = 0x400;
constexpr std::uint32_t kUnitFlagSheath2 = 0x800;

constexpr std::uint32_t kNpcItemDisplaySlotCount = 11;

}

DressUpRebuildPreviewOutput DressUpModelFrame_RebuildPreviewFromUnit(
    const DressUpRebuildPreviewInput& input,
    const DressUpUnitAppearance& appearance,
    const DressUpRebuildCallbacks& callbacks) {

  DressUpRebuildPreviewOutput output{};

  if (callbacks.bind_unit_model) {
    callbacks.bind_unit_model(input.frame_this, input.unit_ptr);
  }

  if (input.existing_preview_model && callbacks.release_preview_model) {
    callbacks.release_preview_model(input.existing_preview_model);
  }

  if (input.unit_ptr == 0) {
    return output;
  }

  if (input.resolved_unit != 0) {

    if (callbacks.allocate_preview_model) {
      output.preview_model = callbacks.allocate_preview_model();
    }

    if (callbacks.init_preview_model && output.preview_model) {
      callbacks.init_preview_model(
          output.preview_model, appearance, input.is_active_player);
    }

    DressUpModel_ResetWeaponSlotCycle();

    const std::uint32_t unit_flags =
        callbacks.get_unit_flags
            ? callbacks.get_unit_flags(input.resolved_unit)
            : 0;

    for (std::uint32_t slot = 0; slot < kEquipSlotCount; ++slot) {

      if (slot == 0 && (unit_flags & kUnitFlagSheath) != 0) {
        continue;
      }

      if (slot == 14 && (unit_flags & kUnitFlagSheath2) != 0) {
        continue;
      }

      if (slot == 17) {
        continue;
      }

      if (!callbacks.get_visible_item) {
        continue;
      }
      const auto entry = callbacks.get_visible_item(input.resolved_unit, slot);
      if (entry.item_entry_id == 0) {
        continue;
      }

      std::int32_t force_slot = -1;
      if (slot == kDressUpSlotMainhand) {
        force_slot = static_cast<std::int32_t>(kDressUpSlotMainhand);
      } else if (slot == kDressUpSlotOffhand) {
        force_slot = static_cast<std::int32_t>(kDressUpSlotOffhand);
      }

      const std::uint32_t item_id = static_cast<std::uint32_t>(
          std::abs(entry.item_entry_id));
      const std::int32_t aura_id =
          callbacks.resolve_aura_visual
              ? callbacks.resolve_aura_visual(entry)
              : 0;

      if (callbacks.try_on_item) {
        callbacks.try_on_item(item_id, aura_id, force_slot);
      }
    }

    if (callbacks.get_weapon_override) {
      const auto mh =
          callbacks.get_weapon_override(input.resolved_unit, 0);
      output.mainhand_override_lo = mh.first;
      output.mainhand_override_hi = mh.second;

      const auto oh =
          callbacks.get_weapon_override(input.resolved_unit, 1);
      output.offhand_override_lo = oh.first;
      output.offhand_override_hi = oh.second;
    }

    output.sheathed =
        callbacks.is_sheathed
            ? callbacks.is_sheathed(input.resolved_unit)
            : false;

    return output;
  }

  if (input.pet_model_id == 0 || !callbacks.lookup_npc_display) {
    return output;
  }

  const auto* npc_record = callbacks.lookup_npc_display(input.pet_model_id);
  if (npc_record == nullptr) {
    return output;
  }

  if (npc_record->baked_texture_name == nullptr ||
      npc_record->baked_texture_name[0] == '\0') {
    return output;
  }

  if (callbacks.allocate_preview_model) {
    output.preview_model = callbacks.allocate_preview_model();
  }

  DressUpUnitAppearance npc_appearance{};
  npc_appearance.race = npc_record->race;
  npc_appearance.gender = npc_record->gender;
  npc_appearance.skin_color = npc_record->skin_color;
  npc_appearance.face = npc_record->face;
  npc_appearance.hair_style = npc_record->hair_style;
  npc_appearance.hair_color = npc_record->hair_color;
  npc_appearance.facial_hair = npc_record->facial_hair;
  npc_appearance.extra_hair_style = npc_record->extra_hair_style;

  if (callbacks.init_preview_model && output.preview_model) {
    callbacks.init_preview_model(output.preview_model, npc_appearance, false);
  }

  if (callbacks.set_item_display) {
    for (std::uint32_t i = 0; i < kNpcItemDisplaySlotCount; ++i) {
      if (npc_record->item_display_ids[i] != 0) {
        callbacks.set_item_display(i, npc_record->item_display_ids[i], 0);
      }
    }
  }

  return output;
}

void ApplyVisualReadinessToRenderFlags(SceneRenderVisibilityFlags& state,
                                       const bool visuals_ready) noexcept {
  const std::uint32_t ready_bit = visuals_ready ? 1u : 0u;

  if (state.is_attached) {

    state.flags = (state.flags & ~kRenderFlagAttachedPrimary) |
                  (ready_bit << 7u);
    state.flags = (state.flags & ~kRenderFlagAttachedSecondary) |
                  (ready_bit << 17u);
  } else {

    state.flags = (state.flags & ~kRenderFlagStandalonePrimary) |
                  (ready_bit << 3u);
    state.flags = (state.flags & ~kRenderFlagStandaloneSecondary) |
                  (ready_bit << 16u);
  }
}

  std::optional<DressUpUpdateModelMotionOutput>
DressUpModelFrame_UpdateModelMotionRenderFlags(
    const DressUpUpdateModelMotionInput& input) noexcept {
  if (!input.has_preview_model) {
    return std::nullopt;
  }

  SceneRenderVisibilityFlags render_state = input.render_state;
  ApplyVisualReadinessToRenderFlags(render_state, input.visuals_ready);

  return DressUpUpdateModelMotionOutput{render_state.flags};
}

}
