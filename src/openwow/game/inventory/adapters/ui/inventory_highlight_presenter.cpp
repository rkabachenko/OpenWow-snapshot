#include "openwow/game/inventory/adapters/ui/inventory_highlight_presenter.h"

#include "openwow/game/inventory/adapters/protocol/inventory_messages.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"

namespace openwow::game::inventory::ui {
namespace {

void ClearItemHighlight(ObjectManager &objects, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return;
  }

  auto *item = objects.GetMutable(guid);
  if (item == nullptr || !item->IsItem()) {
    return;
  }

  item->DisableMouseoverHighlightAndNotify();
  if (!item->IsContainer()) {
    return;
  }

  const auto &container = static_cast<const CGContainer_C &>(*item);
  for (std::uint32_t slot = 0; slot < container.GetNumSlots(); ++slot) {
    auto *contained_item = objects.GetMutable(container.GetSlot(slot));
    if (contained_item != nullptr && contained_item->IsItem()) {
      contained_item->DisableMouseoverHighlightAndNotify();
    }
  }
}

}

void ClearActivePlayerItemHighlights(ObjectManager &objects) {
  const auto *player = objects.GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  for (std::uint8_t slot = 0; slot < inventory_constants::kBagSlotsEnd; ++slot) {
    ClearItemHighlight(objects, player->GetInventorySlotGuid(slot));
  }

  constexpr auto backpack_slot_count =
      inventory_constants::kBackpackEnd - inventory_constants::kBackpackStart;
  for (std::uint8_t slot = 0; slot < backpack_slot_count; ++slot) {
    ClearItemHighlight(objects, player->GetBackpackItem(slot));
  }
}

}
