#include "openwow/game/inventory/adapters/ui/held_cursor_item_use_controller.h"

#include "openwow/game/localization.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/update_fields.h"
#include "openwow/ui/game/error_message.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::game::inventory::ui {

HeldCursorItemDropResult PromptDeleteHeldCursorItem(
    ObjectManager& objects, QueryCache& queries,
    const HeldCursorItem held_item) {
  if (held_item.item.IsEmpty() || held_item.container.IsEmpty()) {

    return HeldCursorItemDropResult::kNoHeldItem;
  }

  const auto* item = objects.GetItem(held_item.item);
  if (item == nullptr) {

    return HeldCursorItemDropResult::kItemNotFound;
  }

  const auto* active_player = objects.GetActivePlayer();
  if (active_player == nullptr) {
    return HeldCursorItemDropResult::kNoActivePlayer;
  }

  if (item->GetOwner() != active_player->GetGuid()) {
    return HeldCursorItemDropResult::kNotOwned;
  }

  if ((item->GetItemFlags() & kItemFlagIndestructible) != 0u) {
    auto& localization = Localization::Get();
    ::openwow::ui::game::ErrorMessageSystem::Get().ShowError(
        localization.GetString("ERR_ITEM_CANT_BE_DESTROYED",
                               "That item cannot be destroyed."));
    return HeldCursorItemDropResult::kIndestructible;
  }

  const auto item_entry = item->GetUInt32(OBJECT_FIELD_ENTRY);
  const auto* item_template = queries.GetOrRequestItemTemplate(item_entry);
  if (item_template == nullptr) {

    return HeldCursorItemDropResult::kTemplatePending;
  }

  ::openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ::openwow::ui::game::events::DELETE_ITEM_CONFIRM,
      {item_template->name,
       static_cast<int>(item_template->quality)});
  return HeldCursorItemDropResult::kPrompted;
}

}
