#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
}
namespace openwow::game {
class ItemDefinitions;
class PlayerInventoryReplica;

namespace inventory::ui {

enum class ItemCursorLeaveEvents : std::uint8_t {
  kPreserve,
  kFire,
};

enum class ItemCursorPickupSound : std::uint8_t {
  kSilent,
  kGrab,
};

enum class ItemCursorSourcePolicy : std::uint8_t {
  kCanonicalInventory,
  kProvidedOrigin,
};

enum class ItemCursorPickupResult : std::uint8_t {
  kPickedUp,
  kItemNotFound,
};

struct ItemCursorSource {
  ObjectGuid container;
  std::int32_t slot = 0;
};

struct ItemCursorPickupOptions {
  ItemCursorSource source;
  std::int32_t split_count = 0;
  ItemCursorSourcePolicy source_policy = ItemCursorSourcePolicy::kCanonicalInventory;
  ItemCursorLeaveEvents leave_events = ItemCursorLeaveEvents::kFire;
  ItemCursorPickupSound sound = ItemCursorPickupSound::kGrab;
};

ItemCursorPickupResult PickupItemCursor(
    actions::held_cursor::HeldCursor& cursor,
    const PlayerInventoryReplica& inventory, const ItemDefinitions& items,
    const openwow::data::dbc::DbcLoader* dbc, ObjectGuid active_player,
    ObjectGuid item_guid, const ItemCursorPickupOptions& options = {});

[[nodiscard]] actions::held_cursor::Grid ResolveItemCursorGrid(
    const ItemDefinitions& items, std::uint32_t item_entry);

}
}
