#include "openwow/game/session/handlers/commerce/mail_packets.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
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
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/lcd_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/money_display.h"
#include "openwow/game/object_types.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/player_bag_family.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/tabard_frame.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/trainer_frame.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_gossip.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
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

std::optional<int> ResolveSendMailResultSystemMessageId(const MailSendResult &result) {
  switch (static_cast<std::uint32_t>(result.error)) {
  case 0:
    return result.action == MailResponseType::kSend ? std::optional<int>{383} : std::nullopt;
  case 1:
    return InventoryResultCodeToSystemMessageId(result.equip_error);
  case 2:
    return 379;
  case 3:
    return 40;
  case 4:
    return 380;
  case 5:
    return 269;
  case 14:
    return 488;
  case 15:
    return 497;
  case 16:
    return 382;
  case 17:
    return 561;
  case 18:
    return 585;
  case 19:
    return 586;
  case 20:
    return std::nullopt;
  case 21:
    return 587;
  default:
    return 381;
  }
}

void DispatchSendMailResultPresentation(
    MailInteraction& mail, const MailSendResult &result) {
  if (const auto message_id = ResolveSendMailResultSystemMessageId(result);
      message_id.has_value()) {
    ui::game::DisplaySystemMessage(*message_id);
  }

  if (result.error == MailResult::kOk && result.action == MailResponseType::kSend) {
    mail.ResetCompose();
    auto& events = ui::game::ScriptEventDispatch::Get();
    events.FireEvent(ui::game::events::SEND_MAIL_MONEY_CHANGED);
    events.FireEvent(ui::game::events::SEND_MAIL_COD_CHANGED);
    events.FireMailSendSuccess();
  }
}

}

void FlushMailProtocolUpdates(MailInteraction& mail,
                              const MailPacketSender& send) {
  if (mail.ConsumeNextMailTimeQueryRequest()) {
    send(mail_protocol::EncodeQueryNextMailTime());
  }
  if (mail.ConsumePendingMailUpdateEvent()) {
    ui::game::ScriptEventDispatch::Get().FireUpdatePendingMail();
  }
  if (mail.ConsumeMailInboxUpdateEvent()) {
    ui::game::ScriptEventDispatch::Get().FireMailInboxUpdate();
  }
  if (mail.ConsumeSendInfoUpdateEvent()) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::MAIL_SEND_INFO_UPDATE);
  }
}

void HandleMailListPacket(MailInteraction& mail, SocialManager& social,
                          QueryCache& queries,
                          const MailPacketSender& send,
                          const net::wotlk::WorldPacket& pkt) {
  auto snapshot =
      mail_protocol::DecodeMailList(pkt.payload.data(), pkt.payload.size());
  if (!snapshot) {
    return;
  }
  const auto result =
      mail.HandleMailListResult(std::move(*snapshot), &social, &queries);
  for (const auto& followup : result.followups) {
    send(mail_protocol::EncodeFollowup(followup));
  }
  if (mail.ConsumeNextMailTimeQueryRequest()) {
    send(mail_protocol::EncodeQueryNextMailTime());
  }
  ui::game::ScriptEventDispatch::Get().FireMailInboxUpdate();
}

void HandleSendMailResultPacket(MailInteraction& mail,
                                const bool local_player_exists,
                                const MailPacketSender& send,
                                const net::wotlk::WorldPacket& pkt) {
  auto result =
      mail_protocol::DecodeActionResult(pkt.payload.data(), pkt.payload.size());
  if (!result) {
    return;
  }
  const auto changes =
      mail.HandleSendMailResult(std::move(*result), local_player_exists);

  const auto& applied_result = mail.last_result();
  if (!applied_result.has_value()) {
    return;
  }

  DispatchSendMailResultPresentation(mail, *applied_result);
  if (changes.show_attachment_autoloot_error) {
    ui::game::DisplaySystemMessage(40);
  }
  for (const auto& followup : changes.followups) {
    send(mail_protocol::EncodeFollowup(followup));
  }

  for (const auto index : changes.closed_inbox_indices) {
    ui::game::ScriptEventDispatch::Get().FireEventArgs(
        ui::game::events::CLOSE_INBOX_ITEM, {static_cast<int>(index)});
  }
  if (changes.inbox_updated) {
    ui::game::ScriptEventDispatch::Get().FireMailInboxUpdate();
  }

  const bool success = applied_result->error == MailResult::kOk;
  ui::game::ScriptEventDispatch::Get().FireEvent(
      success ? ui::game::events::MAIL_SUCCESS
              : ui::game::events::MAIL_FAILED);
}

void HandleReceivedMailPacket(MailInteraction& mail,
                              const MailPacketSender& send,
                              const net::wotlk::WorldPacket& pkt) {
  const auto delay = mail_protocol::DecodeReceivedMailDelay(
      pkt.payload.data(), pkt.payload.size());
  if (!delay) {
    return;
  }
  mail.HandleReceivedMail(*delay);
  FlushMailProtocolUpdates(mail, send);
}

void HandleShowMailboxPacket(MailInteraction& mail,
                             const net::wotlk::WorldPacket& pkt) {
  const auto mailbox =
      mail_protocol::DecodeMailboxGuid(pkt.payload.data(), pkt.payload.size());
  if (!mailbox) {
    return;
  }
  mail.HandleShowMailbox(*mailbox);

  ui::game::ShowMailbox(mail);
}

void HandleNextMailTimePacket(MailInteraction& mail,
                              const MailPacketSender& send,
                              const net::wotlk::WorldPacket& pkt) {
  auto snapshot =
      mail_protocol::DecodeNextMailTime(pkt.payload.data(), pkt.payload.size());
  if (!snapshot) {
    return;
  }
  mail.HandleNextMailTime(std::move(*snapshot));
  FlushMailProtocolUpdates(mail, send);
}

}
