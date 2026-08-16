#include "openwow/ui/surfaces/game/adapters/held_cursor/world_held_cursor_source.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/game/guild_system.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/item_interaction_lease.h"
#include "openwow/game/inventory/adapters/ui/item_target_cursor_presenter.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::ui::game {
namespace {

namespace InventorySlots = openwow::game::InventorySlots;
using openwow::game::actions::held_cursor::LiveItem;

void FireItemLockEvent(ScriptEventDispatch& events, const char* event,
                       const LiveItem& item) {
  const auto slot = static_cast<int>(item.source_slot);
  if (item.source_bag == InventorySlots::kMainBag) {

    if (slot >= InventorySlots::kBackpackStart &&
        slot < InventorySlots::kBackpackEnd) {
      events.FireEventArgs(event,
                           {0, slot - InventorySlots::kBackpackStart + 1});
      return;
    }
    if (slot >= InventorySlots::kBankStart &&
        slot < InventorySlots::kBankEnd) {
      events.FireEventArgs(event,
                           {-1, slot - InventorySlots::kBankStart + 1});
      return;
    }
    if (slot >= InventorySlots::kKeyringStart &&
        slot < InventorySlots::kKeyringEnd) {
      events.FireEventArgs(event,
                           {-2, slot - InventorySlots::kKeyringStart + 1});
      return;
    }
    events.FireEventArgs(event, {slot + 1});
    return;
  }

  if (item.source_bag >= InventorySlots::kBagSlotsStart &&
      item.source_bag < InventorySlots::kBagSlotsEnd) {
    const int lua_bag =
        item.source_bag - InventorySlots::kBagSlotsStart + 1;
    events.FireEventArgs(event, {lua_bag, slot + 1});
    return;
  }
  if (item.source_bag >= InventorySlots::kBankBagStart &&
      item.source_bag < InventorySlots::kBankBagEnd) {
    const int lua_bag =
        item.source_bag - InventorySlots::kBankBagStart + 5;
    events.FireEventArgs(event, {lua_bag, slot + 1});
    return;
  }

  events.FireEventArgs(event,
                       {static_cast<int>(item.source_bag), slot + 1});
}

}

void WorldHeldCursorSource::BindSpellTargeting(
    openwow::game::SpellTargeting* spell_targeting) {
  spell_targeting_ = spell_targeting;
}

void WorldHeldCursorSource::BindItemLocks(
    openwow::game::ItemLockRegistry* item_locks) {
  item_locks_ = item_locks;
}

void WorldHeldCursorSource::CancelItemTargeting() {
  if (spell_targeting_ == nullptr) {
    return;
  }
  if (openwow::game::inventory::ui::HasActiveItemTargetCursor(
          *spell_targeting_)) {
    openwow::game::inventory::ui::ClearItemTargetCursor(*spell_targeting_);
  }
}

void WorldHeldCursorSource::AcquireLiveItemLease(
    const openwow::game::actions::held_cursor::LiveItem& item) {
  if (item_locks_ == nullptr ||
      !item_locks_->Acquire(openwow::game::ObjectGuid(item.item.guid),
                            openwow::game::ItemLockOwner::kCursor)) {
    return;
  }
  events_.FireEvent(events::ITEM_LOCK_CHANGED);
  FireItemLockEvent(events_, events::ITEM_LOCKED, item);
}

void WorldHeldCursorSource::ReleaseLiveItemLease(
    const openwow::game::actions::held_cursor::LiveItem& item) {
  if (item_locks_ == nullptr ||
      !item_locks_->Release(openwow::game::ObjectGuid(item.item.guid),
                            openwow::game::ItemLockOwner::kCursor)) {
    return;
  }

  events_.FireEvent(events::ITEM_LOCK_CHANGED);
  FireItemLockEvent(events_, events::ITEM_UNLOCKED, item);
}

void WorldHeldCursorSource::UnlockGuildBankItem(
    const openwow::game::actions::held_cursor::GuildBankItem& item) {
  if (item.item_entry == 0) {
    return;
  }

  static_cast<void>(guild_.SetGuildBankItemLockByLinearIndexIfEntryMatches(
      item.linear_slot, item.item_entry, false));
  events_.FireEvent(events::GUILDBANK_ITEM_LOCK_CHANGED);
}

void WorldHeldCursorSource::PublishGuildBankMoneyOwnerUpdate() {
  events_.FireEvent(events::GUILDBANK_UPDATE_MONEY);
}

}
