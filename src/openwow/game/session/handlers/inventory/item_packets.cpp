#include "openwow/game/session/handlers/inventory/item_packets.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/console.h"
#include "openwow/game/commerce/auctions/auction_state.h"
#include "openwow/game/commerce/auctions/adapters/protocol/auction_packet_codec.h"
#include "openwow/game/commerce/trade/adapters/protocol/trade_packet_codec.h"
#include "openwow/game/commerce/mail/adapters/protocol/mail_packet_codec.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/replica_sync.h"
#include "openwow/game/inventory/adapters/protocol/inventory_messages.h"
#include "openwow/game/inventory/equipment/adapters/protocol/equipment_set_packet_codec.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_protocol.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"
#include "openwow/game/lcd_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/money_display.h"
#include "openwow/game/object_types.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/player_bag_family.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/tabard_frame.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/trainer_frame.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_gossip.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace openwow::game {

namespace {

void DisplaySystemChatText(const ObjectManager& objects, const std::string& message) {
  if (message.empty()) {
    return;
  }

  ChatFrame_DisplayMessage(objects, message.c_str(), ChatDisplayType::kSystem, nullptr, 0,
                           nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

std::uint32_t ResolveRefundedCurrencyAmount(const std::uint32_t requested,
                                            const std::uint32_t current,
                                            const std::uint32_t retail_cap) {

  return std::min(requested, retail_cap - current);
}

void DisplayCurrencyRefund(const ObjectManager& objects, Localization& localization,
                           const std::uint32_t amount,
                           const std::string& currency_name) {
  if (amount == 0 || currency_name.empty()) {
    return;
  }

  const std::string format =
      localization.GetString("CURRENCY_AMOUNT_REFUND_FORMAT", "");
  if (format.empty()) {
    return;
  }

  DisplaySystemChatText(objects, localization.FormatString(
      format, {std::to_string(amount), currency_name}));
}

void DisplaySuccessfulItemRefund(
    ObjectManager& objects, QueryCache& queries,
    Localization& localization,
    const RefundResult& result) {
  DisplaySystemChatText(objects, localization.GetString("ITEM_REFUND_MSG", ""));

  if (result.money != 0) {
    DisplaySystemChatText(objects,
        MoneyDisplay::FormatLocalizedCoinText(result.money, ", "));
  }

  const auto* player = objects.GetActivePlayer();
  const std::uint32_t current_honor =
      player != nullptr ? player->GetHonorPoints() : 0u;
  const std::uint32_t current_arena =
      player != nullptr ? player->GetArenaPoints() : 0u;
  DisplayCurrencyRefund(
      objects, localization,
      ResolveRefundedCurrencyAmount(result.honor, current_honor, 75000u),
      localization.GetString("HONOR_POINTS", ""));
  DisplayCurrencyRefund(
      objects, localization,
      ResolveRefundedCurrencyAmount(result.arena, current_arena, 10000u),
      localization.GetString("ARENA_POINTS", ""));

  for (const auto& returned_item : result.returned_items) {
    if (returned_item.count == 0) {
      continue;
    }

    const auto* item_template =
        queries.GetItemTemplate(returned_item.item_id);
    if (item_template != nullptr) {
      DisplayCurrencyRefund(
          objects, localization, returned_item.count, item_template->name);
    }
  }
}

void DisplayFailedItemRefund(
    const ObjectManager& objects, Localization& localization,
    const std::uint32_t error) {
  switch (error) {
    case 10:
      ui::game::DisplaySystemMessage(0);
      break;
    case 11:
      ui::game::DisplaySystemMessage(712);
      break;
    default:
      DisplaySystemChatText(objects,
          localization.GetString("UNABLE_TO_REFUND_ITEM", ""));
      break;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                     "Item purchase refund error code " +
                         std::to_string(error));
}

std::uint32_t ResolveItemEntryByGuid(
                                     const PlayerInventoryReplica& inventory,
                                     const ObjectManager& objects,
                                     const std::uint64_t item_guid) {
  if (const auto* item = inventory.FindItemByGuid(item_guid);
      item != nullptr && !item->IsEmpty()) {
    return item->entry;
  }

  const auto* object = objects.GetItem(ObjectGuid(item_guid));
  return object != nullptr ? object->GetEntry() : 0u;
}

std::uint32_t ResolveItemCooldownDurationFromTemplate(
    const QueryCache& queries,
    const std::uint32_t item_entry,
    const std::uint32_t spell_id) {
  const auto* item_template = queries.GetItemTemplate(item_entry);
  if (item_template == nullptr) {
    return 0;
  }

  const auto find_matching_spell = [&]() -> const ItemSpellData* {
    if (spell_id != 0) {
      for (const auto& item_spell : item_template->spells) {
        if (item_spell.trigger == 0 && item_spell.spell_id == spell_id) {
          return &item_spell;
        }
      }
      return nullptr;
    }

    return FindFirstOnUseSpell(*item_template);
  };

  const auto* item_spell = find_matching_spell();
  if (item_spell == nullptr) {
    return 0;
  }

  return std::max(item_spell->cooldown, item_spell->category_cooldown);
}

bool ResolveItemCooldownTiming(const SpellBook& spell_book,
                               const QueryCache& queries,
                               const std::uint64_t elapsed_milliseconds,
                               const ItemCooldownObservation& cooldown,
                               const std::uint32_t item_entry,
                               std::uint32_t& start_time_ms,
                               std::uint32_t& duration_ms) {
  if (cooldown.spell_id != 0) {
    const auto cooldown_it = spell_book.cooldowns().find(cooldown.spell_id);
    if (cooldown_it != spell_book.cooldowns().end()) {
      duration_ms = std::max(cooldown_it->second.cooldown_ms,
                             cooldown_it->second.category_cooldown_ms);
      if (duration_ms != 0) {
        start_time_ms =
            static_cast<std::uint32_t>(cooldown_it->second.start_time_s * 1000.0);
        return true;
      }
    }
  }

  duration_ms =
      ResolveItemCooldownDurationFromTemplate(queries, item_entry, cooldown.spell_id);
  if (duration_ms == 0) {
    return false;
  }

  start_time_ms = static_cast<std::uint32_t>(elapsed_milliseconds);
  return true;
}

}

std::optional<ItemPushResult> HandleItemPushResultPacket(
    InventoryMessageState& state,
    const net::wotlk::WorldPacket& pkt) {
  ItemPushResult result;
  if (!InventoryMessageState::ParseItemPushResult(
          pkt.payload.data(), pkt.payload.size(), result)) {
    return std::nullopt;
  }
  state.OnItemPushResult(result);
  return result;
}

std::optional<InventoryChangeFailure> HandleInventoryChangeFailurePacket(
    InventoryMessageState& state,
    const net::wotlk::WorldPacket& pkt) {
  InventoryChangeFailure failure;
  if (!InventoryMessageState::ParseInventoryChangeFailure(pkt.payload.data(), pkt.payload.size(),
                                                    failure)) {
    return std::nullopt;
  }
  state.OnInventoryChangeFailure(failure);
  return failure;
}

void HandleEnchantmentLogPacket(
    CombatLog& combat_log, ItemDefinitions& items, QueryCache& queries,
    const net::wotlk::WorldPacket& pkt) {
  const auto enchantment = decode_enchantment(pkt.payload);
  if (!enchantment.has_value()) {
    return;
  }

  const double timestamp = combat_log.TimestampWithOffsetMs(0);
  const auto finalize = [&combat_log, enchantment = *enchantment,
                         timestamp](std::string item_name) {
    (void)CombatLog_HandleEnchantLog(
        combat_log, enchantment.target.GetRawValue(),
        enchantment.caster.GetRawValue(), enchantment.item_id,
        enchantment.enchantment_id, std::move(item_name), timestamp);
  };

  if (auto item_name = items.GetItemNameSnapshot(enchantment->item_id)) {
    finalize(std::move(*item_name));
    return;
  }

  const auto *const item = queries.GetOrRequestItemTemplate(
      enchantment->item_id,
      QueryCache::QueryRequestOptions{
          .dedupe_callbacks = false,
          .callback = [&combat_log, &queries, enchantment = *enchantment,
                       timestamp](const bool success) {
            const auto *const resolved =
                success ? queries.GetItemTemplate(enchantment.item_id) : nullptr;
            (void)CombatLog_HandleEnchantLog(
                combat_log, enchantment.target.GetRawValue(),
                enchantment.caster.GetRawValue(), enchantment.item_id,
                enchantment.enchantment_id,
                resolved != nullptr ? resolved->name : std::string{}, timestamp);
          },
      });
  if (item != nullptr) {
    finalize(item->name);
  }
}

void HandleItemEnchantTimePacket(ObjectManager& objects,
                                 const net::wotlk::WorldPacket& pkt) {
  const auto update = decode_enchantment_time(pkt.payload);
  if (!update.has_value()) {
    return;
  }

  const auto duration_seconds =
      static_cast<std::int32_t>(update->seconds);
  if (auto* object = objects.GetMutable(update->item);
      object != nullptr && object->IsItem()) {
    static_cast<CGItem_C*>(object)->SetEnchantTimeRemainingSeconds(
        update->slot, duration_seconds);
    return;
  }

  CGPlayer_C* owner = objects.GetMutablePlayer(update->owner);
  if (owner == nullptr) {
    owner = objects.GetActivePlayer();
  }
  if (owner == nullptr) {
    return;
  }

  owner->QueuePendingItemEnchantTimeUpdate(update->item, update->slot,
                                           duration_seconds);
}

bool HandleItemChargesPacket(
    ObjectManager& objects, PlayerInventoryReplicaSync& inventory_sync,
    const net::wotlk::WorldPacket& pkt) {
  const auto update = decode_item_charges(pkt.payload);
  if (!update.has_value()) {
    return false;
  }

  auto* object = objects.GetMutable(update->item);
  if (object == nullptr || !object->IsItem()) {
    return false;
  }

  auto* item = static_cast<CGItem_C*>(object);
  item->ApplyServerSpellChargeUpdate(update->charges);
  inventory_sync.OnItemUpdated(*item);
  return true;
}

void HandleItemRefundInfoPacket(ItemInteractionSession& interactions,
                                const net::wotlk::WorldPacket& pkt) {
  if (auto quote = decode_refund_quote(pkt.payload); quote.has_value()) {
    (void)interactions.apply_refund_quote(
        interactions.request_generation(), std::move(*quote));
  }
}

void HandleItemRefundResultPacket(
    ItemInteractionSession& interactions, ObjectManager& objects,
    QueryCache& queries, Localization& localization,
    const net::wotlk::WorldPacket& pkt) {
  auto result = decode_refund_result(pkt.payload);
  if (!result.has_value()) {
    return;
  }

  if (!interactions.apply_refund_result(
          interactions.request_generation(), *result)) {
    return;
  }
  if (result->error == 0) {
    DisplaySuccessfulItemRefund(objects, queries, localization, *result);
    return;
  }

  DisplayFailedItemRefund(objects, localization, result->error);
}

void HandleSocketGemsResultPacket(ItemInteractionSession& interactions,
                                  const net::wotlk::WorldPacket& pkt) {
  if (const auto result = decode_socket_result(pkt.payload);
      result.has_value()) {
    (void)interactions.apply_socket_result(result->item);
  }
}

void HandleEquipmentSetUseResultPacket(EquipmentSets& equipment,
                                       Localization& localization,
                                       const net::wotlk::WorldPacket& pkt) {
  const auto result = equipment_protocol::decode_use_result(pkt.payload);
  if (!result.has_value()) {
    return;
  }

  const auto outcome = equipment.apply_use_result(*result);

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::EQUIPMENT_SWAP_FINISHED,
      {outcome.success, outcome.set_name});

  if (outcome.bags_full) {
    const std::string message =
        localization.GetString("EQUIPMENT_MANAGER_BAGS_FULL", "");
    if (!message.empty()) {
      ui::UIErrorManager::Get().AddErrorMessage(message);
      ui::game::ScriptEventDispatch::Get().FireUiErrorMessage(message);
    }
  }
}

void HandleItemCooldownPacket(
    PlayerInventoryReplica& inventory, ObjectManager& objects,
    QueryCache& queries, const SpellBook& spell_book,
    CooldownTracker& cooldowns, const std::uint64_t elapsed_milliseconds,
    const bool has_shapeshift_forms,
    const net::wotlk::WorldPacket& pkt) {
  const auto cooldown = decode_item_cooldown(pkt.payload);
  if (!cooldown.has_value()) {
    return;
  }

  const auto item_entry =
      ResolveItemEntryByGuid(inventory, objects, cooldown->item.GetRawValue());
  std::uint32_t start_time_ms = 0;
  std::uint32_t duration_ms = 0;
  if (item_entry != 0 &&
      ResolveItemCooldownTiming(spell_book, queries, elapsed_milliseconds, *cooldown,
                                item_entry, start_time_ms,
                                duration_ms)) {
    cooldowns.SetItemCooldown(item_entry, duration_ms, start_time_ms);
  }

  auto& events = ui::game::ScriptEventDispatch::Get();
  events.FireActionbarSpellAndShapeshiftCooldownUpdates(has_shapeshift_forms);
  events.FireBagUpdateCooldown();
}

void HandleItemTimeUpdatePacket(ObjectManager& objects,
                                const net::wotlk::WorldPacket& pkt) {
  const auto update = decode_item_duration(pkt.payload);
  if (!update.has_value()) {
    return;
  }

  if (auto* object = objects.GetMutable(update->item);
      object != nullptr && object->IsItem()) {
    static_cast<CGItem_C*>(object)->SetExpiryDurationSeconds(
        static_cast<std::int32_t>(update->seconds));
  }
}

void HandleBuyBankSlotResultPacket(const net::wotlk::WorldPacket& pkt) {
  (void)decode_bank_slot_result(pkt.payload);
}

void HandleEquipmentSetSavedPacket(EquipmentSets& equipment,
                                   const net::wotlk::WorldPacket& pkt) {
  if (const auto saved = equipment_protocol::decode_saved(pkt.payload);
      saved.has_value()) {
    equipment.apply_saved(saved->first, saved->second);
  }
}

std::optional<std::uint64_t> HandleItemTextPacket(
    ItemInteractionSession& interactions,
    data::WDBPersistence& persistence,
    const net::wotlk::WorldPacket& pkt) {
  auto response = decode_item_text(pkt.payload);
  if (!response.has_value() || response->item.IsEmpty()) {
    return std::nullopt;
  }

  if (!interactions.cache_text(response->item, response->text)) {
    return std::nullopt;
  }
  persistence.StoreItemTextEntry(response->item.GetRawValue(),
                                 response->text);
  return response->item.GetRawValue();
}

}
