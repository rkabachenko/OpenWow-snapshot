#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"

#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace openwow::game::inventory::ui {
namespace {

std::string ResolveItemTexturePath(
    const ItemDefinitions& item_definitions, const data::dbc::DbcLoader* dbc,
    const std::uint32_t item_entry) {
  const auto *item = item_definitions.GetItem(item_entry);
  if (item == nullptr || dbc == nullptr || item->display_id == 0) {
    return {};
  }

  return ResolveItemInventoryIconTexturePath(dbc, item->display_id);
}

ItemCursorSource ResolveCanonicalSource(const ObjectGuid active_player,
                                        const PlayerInventoryReplica &inventory,
                                        const PlayerItemPacketLocation &location,
                                        const ItemCursorSource provided_source) {
  if (location.packet_bag == InventorySlots::kMainBag) {
    if (active_player.IsEmpty()) {
      return provided_source;
    }

    return ItemCursorSource{
        .container = active_player,
        .slot = static_cast<std::int32_t>(location.packet_slot),
    };
  }

  if (location.packet_bag >= InventorySlots::kBagSlotsStart &&
      location.packet_bag < InventorySlots::kBagSlotsEnd) {
    const auto bag_index = static_cast<std::uint8_t>(
        location.packet_bag - InventorySlots::kBagSlotsStart + 1);
    const auto *bag = inventory.GetBag(bag_index);
    if (bag != nullptr && bag->guid != 0) {
      return ItemCursorSource{
          .container = ObjectGuid(bag->guid),
          .slot = static_cast<std::int32_t>(location.packet_slot),
      };
    }
  }

  if (location.packet_bag >= InventorySlots::kBankBagStart &&
      location.packet_bag < InventorySlots::kBankBagEnd) {
    const auto bag_index =
        static_cast<std::uint8_t>(location.packet_bag - InventorySlots::kBankBagStart);
    const auto *bag = inventory.GetBankBag(bag_index);
    if (bag != nullptr && bag->guid != 0) {
      return ItemCursorSource{
          .container = ObjectGuid(bag->guid),
          .slot = static_cast<std::int32_t>(location.packet_slot),
      };
    }
  }

  return provided_source;
}

}

actions::held_cursor::Grid ResolveItemCursorGrid(
    const ItemDefinitions& items, const std::uint32_t item_entry) {
  const auto* item = items.GetItem(item_entry);
  if (item == nullptr) {

    return actions::held_cursor::Grid::None;
  }

  const bool has_on_use_spell = std::any_of(
      item->spells.begin(), item->spells.end(), [](const ItemSpellData& spell) {
        return spell.spell_id != 0 && spell.trigger == 0;
      });

  if (has_on_use_spell || item->inventory_type != InventoryType::NonEquip) {
    return actions::held_cursor::Grid::ActionBar;
  }
  return actions::held_cursor::Grid::None;
}

ItemCursorPickupResult PickupItemCursor(
    actions::held_cursor::HeldCursor& cursor,
    const PlayerInventoryReplica& inventory, const ItemDefinitions& items,
    const data::dbc::DbcLoader* dbc, const ObjectGuid active_player,
    const ObjectGuid item_guid, const ItemCursorPickupOptions &options) {
  const auto location =
      ResolvePlayerItemPacketLocationByGuid(inventory, item_guid.GetRawValue());
  if (!location.has_value()) {
    return ItemCursorPickupResult::kItemNotFound;
  }

  auto source = options.source;
  if (options.source_policy == ItemCursorSourcePolicy::kCanonicalInventory) {
    source = ResolveCanonicalSource(
        active_player, inventory, *location, options.source);
  }
  cursor.Clear({
      .release_source_lease =
          options.leave_events == ItemCursorLeaveEvents::kFire,
      .publish_money_owner_update = true,
  });
  cursor.HoldLiveItem(
      actions::held_cursor::LiveItem{
          .item = location->item,
          .source_container_guid = source.container.GetRawValue(),
          .source_bag = location->packet_bag,
          .source_slot = static_cast<std::uint8_t>(source.slot),
          .auxiliary_value =
              static_cast<std::uint32_t>(std::max(options.split_count, 0)),
      },
      actions::held_cursor::Presentation{
          .texture_path =
              ResolveItemTexturePath(items, dbc,
                                     location->item.entry),
          .texture_mode = actions::held_cursor::TextureMode::HeldTexture,
          .sound = options.sound == ItemCursorPickupSound::kGrab
                       ? actions::held_cursor::Sound::CursorGrabObject
                       : actions::held_cursor::Sound::None,
          .grid = ResolveItemCursorGrid(items, location->item.entry),
      });
  return ItemCursorPickupResult::kPickedUp;
}

}
