#include "openwow/game/inventory/adapters/ui/item_spell_target_controller.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/adapters/protocol/inventory_messages.h"
#include "openwow/game/inventory/adapters/ui/item_spell_target_rules.h"
#include "openwow/game/inventory/adapters/ui/item_target_cursor_presenter.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"
#include "openwow/game/inventory/items/item_trade_eligibility.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/net/wotlk/spell_packets.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/ui_error_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace openwow::game::inventory::ui {
namespace {

constexpr std::uint32_t kItemTemplateFlagSuppressAuraItemTargeting = 0x00002000u;

using WireTargetFlags = net::wotlk::SpellCastTargetFlags;
constexpr WireTargetFlags kItemWireTargetFlags =
    WireTargetFlags::kItem | WireTargetFlags::kGameObjectItem;
constexpr WireTargetFlags kItemOrGlyphWireTargetFlags =
    kItemWireTargetFlags | WireTargetFlags::kGlyphSlot;

bool HasAnyWireTargetFlag(const std::uint32_t mask,
                          const WireTargetFlags flags) {
  return net::wotlk::HasFlag(static_cast<WireTargetFlags>(mask), flags);
}

const ItemInstance *FindCarriedItemByGuid(
    const PlayerInventoryReplica& inventory,
    const ObjectGuid item_guid) {
  if (item_guid.IsEmpty()) {
    return nullptr;
  }

  for (std::uint16_t slot = 0; slot < InventorySlots::kTotalSlots; ++slot) {
    const auto *item = inventory.GetItemInSlot(static_cast<std::uint8_t>(slot));
    if (item != nullptr && item->guid == item_guid.GetRawValue()) {
      return item;
    }
  }

  return nullptr;
}

std::string ToOwnedString(const std::string_view value) {
  return std::string(value.data(), value.size());
}

std::uint32_t ResolveItemTargetingSpellId(const data::dbc::DbcLoader* dbc,
                                          const ItemInstance &item,
                                          const ItemTemplate &item_template) {
  if (const auto *item_spell = FindFirstOnUseSpell(item_template); item_spell != nullptr) {
    return item_spell->spell_id;
  }

  if (dbc == nullptr) {
    return 0;
  }

  const auto enchant_match =
      FindFirstOnUseSpellEnchantment(item, [dbc](const std::uint32_t enchantment_id) {
        return dbc->spell_item_enchantment().LookupEntry(enchantment_id);
      });
  return enchant_match.has_value() ? enchant_match->spell_id() : 0u;
}

const data::dbc::SpellItemEnchantmentEntry *
LookupEnchantment(const data::dbc::DbcLoader &dbc, const std::uint32_t enchant_id) {
  return enchant_id == 0 ? nullptr : dbc.spell_item_enchantment().LookupEntry(enchant_id);
}

bool HasBindingEnchantSlot(const ItemInstance &item, const data::dbc::DbcLoader &dbc) {
  return ItemHasBindingEnchantSlot(item, [&dbc](const std::uint32_t enchantment_id) {
    return LookupEnchantment(dbc, enchantment_id);
  });
}

bool HasItemUseSpell(const ItemTemplate &item_template) {
  return FindFirstOnUseSpell(item_template) != nullptr;
}

bool HasUseSpellEnchant(const ItemInstance &item, const data::dbc::DbcLoader &dbc) {
  return FindFirstOnUseSpellEnchantment(item, [&](const std::uint32_t enchantment_id) {
           return LookupEnchantment(dbc, enchantment_id);
         }).has_value();
}

bool EnchantHasUseSpellEffect(const data::dbc::SpellItemEnchantmentEntry &entry) {
  for (std::size_t effect_index = 0; effect_index < entry.type.size(); ++effect_index) {
    if (entry.type[effect_index] == kEnchantmentTypeUseSpell &&
        (entry.spell_id[effect_index] != 0 || entry.amount[effect_index] != 0)) {
      return true;
    }
  }

  return false;
}

std::string ResolveSkillName(const data::dbc::DbcLoader &dbc, const std::uint32_t skill_id) {
  if (const auto *entry = dbc.skill_line().LookupEntry(skill_id);
      entry != nullptr && !entry->name.empty()) {
    return ToOwnedString(entry->name);
  }

  return "Skill #" + std::to_string(skill_id);
}

std::string ResolveItemName(const ItemTemplate &item_template) {
  return item_template.name.empty() ? "Item #" + std::to_string(item_template.entry)
                                    : item_template.name;
}

void DisplayTargetFailure(Localization& localization,
                          ::openwow::ui::UIErrorManager& errors,
                          ::openwow::ui::game::ScriptEventDispatch& events,
                          const SpellCastResult reason,
                          const data::dbc::DbcLoader* dbc,
                          const ItemTemplate *item_template = nullptr,
                          const std::uint32_t item_quantity = 0,
                          const std::uint32_t skill_id = 0,
                          const std::uint32_t skill_rank = 0) {
  const auto failure_key = SpellFailedReasonToString(static_cast<std::uint32_t>(reason));
  const auto failure_format = localization.GetString(failure_key, failure_key);
  std::string message = failure_format;

  if (reason == SpellCastResult::kNeedMoreItems && item_template != nullptr) {
    message = localization.FormatString(
        failure_format, {std::to_string(item_quantity), ResolveItemName(*item_template)});
  } else if (reason == SpellCastResult::kMinSkill && skill_id != 0) {
    const auto skill_name =
        dbc != nullptr ? ResolveSkillName(*dbc, skill_id) : "Skill #" + std::to_string(skill_id);
    message =
        localization.FormatString(failure_format, {skill_name, std::to_string(skill_rank)});
  }

  if (message.empty()) {
    return;
  }

  errors.AddErrorMessage(message);
  events.FireUiErrorMessage(message);
}

bool MatchesTargetItemRequirements(const ItemTemplate &item_template,
                                   const data::dbc::SpellEntry &spell) {
  if (spell.equipped_item_class >= 0 &&
      static_cast<std::int32_t>(item_template.item_class) != spell.equipped_item_class) {
    return false;
  }

  if (spell.equipped_item_sub_class_mask > 0 && item_template.subclass < 32 &&
      (static_cast<std::uint32_t>(spell.equipped_item_sub_class_mask) &
       (1u << item_template.subclass)) == 0) {
    return false;
  }

  const auto inventory_type = static_cast<std::uint32_t>(item_template.inventory_type);
  return spell.equipped_item_inv_type_mask <= 0 || inventory_type >= 32 ||
         (static_cast<std::uint32_t>(spell.equipped_item_inv_type_mask) &
          (1u << inventory_type)) != 0;
}

void QueueConfirmation(ItemInteractionSession& interactions,
                       ::openwow::ui::game::ScriptEventDispatch& events,
                       const ObjectGuid item_guid, const char *event_name,
                       std::initializer_list<::openwow::ui::game::EventArg> args = {}) {
  interactions.set_pending_modification(item_guid);
  if (args.size() == 0) {
    events.FireEvent(event_name);
  } else {
    events.FireEventArgs(event_name, args);
  }
}

void SendTargetedItemCast(InteractionSender& interaction,
                          SpellCastRuntime& spell_client,
                          const std::uint32_t spell_id,
                          const ObjectGuid item_guid) {
  if (spell_client.GetSlot(SpellSlotType::kCurrent).spell_id == spell_id) {
    spell_client.SetItemTarget(SpellSlotType::kCurrent, item_guid);
  }

  interaction.SendCastSpellOnItem(spell_id, 0, item_guid.GetRawValue());
  ClearItemTargetCursor(spell_client.GetTargeting());
  spell_client.GetTargeting().CancelTargeting();
}

void ProcessEnchantEffect(ItemInteractionSession& interactions,
                          InteractionSender& interaction,
                          SpellCastRuntime& spells,
                          Localization& localization,
                          ::openwow::ui::UIErrorManager& errors,
                          ::openwow::ui::game::ScriptEventDispatch& events,
                          const data::dbc::DbcLoader &dbc,
                          const data::dbc::SpellEntry &spell, const std::uint32_t spell_id,
                          const ItemInstance &item, const ItemTemplate &item_template,
                          const std::size_t effect_index,
                          const ItemTargetConfirmation confirmation) {
  const auto effect = static_cast<ItemSpellEffect>(spell.effect[effect_index]);
  const auto item_guid = ObjectGuid(item.guid);
  if ((item_template.flags & kItemTemplateFlagSuppressAuraItemTargeting) != 0) {
    return;
  }

  if (!MatchesTargetItemRequirements(item_template, spell)) {
    DisplayTargetFailure(localization, errors, events,
                         SpellCastResult::kBadTargets, &dbc);
    return;
  }

  const auto enchant_id = static_cast<std::uint32_t>(spell.effect_misc_value[effect_index]);
  const auto *new_enchant = LookupEnchantment(dbc, enchant_id);
  const bool new_enchant_binds = new_enchant != nullptr && (new_enchant->slot & 1u) != 0;
  const bool has_binding_enchant = HasBindingEnchantSlot(item, dbc);
  const auto* refund =
      interactions.refund_quote(ObjectGuid(item.guid));
  const bool refund_active = refund != nullptr && refund->time_left != 0;
  const bool should_prompt = confirmation == ItemTargetConfirmation::kPrompt;

  if (should_prompt && new_enchant_binds && !item.IsSoulbound() && !has_binding_enchant &&
      item_template.inventory_type != InventoryType::NonEquip) {
    QueueConfirmation(interactions, events, item_guid,
                      ::openwow::ui::game::events::BIND_ENCHANT);
    return;
  }

  if (should_prompt && item.IsSoulbound() && refund_active && !has_binding_enchant &&
      (effect != ItemSpellEffect::kEnchantTemporary || new_enchant_binds)) {
    QueueConfirmation(interactions, events, item_guid,
                      ::openwow::ui::game::events::END_BOUND_TRADEABLE,
                      {"itemenchant"});
    return;
  }

  if (should_prompt && refund_active && effect != ItemSpellEffect::kEnchantTemporary) {
    QueueConfirmation(interactions, events, item_guid,
                      ::openwow::ui::game::events::END_REFUND, {1});
    return;
  }

  if (new_enchant != nullptr && EnchantHasUseSpellEffect(*new_enchant) &&
      (HasItemUseSpell(item_template) || HasUseSpellEnchant(item, dbc))) {
    DisplayTargetFailure(localization, errors, events,
                         SpellCastResult::kOnUseEnchant, &dbc);
    return;
  }

  const auto enchant_slot = effect == ItemSpellEffect::kEnchantPermanent
                                ? EnchantmentSlot::Permanent
                                : EnchantmentSlot::Temporary;
  const auto current_enchant_id = item.enchantments[static_cast<std::size_t>(enchant_slot)].id;
  if (should_prompt && current_enchant_id != 0 && new_enchant != nullptr) {
    if (const auto *current_enchant = LookupEnchantment(dbc, current_enchant_id);
        current_enchant != nullptr) {
      QueueConfirmation(
          interactions, events, item_guid,
          ::openwow::ui::game::events::REPLACE_ENCHANT,
          {ToOwnedString(current_enchant->description), ToOwnedString(new_enchant->description)});
      return;
    }
  }

  SendTargetedItemCast(interaction, spells, spell_id, item_guid);
}

void ProcessProfessionEffect(ObjectManager& objects,
                             InteractionSender& interaction,
                             SpellCastRuntime& spells,
                             Localization& localization,
                             ::openwow::ui::UIErrorManager& errors,
                             ::openwow::ui::game::ScriptEventDispatch& events,
                             const data::dbc::DbcLoader* dbc,
                             const data::dbc::SpellEntry &spell,
                             const std::uint32_t spell_id, const ItemInstance &item,
                             const ItemTemplate &item_template,
                             const std::size_t effect_index) {
  const auto effect = static_cast<ItemSpellEffect>(spell.effect[effect_index]);
  const auto processing_rule = GetItemProcessingRule(effect);
  if (!processing_rule.has_value()) {
    return;
  }

  if ((item_template.flags & static_cast<std::uint32_t>(processing_rule->required_flag)) == 0) {
    DisplayTargetFailure(localization, errors, events,
                         processing_rule->failure, dbc);
    return;
  }

  if (item_template.required_skill != 0) {
    const auto *player = objects.GetActivePlayer();
    if (player == nullptr) {
      return;
    }

    const auto skill_id = static_cast<std::uint16_t>(item_template.required_skill);
    const auto skill_value = player->GetSkillValueWithStepModifier(skill_id);
    if (skill_value < item_template.required_skill_rank) {
      DisplayTargetFailure(localization, errors, events,
                           SpellCastResult::kMinSkill, dbc, nullptr, 0,
                           item_template.required_skill, item_template.required_skill_rank);
      return;
    }
  }

  const auto required_item_count =
      static_cast<std::uint32_t>(std::max(0, spell.effect_base_points[effect_index] + 1));
  if (required_item_count != 0 && item.count < required_item_count) {
    DisplayTargetFailure(localization, errors, events,
                         SpellCastResult::kNeedMoreItems, dbc, &item_template,
                         required_item_count);
    return;
  }

  SendTargetedItemCast(interaction, spells, spell_id, ObjectGuid(item.guid));
}

}

