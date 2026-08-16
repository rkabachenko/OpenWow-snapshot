#include "openwow/game/targeting/adapters/ui/target_interaction_controller.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/targeting.h"
#include "openwow/game/commerce/trade/adapters/protocol/trade_packet_codec.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

namespace openwow::game::targeting::ui {
namespace {

bool HeldCursorCanStartTrade(const actions::held_cursor::HeldCursor& cursor) {
  const auto* held_item = cursor.live_item();
  if (held_item != nullptr && held_item->item.guid != 0 &&
      held_item->source_container_guid != 0) {
    return true;
  }

  const auto* money = cursor.get_if<actions::held_cursor::PlayerMoney>();
  return money != nullptr && money->amount != 0;
}

bool ValidateHeldTradeItemPlacement(
    WorldSession &session,
    const actions::held_cursor::LiveItem* held_item) {
  if (held_item == nullptr || held_item->item.guid == 0 ||
      held_item->source_container_guid == 0) {
    return true;
  }

  if (const auto *container =
          session.objects().GetContainer(ObjectGuid(held_item->item.guid));
      container != nullptr &&
      container->GetNumFreeSlots() != container->GetNumSlots()) {
    ::openwow::ui::game::DisplaySystemMessage(47);
    return false;
  }

  const auto absolute_slot =
      session.inventory_replica().FindSlotByGuid(held_item->item.guid);
  if (absolute_slot >= InventorySlots::kBagSlotsStart &&
      absolute_slot < InventorySlots::kBagSlotsEnd) {
    ::openwow::ui::game::DisplaySystemMessage(16);
    return false;
  }

  return true;
}

}

void HandleHeldCursorLeftClickOnPlayer(WorldSession &session,
                                       const CGUnit_C &player,
                                       const CGUnit_C &target) {
  auto* cursor = session.held_cursor();
  if (cursor == nullptr ||
      session.spells().GetTargeting().GetSpellId() != 0 ||
      !HeldCursorCanStartTrade(*cursor) || !target.IsPlayer()) {
    return;
  }

  const auto target_guid = target.GetGuid().GetRawValue();
  if (!ValidateHeldTradeItemPlacement(session, cursor->live_item())) {
    return;
  }

  if (player.State().IsDeadOrGhost()) {
    ::openwow::ui::game::DisplaySystemMessage(135);
    return;
  }

  if (!player.Interaction().IsFriendlyTo(target) ||
      player.Interaction().IsHostileTo(target)) {
    ::openwow::ui::game::DisplaySystemMessage(269);
    return;
  }

  if (const auto trade_guid = session.trade().begin_trade_guid();
      trade_guid != 0) {
    if (trade_guid != target_guid) {
      ::openwow::ui::game::DisplaySystemMessage(211);
      return;
    }

    const auto placement =
        ::openwow::game::ResolveAutoTradePlacement(
            session.trade(), session.inventory_replica(), cursor,
            target_guid);
    if (!placement.has_value()) {
      return;
    }

    if (session.Send(trade_protocol::EncodeSetItem(
            placement->trade_slot, placement->source_bag,
            placement->source_slot)) &&
        session.trade().SetLocalPlayerTradeSlot(
            placement->trade_slot, placement->item_guid,
            placement->source_bag, placement->source_slot)) {
      ::openwow::ui::game::ScriptEventDispatch::Get()
          .FireTradePlayerItemChanged(
              static_cast<int>(placement->trade_slot) + 1);
      cursor->Clear({
          .release_source_lease = false,
          .publish_money_owner_update = true,
      });
    }
    return;
  }

  session.interaction().SendInitiateTrade(target_guid, true);
}

namespace {

void TryHandleHeldCursorPetInteraction(WorldSession &session,
                                       const CGPlayer_C &player,
                                       const CGUnit_C &target) {
  if (TryCastHeldItemSpellOnOwnedPet(session, player, target)) {
    if (session.held_cursor() != nullptr) {
      session.held_cursor()->Clear();
    }
  }
}

}

bool TryCastHeldItemSpellOnOwnedPet(WorldSession &session,
                                    const CGPlayer_C &player,
                                    const CGUnit_C &target) {
  const auto* held_item =
      session.held_cursor() != nullptr
          ? session.held_cursor()->live_item()
          : nullptr;
  const auto remembered_spell_id =
      session.spells().GetRememberedHeldItemPetTargetSpellId();
  if (held_item == nullptr || held_item->item.guid == 0 ||
      remembered_spell_id == 0 ||
      player.State().GetPetGUID() != target.GetGuid()) {
    return false;
  }

  if (!SpellQueryBridge::Get().Query(remembered_spell_id).has_value()) {
    return false;
  }

  return session.interaction().SendCastSpellOnUnitAndItem(
      remembered_spell_id, 0, target.GetGuid().GetRawValue(),
      held_item->item.guid);
}

TargetInteractionController::TargetInteractionController(
    TargetingSystem &targeting) noexcept
    : targeting_(targeting) {}

TargetInteractionResult TargetInteractionController::InteractWithTarget(
    WorldSession &session, const ObjectGuid target) {
  const auto *active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return TargetInteractionResult::kNoActivePlayer;
  }

  const auto *object = session.objects().Get(target);
  if (object == nullptr) {
    return TargetInteractionResult::kObjectNotFound;
  }

  if (object->IsUnit()) {
    const auto &target_unit = static_cast<const CGUnit_C &>(*object);

    TryHandleHeldCursorPetInteraction(session, *active_player, target_unit);
  }

  return targeting_.InteractWith(target.GetRawValue())
             ? TargetInteractionResult::kInteracted
             : TargetInteractionResult::kObjectNotFound;
}

}
