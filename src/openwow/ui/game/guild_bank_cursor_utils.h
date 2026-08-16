#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace openwow::ui::game::detail::guild_bank_cursor {

inline constexpr std::uint32_t kGuildBankDepositTabFlag = 0x2u;

inline constexpr int kGuildPermissionsMessage = 95;
inline constexpr int kGuildBankConjuredItemMessage = 127;
inline constexpr int kGuildBankEquippedItemMessage = 128;
inline constexpr int kGuildBankBoundItemMessage = 129;
inline constexpr int kGuildBankQuestItemMessage = 130;
inline constexpr int kGuildBankWrappedItemMessage = 131;

struct GuildBankCursorInventorySource {
  ::openwow::game::ItemInstance source_item;
  std::uint8_t source_bag = ::openwow::game::InventorySlots::kMainBag;
  std::uint8_t source_slot = 0;
};

struct GuildBankContainerDropTarget {
  std::uint8_t player_bag = ::openwow::game::InventorySlots::kMainBag;
  std::uint8_t player_slot = 0;
  const ::openwow::game::ItemInstance* item = nullptr;
};

struct GuildBankHeldItemView {
  std::uint32_t item_entry{0};
  std::uint32_t linear_slot{0};
  std::uint32_t split_count{0};
};

inline std::optional<GuildBankContainerDropTarget>
ResolveContainerDropTarget(const ::openwow::game::PlayerInventoryReplica& inventory,
                           int bag_id, int slot);

inline bool HasActiveGuildBankInteraction(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return false;
  }

  const auto& guild = ::openwow::game::GuildSystem::Get();
  return guild.IsBankFrameOpen() && guild.GetBankerGuid() != 0;
}

inline std::uint32_t ResolveGuildBankItemDisplayId(
    const ::openwow::game::ItemDefinitions& item_definitions,
    const std::uint32_t item_entry) {
  if (const auto* item = item_definitions.GetItem(item_entry);
      item != nullptr) {
    return item->display_id;
  }

  return 0;
}