bool TryStartItemSpellTargeting(const data::dbc::DbcLoader* dbc,
                                SpellCastRuntime& spells,
                                const ItemInstance &item,
                                const ItemTemplate &item_template) {
  const auto spell_id =
      ResolveItemTargetingSpellId(dbc, item, item_template);
  if (spell_id == 0) {
    return false;
  }

  if (dbc == nullptr) {
    return false;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  const auto target_mask = SpellTargetValidator::BuildTargetMask(*spell);
  if (!HasAnyWireTargetFlag(target_mask,
                            kItemOrGlyphWireTargetFlags)) {
    return false;
  }

  auto &targeting = spells.GetTargeting();
  targeting.StartTargeting(spell_id, SpellTargetingMode::Unit, 0.0f, 0.0f, target_mask);
  targeting.SetPendingSourceItem(ObjectGuid(item.guid));
  if (HasAnyWireTargetFlag(target_mask, kItemWireTargetFlags)) {
    BeginItemTargetCursor(targeting, ObjectGuid(item.guid));
  } else {
    ClearItemTargetCursor(targeting);
  }
  return true;
}

void ProcessItemSpellTarget(const data::dbc::DbcLoader* dbc,
                            PlayerInventoryReplica& inventory,
                            QueryCache& item_definitions,
                            ItemInteractionSession& interactions,
                            ObjectManager& objects,
                            InteractionSender& interaction,
                            SpellCastRuntime& spells,
                            Localization& localization,
                            ::openwow::ui::UIErrorManager& errors,
                            ::openwow::ui::game::ScriptEventDispatch& events,
                            const ObjectGuid item_guid,
                            const ItemTargetConfirmation confirmation) {
  if (dbc == nullptr) {
    return;
  }

  const auto *item =
      FindCarriedItemByGuid(inventory, item_guid);
  if (item == nullptr) {
    return;
  }

  const auto *item_template =
      item_definitions.GetOrRequestItemTemplate(item->entry);
  if (item_template == nullptr) {
    return;
  }

  auto &targeting = spells.GetTargeting();
  if (!targeting.IsTargeting() ||
      !HasAnyWireTargetFlag(targeting.GetTargetMask(),
                            kItemWireTargetFlags)) {
    return;
  }

  const auto spell_id = targeting.GetSpellId();
  if (spell_id == 0) {
    return;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return;
  }

  for (std::size_t effect_index = 0; effect_index < spell->effect.size(); ++effect_index) {
    switch (static_cast<ItemSpellEffect>(spell->effect[effect_index])) {
    case ItemSpellEffect::kEnchantPermanent:
    case ItemSpellEffect::kEnchantTemporary:
    case ItemSpellEffect::kEnchantPrismatic:
      ProcessEnchantEffect(interactions, interaction, spells, localization,
                           errors, events, *dbc, *spell, spell_id,
                           *item, *item_template, effect_index,
                           confirmation);
      return;
    case ItemSpellEffect::kMilling:
    case ItemSpellEffect::kProspecting:
      ProcessProfessionEffect(objects, interaction, spells, localization,
                              errors, events, dbc, *spell, spell_id,
                              *item, *item_template, effect_index);
      return;
    default:
      break;
    }
  }
}

}
