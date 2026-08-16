#include "openwow/game/session/handlers/commerce/merchant_packets.h"
#include "openwow/game/commerce/merchants/adapters/protocol/merchant_result_packet_codec.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/console.h"
#include "openwow/game/commerce/auctions/auction_state.h"
#include "openwow/game/commerce/auctions/adapters/protocol/auction_packet_codec.h"
#include "openwow/game/commerce/trade/adapters/protocol/trade_packet_codec.h"
#include "openwow/game/commerce/mail/adapters/protocol/mail_packet_codec.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/gossip_manager.h"
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
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/petition_handler.h"
#include "openwow/game/interaction_sender.h"
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

constexpr std::uint32_t kMerchantTutorialId = 0x13u;

bool ReadGossipMessageNpcGuid(const net::wotlk::WorldPacket &pkt, std::uint64_t *npc_guid) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  return reader.ReadU64(*npc_guid);
}

void DisplayMerchantListFailureMessage(const ObjectManager& objects,
                                       const char* message) {
  if (message == nullptr || message[0] == '\0') {
    return;
  }

  ChatFrame_DisplayMessage(objects, message, ChatDisplayType::kSystem, nullptr, 0, nullptr, nullptr,
                           nullptr, 0, 0, 0, 0, 0, nullptr);
}

const char* ResolveMerchantListFailureMessage(const VendorListResult result) {
  switch (result) {
    case VendorListResult::kDisliked:
      return "I don't think he likes you very much";
    case VendorListResult::kTooFarAway:
      return "You are too far away";
    case VendorListResult::kVendorDead:
      return "Vendor is dead";
    case VendorListResult::kPlayerDead:
      return "You can't shop while dead.";
    default:
      return nullptr;
  }
}

std::string ResolveMerchantMessage(
    const CGUnit_C* unit, const char* key) {
  if (key == nullptr || key[0] == '\0') {
    return {};
  }

  auto& localization = Localization::Get();
  auto message = localization.GetString(key, key);
  if (unit != nullptr && unit->State().GetGender() == 1) {
    const auto feminine_key = std::string(key) + "_FEMALE";
    if (localization.HasString(feminine_key)) {
      message = localization.GetString(feminine_key, message);
    }
  }
  return message;
}

void DispatchSellItemError(ObjectManager& objects,
                           const SellItemResult &result) {
  const char *global_string_key = GetSellResultGlobalStringKey(result.result);
  if (global_string_key == nullptr) {
    return;
  }

  const auto *vendor = objects.GetUnit(ObjectGuid(result.vendor_guid));
  if (vendor == nullptr) {
    return;
  }

  const std::string message =
      ResolveMerchantMessage(vendor, global_string_key);
  if (message.empty()) {
    return;
  }

  ui::UIErrorManager::Get().AddErrorMessage(message);
  ui::game::ScriptEventDispatch::Get().FireUiErrorMessage(message);
}

}

void HandleGossipMessagePacket(
    GossipManager& gossip, QueryCache& queries, InteractionSender& sender,
    const std::function<void(std::uint64_t)>& close_interaction,
    const std::function<bool()>& prepare_gossip_text,
    const net::wotlk::WorldPacket& pkt) {
  std::uint64_t npc_guid = 0;
  if (ReadGossipMessageNpcGuid(pkt, &npc_guid)) {
    if (npc_guid != 0) {
      ui::game::SetNpcInteractionTarget(ObjectGuid(npc_guid));
    } else if (gossip.has_gossip() && close_interaction) {
      close_interaction(gossip.gossip().npc_guid.GetRawValue());
    }
  }

  if (!gossip.HandleGossipMessage(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "interaction reject malformed SMSG_GOSSIP_MESSAGE bytes=" +
            std::to_string(pkt.payload.size()));
    return;
  }

  const auto& dialog = gossip.gossip();
  if (queries.HasNpcText(dialog.title_text_id)) {
    if (prepare_gossip_text && prepare_gossip_text()) {
      ui::game::ScriptEventDispatch::Get().FireGossipShow();
    }
    return;
  }

  if (!queries.IsNpcTextQueryPending(dialog.title_text_id)) {
    queries.MarkNpcTextQueryPending(dialog.title_text_id);
    sender.SendNpcTextQuery(dialog.npc_guid.GetRawValue(), dialog.title_text_id);
  }
}

void HandleNpcTextUpdatePacket(
    GossipManager& gossip, QueryCache& queries,
    const std::function<bool()>& prepare_gossip_text,
    const net::wotlk::WorldPacket& pkt) {
  PacketReader packet_reader(pkt.payload.data(), pkt.payload.size());
  std::uint32_t response_text_id = 0;
  if (!packet_reader.ReadU32(response_text_id)) {
    return;
  }

  if (!queries.HandleNpcTextUpdate(pkt.payload.data(), pkt.payload.size()) ||
      !gossip.has_gossip()) {
    return;
  }

  const auto text_id = gossip.gossip().title_text_id;
  if (response_text_id != text_id || !queries.HasNpcText(text_id)) {
    return;
  }

  if (prepare_gossip_text && prepare_gossip_text()) {
    ui::game::ScriptEventDispatch::Get().FireGossipShow();
  }
}