inline bool BeginHeldGuildBankCursor(lua_State* L, const std::uint8_t wire_tab,
                                     const std::uint8_t wire_slot,
                                     const ::openwow::game::ItemInstance& item,
                                     const std::uint32_t split_count = 0) {
  if (item.entry == 0) {
    return false;
  }

  const std::size_t linear_slot_index =
      static_cast<std::size_t>(wire_tab) *
          ::openwow::game::GuildSystem::kGuildBankSlotsPerTab +
      static_cast<std::size_t>(wire_slot);
  if (linear_slot_index >
      std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  const auto linear_slot =
      static_cast<std::uint32_t>(linear_slot_index);
  auto& guild = ::openwow::game::GuildSystem::Get();
  auto* session = GetWorldSession(L);
  auto* cursor = session != nullptr ? session->held_cursor() : nullptr;
  if (cursor == nullptr) {
    return false;
  }
  cursor->Clear();
  if (!guild.SetGuildBankItemLockByLinearIndexIfEntryMatches(
          linear_slot, item.entry, true)) {
    return false;
  }

  namespace held_cursor =
      ::openwow::game::actions::held_cursor;
  cursor->HoldGuildBankItem(
      held_cursor::GuildBankItem{
          .item_entry = item.entry,
          .display_id =
              ResolveGuildBankItemDisplayId(RequireItemDefinitions(L), item.entry),
          .linear_slot = linear_slot,
          .split_count = split_count,
      },
      held_cursor::Presentation{
          .texture_path =
              cursor_texture::ResolveItemTexturePath(L, item.entry),
          .texture_mode = held_cursor::TextureMode::HeldTexture,
          .sound = held_cursor::Sound::CursorGrabObject,
      });
  ScriptEventDispatch::Get().FireEvent(events::GUILDBANK_ITEM_LOCK_CHANGED);
  return true;
}

inline void ClearGuildBankItemLockAtLinearSlotAndNotify(
    const std::uint32_t item_entry, const std::uint32_t linear_slot) {
  if (item_entry == 0) {
    return;
  }
  (void)::openwow::game::GuildSystem::Get()
      .SetGuildBankItemLockByLinearIndexIfEntryMatches(
          linear_slot, item_entry, false);
  ScriptEventDispatch::Get().FireEvent(events::GUILDBANK_ITEM_LOCK_CHANGED);
}

inline std::optional<GuildBankCursorInventorySource>
ResolveGuildBankCursorInventorySource(
    const ::openwow::game::PlayerInventoryReplica& inventory,
    const ::openwow::game::ItemInstance& cursor_item) {
  const auto item_guid = cursor_item.guid;
  if (item_guid != 0) {
    if (const auto location =
            ::openwow::game::ResolvePlayerItemPacketLocationByGuid(
                inventory, item_guid);
        location.has_value()) {
      return GuildBankCursorInventorySource{
          .source_item = location->item,
          .source_bag = location->packet_bag,
          .source_slot = location->packet_slot,
      };
    }
  }

  auto matches_item =
      [&](const ::openwow::game::ItemInstance* item,
          const std::uint8_t source_bag,
          const std::uint8_t source_slot)
      -> std::optional<GuildBankCursorInventorySource> {
    if (item == nullptr || item->IsEmpty()) {
      return std::nullopt;
    }
    if (item_guid != 0 && item->guid != item_guid) {
      return std::nullopt;
    }
    if (item_guid == 0 && item->entry != cursor_item.entry) {
      return std::nullopt;
    }
    return GuildBankCursorInventorySource{
        .source_item = *item,
        .source_bag = source_bag,
        .source_slot = source_slot,
    };
  };

  for (std::uint8_t slot = 0;
       slot < ::openwow::game::PlayerInventoryReplica::kMaxEquipSlots; ++slot) {
    if (auto source = matches_item(
            inventory.GetEquipSlot(slot),
            ::openwow::game::InventorySlots::kMainBag, slot)) {
      return source;
    }
  }

  for (std::uint8_t slot = 0;
       slot < ::openwow::game::PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (auto source = matches_item(
            inventory.GetBackpackSlot(slot),
            ::openwow::game::InventorySlots::kMainBag,
            static_cast<std::uint8_t>(
                ::openwow::game::InventorySlots::kBackpackStart + slot))) {
      return source;
    }
  }

  for (std::uint8_t bag = 1; bag <= ::openwow::game::PlayerInventoryReplica::kMaxBags;
       ++bag) {
    const auto* bag_info = inventory.GetBag(bag);
    if (bag_info == nullptr) {
      continue;
    }

    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      if (auto source = matches_item(
              inventory.GetBagSlot(bag, slot),
              static_cast<std::uint8_t>(
                  ::openwow::game::InventorySlots::kBagSlotsStart + bag - 1),
              slot)) {
        return source;
      }
    }
  }

  return std::nullopt;
}

inline bool CanActivePlayerDepositIntoGuildBankTab(
    const ::openwow::game::WorldSession& session, const std::uint8_t wire_tab) {
  const auto* player = session.objects().GetActivePlayer();
  return player != nullptr &&
         wire_tab < ::openwow::game::GuildSystem::kGuildBankMaxTabs &&
         (::openwow::game::GuildSystem::Get().GetControlBankTabFlags(
              player->GetGuildRank(), wire_tab) &
          kGuildBankDepositTabFlag) != 0;
}

inline bool CanActivePlayerMoveGuildBankItemsFromTab(
    const ::openwow::game::WorldSession& session, const std::uint8_t wire_tab) {
  const auto* player = session.objects().GetActivePlayer();
  return player != nullptr &&
         wire_tab < ::openwow::game::GuildSystem::kGuildBankMaxTabs &&
         ::openwow::game::GuildSystem::Get().GetControlBankTabWithdrawLimit(
             player->GetGuildRank(), wire_tab) != 0;
}

inline bool ValidatePlayerItemForGuildBankDeposit(
    const ::openwow::game::WorldSession& session,
    const ::openwow::game::ItemInstance& source_item,
    const ::openwow::game::ItemTemplate& item_template,
    const std::uint8_t source_bag,
    const std::uint8_t source_slot,
    const std::uint8_t wire_tab) {
  if (source_item.IsSoulbound()) {
    DisplaySystemMessage(item_template.bonding == 4 ? kGuildBankQuestItemMessage
                                                    : kGuildBankBoundItemMessage);
    return false;
  }

  const auto* live_item =
      session.objects().GetItem(::openwow::game::ObjectGuid(source_item.guid));
  if ((live_item != nullptr && live_item->IsLocked()) ||
      source_item.IsConjured() ||
      source_item.duration != 0 || item_template.duration > 0 ||
      cursor_texture::ItemTemplateHasLocationRestriction(item_template)) {
    DisplaySystemMessage(kGuildBankConjuredItemMessage);
    return false;
  }

  if ((source_item.flags & ::openwow::game::ItemFlags::kWrapped) != 0) {
    DisplaySystemMessage(kGuildBankWrappedItemMessage);
    return false;
  }

  if (source_bag == ::openwow::game::InventorySlots::kMainBag &&
      source_slot < ::openwow::game::InventorySlots::kBackpackStart) {
    DisplaySystemMessage(kGuildBankEquippedItemMessage);
    return false;
  }

  if (!CanActivePlayerDepositIntoGuildBankTab(session, wire_tab)) {
    DisplaySystemMessage(kGuildPermissionsMessage);
    return false;
  }

  return true;
}

