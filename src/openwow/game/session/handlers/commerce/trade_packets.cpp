#include "openwow/game/session/handlers/commerce/trade_packets.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/commerce/trade/adapters/protocol/trade_packet_codec.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/adapters/protocol/inventory_messages.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/social_manager.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include <optional>
#include <string>
#include <utility>

namespace openwow::game {

namespace {

void PresentTradeAcceptEvent(const TradeAcceptUpdateEvent& event) {
  ui::game::ScriptEventDispatch::Get().FireTradeAcceptUpdate(
      event.player_accepted, event.trader_accepted);
}

void PresentTradeAcceptEvents(const TradeAcceptTransition& transition) {
  for (const auto& event : transition.events) {
    PresentTradeAcceptEvent(event);
  }
}

bool CanPerformTradeProtectedAction() {
  return ui::game::GameUI_CanPerformProtectedAction(
             ui::game::protected_action_kind::kTrade) != 0;
}

void ResetTradeAcceptanceForTradeChange(
    TradeInteraction& trade, InteractionSender& interaction) {
  const auto transition = trade.ResetAcceptedStateForTradeChange(
      CanPerformTradeProtectedAction());
  auto next_event = transition.events.begin();

  if (transition.send_unaccept_packet && next_event != transition.events.end()) {
    PresentTradeAcceptEvent(*next_event);
    ++next_event;
  }
  if (transition.send_unaccept_packet) {
    interaction.SendUnacceptTrade();
  }
  for (; next_event != transition.events.end(); ++next_event) {
    PresentTradeAcceptEvent(*next_event);
  }
}

void DisplayTradeMessage(int message_id, std::string name = {}) {
  if (name.empty()) {
    ui::game::DisplaySystemMessage(message_id);
  } else {
    ui::game::DisplaySystemMessage(message_id, name.c_str());
  }
}

void DisplayTradePartnerMessage(
    const ObjectManager& objects, const QueryCache& queries,
    int message_id, std::uint64_t guid) {
  if (guid == 0 ||
      objects.GetUnit(ObjectGuid(guid)) == nullptr) {
    return;
  }

  std::string name;
  if (const auto* known = queries.GetPlayerName(guid)) {
    name = known->name;
  } else {
    name = objects.GetPlayerName(ObjectGuid(guid));
  }
  DisplayTradeMessage(message_id, std::move(name));
}

void PresentTradeChanges(TradeInteraction& trade) {
  const auto changes = trade.TakeChanges();
  for (const auto guid : changes.released_item_guids) {
    ui::game::GameUI_OnMouseoverUnitLeave(guid);
  }
  if (changes.closed) {
    ui::game::ScriptEventDispatch::Get().FireTradeClosed();
  }
}

std::uint32_t ResolveTradeCloseMessageId(
    std::uint32_t reason_code, bool alternate_message) {
  switch (reason_code) {
  case 4:
    return alternate_message ? 207 : 208;
  case 17:
    return alternate_message ? 210 : 209;
  case 28:
    return alternate_message ? 730 : 226;
  case 84:
    return alternate_message ? 627 : 626;
  default:
    return static_cast<std::uint32_t>(
        InventoryResultCodeToSystemMessageId(reason_code));
  }
}

}

void HandleTradeStatusPacket(
    TradeInteraction& trade, InteractionSender& interaction,
    const SocialManager& social, const PlayerInventoryReplica& inventory,
    actions::held_cursor::HeldCursor* held_cursor,
    const ObjectManager& objects,
    const QueryCache& queries, const bool cinematic_active,
    const bool has_blocking_interaction,
    const net::wotlk::WorldPacket& pkt) {
  auto message =
      trade_protocol::DecodeStatus(pkt.payload.data(), pkt.payload.size());
  if (!message) {
    return;
  }
  auto& events = ui::game::ScriptEventDispatch::Get();
  trade.HandleTradeStatus(*message);
  PresentTradeChanges(trade);

  TradeAcceptTransition transition;
  const auto &status_context = trade.last_status_context();
  if (trade.last_status_code() == 13) {
    events.FireTradeUpdate();
    return;
  }
  switch (trade.last_status()) {
  case TradeStatus::kBusy:
  case TradeStatus::kBusy2:
    if (status_context.local_initiate_active) {
      DisplayTradePartnerMessage(
          objects, queries, 68, status_context.partner_guid);
    }
    break;
  case TradeStatus::kBeginTrade: {
    if (status_context.auto_busy_begin_trade) {
      interaction.SendBusyTrade();
      break;
    }

    const auto guid = status_context.partner_guid;

    if (social.IsIgnored(ObjectGuid(guid))) {
      interaction.SendIgnoreTrade();
      break;
    }

    if (objects.GetUnit(ObjectGuid(guid)) == nullptr) {
      break;
    }

    const bool blocks_trades =
        ui::game::CVarSystem::Instance().GetCVarBool("BlockTrades");
    if (cinematic_active || has_blocking_interaction || blocks_trades) {
      interaction.SendBusyTrade();
      if (blocks_trades) {
        DisplayTradePartnerMessage(objects, queries, 202, guid);
      }
      break;
    }

    interaction.SendBeginTrade();
    break;
  }
  case TradeStatus::kOpenWindow:
    if (trade.ShouldAutoPlaceHeldCursorItemOnOpen()) {
      if (const auto placement =
              ResolveAutoTradePlacement(
                  trade, inventory, held_cursor, trade.begin_trade_guid());
          placement.has_value()) {
        if (interaction.SendSetTradeItem(
                placement->trade_slot, placement->source_bag,
                placement->source_slot) &&
            trade.SetLocalPlayerTradeSlot(placement->trade_slot, placement->item_guid,
                                           placement->source_bag, placement->source_slot)) {
          if (held_cursor != nullptr) {
            held_cursor->Clear({
                .release_source_lease = false,
                .publish_money_owner_update = true,
            });
          }
          events.FirePlayerTradeMoney();
          events.FireTradeUpdate();
        }
      }
    }
    events.FireTradeShow();
    break;
  case TradeStatus::kTradeCanceled:
    events.FireTradeUpdate();
    events.FireTradeRequestCancel();
    events.FireTradeClosed();
    interaction.SendCancelTrade();
    DisplayTradeMessage(205);
    break;
  case TradeStatus::kTradeAccept:
    transition = trade.SetTraderAcceptedState(true);
    break;
  case TradeStatus::kNoTarget:
    DisplayTradeMessage(199);
    break;
  case TradeStatus::kTradeUnaccept:
    transition = trade.SetTraderAcceptedState(false);
    break;
  case TradeStatus::kBackToTrade:
    ResetTradeAcceptanceForTradeChange(trade, interaction);
    break;
  case TradeStatus::kTradeComplete:
    events.FireTradeClosed();
    interaction.SendCancelTrade();
    DisplayTradeMessage(206);
    break;
  case TradeStatus::kTargetTooFar:
    DisplayTradeMessage(204);
    break;
  case TradeStatus::kWrongFaction:
    DisplayTradeMessage(269);
    break;
  case TradeStatus::kCloseWindow:
    DisplayTradeMessage(
        static_cast<int>(ResolveTradeCloseMessageId(
                    status_context.reason_code,
                    status_context.reason_has_alternate_message)));
    events.FireTradeClosed();
    interaction.SendCancelTrade();
    break;
  case TradeStatus::kIgnoreYou:
    if (status_context.local_initiate_active) {
      DisplayTradePartnerMessage(
          objects, queries, 336, status_context.partner_guid);
    }
    break;
  case TradeStatus::kYouStunned:
    DisplayTradeMessage(434);
    break;
  case TradeStatus::kTargetStunned:
    DisplayTradeMessage(435);
    break;
  case TradeStatus::kYouDead:
    DisplayTradeMessage(135);
    break;
  case TradeStatus::kTargetDead:
    DisplayTradeMessage(203);
    break;
  case TradeStatus::kYouLogout:
    DisplayTradeMessage(446);
    break;
  case TradeStatus::kTargetLogout:
    DisplayTradeMessage(447);
    break;
  case TradeStatus::kTrialAccount:
    DisplayTradeMessage(488);
    break;
  case TradeStatus::kOnlyConjured:
    DisplayTradeMessage(504);
    if (status_context.slot == 0xFF) {
      events.FirePlayerTradeMoney();
      events.FireTradeUpdate();
    } else {
      events.FireTradePlayerItemChanged(
          static_cast<int>(status_context.slot) + 1);
    }
    break;
  case TradeStatus::kNotEligible:
    DisplayTradeMessage(505);
    if (status_context.slot == 0xFF) {
      events.FirePlayerTradeMoney();
      events.FireTradeUpdate();
    } else {
      events.FireTradePlayerItemChanged(
          static_cast<int>(status_context.slot) + 1);
    }
    break;
  default:
    break;
  }

  PresentTradeAcceptEvents(transition);
}

void HandleTradeExtendedPacket(
    TradeInteraction& trade, InteractionSender& interaction,
    const net::wotlk::WorldPacket& pkt) {
  auto& events = ui::game::ScriptEventDispatch::Get();
  constexpr std::size_t kExtendedPrefixSize = 9;
  const auto prefix = trade_protocol::DecodeExtendedPrefix(
      pkt.payload.data(), pkt.payload.size());
  if (!prefix) {
    return;
  }

  const auto disposition = trade.ClassifyTradeExtended(*prefix);
  if (disposition == TradeExtendedDisposition::kCancelMismatchedTrade) {
    interaction.SendCancelTrade();
    return;
  }
  if (disposition == TradeExtendedDisposition::kIgnoredStale) {
    return;
  }
  if (disposition == TradeExtendedDisposition::kInvalidPrefix) {
    return;
  }

  auto snapshot = trade_protocol::DecodeExtendedSnapshot(
      *prefix, pkt.payload.data() + kExtendedPrefixSize,
      pkt.payload.size() - kExtendedPrefixSize);
  if (!snapshot) {
    return;
  }
  trade.ApplyTradeExtendedSnapshot(std::move(*snapshot));
  PresentTradeChanges(trade);

  const auto &update = trade.last_trade_extended_update();
  ResetTradeAcceptanceForTradeChange(trade, interaction);

  for (std::size_t i = 0; i < update.target_slot_changed.size(); ++i) {
    if (update.target_slot_changed[i]) {
      events.FireTradeTargetItemChanged(static_cast<int>(i) + 1);
    }
    if (update.player_slot_removed[i]) {
      events.FireTradePlayerItemChanged(static_cast<int>(i) + 1);
    }
  }
  if (update.target_slot7_text_changed) {
    events.FireTradeTargetItemChanged(kTradeSlotCount);
  }
  if (update.player_slot7_text_changed) {
    events.FireTradePlayerItemChanged(kTradeSlotCount);
  }
  if (update.player_money_changed) {
    events.FirePlayerTradeMoney();
    events.FireTradeUpdate();
  }
  if (update.target_money_changed) {
    events.FireTradeMoneyChanged();
  }
}

}