void HandleTrainerListPacket(
    GossipManager& gossip, const std::function<void()>& update_greeting,
    const net::wotlk::WorldPacket& pkt) {
  if (!gossip.HandleTrainerList(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (update_greeting) {
    update_greeting();
  }
  ui::game::ScriptEventDispatch::Get().FireTrainerShow();
}

void HandleMerchantListPacket(ObjectManager& objects, GossipManager& gossip,
                              QueryCache& queries,
                              const net::wotlk::WorldPacket& pkt) {
  if (!gossip.HandleListInventory(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  switch (gossip.merchant().last_list_result()) {
    case VendorListResult::kItems:
    case VendorListResult::kNoInventory: {
      queries.CancelItemTemplateCallbacks(
          MerchantInteraction::ItemInfoRefreshCallbackKey());
      gossip.merchant().ResetItemInfoRefresh();
      TutorialSystem::Instance().TriggerTutorial(kMerchantTutorialId);

      if (gossip.merchant().active()) {
        ui::game::SetNpcInteractionTarget(gossip.merchant().snapshot().vendor_guid);
      }

      ui::game::ScriptEventDispatch::Get().FireMerchantShow();
      return;
    }
    case VendorListResult::kDisliked:
    case VendorListResult::kTooFarAway:
    case VendorListResult::kVendorDead:
    case VendorListResult::kPlayerDead:
      DisplayMerchantListFailureMessage(objects,
          ResolveMerchantListFailureMessage(gossip.merchant().last_list_result()));
      return;
    case VendorListResult::kUnknownFailure:
      return;
  }
}

void HandleMerchantBuyPacket(MerchantInteraction& merchant,
                             const net::wotlk::WorldPacket& pkt) {
  const auto buy = merchant_protocol::DecodeBuyItem(
      pkt.payload.data(), pkt.payload.size());
  if (!buy.has_value()) {
    return;
  }

  if (merchant.UpdateItemCount(
          buy->vendor_guid, buy->vendor_slot, buy->new_count)) {
    ui::game::ScriptEventDispatch::Get().FireMerchantUpdate();
  }
}

void HandleMerchantSellPacket(ObjectManager& objects,
                              const net::wotlk::WorldPacket& pkt) {
  const auto result = merchant_protocol::DecodeSellItem(
      pkt.payload.data(), pkt.payload.size());
  if (!result.has_value()) {
    return;
  }
  if (result->result != SellResult::kOk) {
    DispatchSellItemError(objects, *result);
    if (result->item_guid != 0) {
      openwow::ui::game::GameUI_OnMouseoverUnitLeave(result->item_guid);
    }
  }
}

void HandleMerchantBuyFailurePacket(
    MerchantInteraction& merchant,
    const net::wotlk::WorldPacket& pkt) {
  const auto fail = merchant_protocol::DecodeBuyFailed(
      pkt.payload.data(), pkt.payload.size());
  if (!fail.has_value()) {
    return;
  }

  if (fail->reason == BuyFailReason::kAlreadySold) {
    if (merchant.UpdateItemCount(
            fail->vendor_guid, fail->item_id, 0)) {
      ui::game::ScriptEventDispatch::Get().FireMerchantUpdate();
    }
  }

  const int msg_index = GetBuyFailSystemMessageIndex(fail->reason);
  if (msg_index >= 0) {
    ui::game::DisplaySystemMessage(msg_index);
  }
}

void HandleTrainerBuySucceededPacket(
    PetitionHandler& petition, const net::wotlk::WorldPacket& pkt) {
  petition.HandleTrainerBuySucceeded(pkt.payload.data(), pkt.payload.size());
}

void HandleTrainerBuyFailedPacket(
    PetitionHandler& petition, const net::wotlk::WorldPacket& pkt) {
  if (!petition.HandleTrainerBuyFailed(pkt.payload.data(), pkt.payload.size()))
    return;

  const auto& result = petition.last_trainer_buy();
  switch (static_cast<TrainerBuyFailReason>(result.fail_reason)) {
    case TrainerBuyFailReason::kUnavailable:
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "Trainer service unavailable: " + std::to_string(result.spell_id));
      break;
    case TrainerBuyFailReason::kNotEnoughMoney:
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "Insufficient money for trainer service: " +
              std::to_string(result.spell_id));
      break;
    case TrainerBuyFailReason::kNotEnoughSkill:
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "Insufficient skill for trainer service: " +
              std::to_string(result.spell_id));
      break;
    default:
      break;
  }
}

}