inline std::uint32_t GetHeldItemCountByLinearSlot(
    const GuildBankHeldItemView& held_state) {
  if (held_state.item_entry == 0 || held_state.split_count != 0) {
    return 0;
  }

  const auto tab = static_cast<std::uint8_t>(
      held_state.linear_slot / ::openwow::game::GuildSystem::kGuildBankSlotsPerTab);
  const auto slot = static_cast<std::uint8_t>(
      held_state.linear_slot % ::openwow::game::GuildSystem::kGuildBankSlotsPerTab);
  const auto* source_item =
      ::openwow::game::GuildSystem::Get().GetGuildBankTabItem(tab, slot);
  if (source_item == nullptr || source_item->IsEmpty()) {
    return 0;
  }

  return source_item->count;
}

inline bool ShouldCancelHeldGuildBankDropLocally(
    const GuildBankHeldItemView& held_state,
    const ::openwow::game::ItemTemplate* item_template,
    const ::openwow::game::ItemInstance* destination_item) {
  return destination_item != nullptr && !destination_item->IsEmpty() &&
         destination_item->entry == held_state.item_entry &&
         (item_template == nullptr || item_template->stackable <= 0);
}

inline std::uint32_t ComputeGuildBankHeldItemMoveCount(
    const GuildBankHeldItemView& held_state,
    const ::openwow::game::ItemTemplate* item_template,
    const ::openwow::game::ItemInstance* destination_item) {
  if (item_template == nullptr || item_template->stackable <= 0) {
    return 0;
  }

  if (destination_item == nullptr || destination_item->IsEmpty()) {
    return held_state.split_count;
  }

  if (destination_item->entry != held_state.item_entry) {
    return 0;
  }

  const auto max_stack_count =
      static_cast<std::uint32_t>(item_template->stackable);
  if (destination_item->count >= max_stack_count) {
    return 0;
  }

  const auto free_space = max_stack_count - destination_item->count;
  if (held_state.split_count != 0) {
    return std::min(held_state.split_count, free_space);
  }

  return std::min(GetHeldItemCountByLinearSlot(held_state), free_space);
}

inline std::uint32_t ComputeGuildBankPlayerMoveCount(
    const ::openwow::game::ItemInstance& cursor_item,
    const GuildBankCursorInventorySource& source,
    const ::openwow::game::ItemTemplate& item_template,
    const ::openwow::game::ItemInstance* destination_item) {
  const auto split_count =
      cursor_item.count > 0 && cursor_item.count < source.source_item.count
          ? cursor_item.count
          : 0u;
  if (destination_item == nullptr || destination_item->IsEmpty()) {
    return split_count;
  }

  if (destination_item->entry != source.source_item.entry ||
      item_template.stackable <= 0) {
    return 0;
  }

  const auto destination_count = destination_item->count;
  const auto max_stack_count =
      static_cast<std::uint32_t>(item_template.stackable);
  if (destination_count >= max_stack_count) {
    return 0;
  }

  const auto free_space = max_stack_count - destination_count;
  if (split_count != 0) {
    return std::min(split_count, free_space);
  }

  return std::min(source.source_item.count, free_space);
}

inline bool TryMoveCursorItemToGuildBankTab(::openwow::game::WorldSession& session,
                                            const std::uint64_t banker_guid,
                                            const std::uint8_t wire_tab,
                                            const std::uint8_t wire_slot) {
  auto* cursor = session.held_cursor();
  const auto* held_item =
      cursor != nullptr ? cursor->live_item() : nullptr;
  if (banker_guid == 0 || held_item == nullptr) {
    return false;
  }

  const GuildBankCursorInventorySource source{
      .source_item = held_item->item,
      .source_bag = held_item->source_bag,
      .source_slot = held_item->source_slot,
  };

  const auto* item_template =
      session.query_cache().GetOrRequestItemTemplate(source.source_item.entry);
  if (item_template == nullptr) {
    return false;
  }

  if (!ValidatePlayerItemForGuildBankDeposit(
          session, source.source_item, *item_template, source.source_bag,
          source.source_slot, wire_tab)) {
    return false;
  }

  const auto* destination_item =
      ::openwow::game::GuildSystem::Get().GetGuildBankTabItem(wire_tab, wire_slot);
  const auto destination_entry =
      destination_item != nullptr && !destination_item->IsEmpty()
          ? destination_item->entry
          : 0u;
  const auto move_count = ComputeGuildBankPlayerMoveCount(
      held_item->item, source, *item_template, destination_item);

  session.interaction().SendGuildBankSwapItemsPlayerToBank(
      banker_guid, wire_tab, wire_slot, destination_entry, source.source_bag,
      source.source_slot, move_count);
  cursor->Clear();
  ScriptEventDispatch::Get().FireEvent(events::GUILDBANKBAGSLOTS_CHANGED);
  return true;
}

inline bool TryUseContainerItemToCurrentGuildBankTab(
    lua_State* L, ::openwow::game::WorldSession& session, const int bag_id,
    const int slot) {
  if (!HasActiveGuildBankInteraction(L)) {
    return false;
  }

  auto& inventory = session.inventory_replica();
  const auto source = ResolveContainerDropTarget(inventory, bag_id, slot);
  if (!source.has_value() || source->item == nullptr || source->item->IsEmpty()) {
    return true;
  }

  if (session.held_cursor() != nullptr) {
    session.held_cursor()->Clear();
  }

  const auto* item_template =
      session.query_cache().GetOrRequestItemTemplate(source->item->entry);
  if (item_template == nullptr) {
    return true;
  }

  const auto current_tab =
      ::openwow::game::GuildSystem::Get().GetCurrentGuildBankTabIndex();
  if (!ValidatePlayerItemForGuildBankDeposit(
          session, *source->item, *item_template, source->player_bag,
          source->player_slot, current_tab)) {
    return true;
  }

  session.interaction().SendGuildBankSwapItemsPlayerToBank(
      ::openwow::game::GuildSystem::Get().GetBankerGuid(), current_tab, 0xFFu,
      0, source->player_bag, source->player_slot, 0);
  if (session.held_cursor() != nullptr) {
    session.held_cursor()->Clear();
  }
  ScriptEventDispatch::Get().FireEvent(events::GUILDBANKBAGSLOTS_CHANGED);
  return true;
}

inline std::optional<GuildBankContainerDropTarget>
ResolveContainerDropTarget(const ::openwow::game::PlayerInventoryReplica& inventory,
                           const int bag_id,
                           const int slot) {
  if (slot < 0) {
    return std::nullopt;
  }

  if (bag_id == 0) {
    if (slot >= static_cast<int>(::openwow::game::PlayerInventoryReplica::kBackpackSize)) {
      return std::nullopt;
    }
    return GuildBankContainerDropTarget{
        .player_bag = ::openwow::game::InventorySlots::kMainBag,
        .player_slot = static_cast<std::uint8_t>(
            ::openwow::game::InventorySlots::kBackpackStart + slot),
        .item = inventory.GetBackpackSlot(static_cast<std::uint8_t>(slot)),
    };
  }

  if (bag_id < 1 ||
      bag_id > static_cast<int>(::openwow::game::PlayerInventoryReplica::kMaxBags)) {
    return std::nullopt;
  }

  const auto* bag = inventory.GetBag(static_cast<std::uint8_t>(bag_id));
  if (bag == nullptr || slot >= static_cast<int>(bag->num_slots)) {
    return std::nullopt;
  }

  return GuildBankContainerDropTarget{
      .player_bag = static_cast<std::uint8_t>(
          ::openwow::game::InventorySlots::kBagSlotsStart + bag_id - 1),
      .player_slot = static_cast<std::uint8_t>(slot),
      .item = inventory.GetBagSlot(static_cast<std::uint8_t>(bag_id),
                                   static_cast<std::uint8_t>(slot)),
  };
}

}
