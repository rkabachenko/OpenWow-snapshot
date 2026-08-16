#include "openwow/game/session/handlers/commerce/auction_packets.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/console.h"
#include "openwow/game/commerce/auctions/auction_state.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/commerce/auctions/adapters/protocol/auction_packet_codec.h"
#include "openwow/game/commerce/trade/adapters/protocol/trade_packet_codec.h"
#include "openwow/game/commerce/mail/adapters/protocol/mail_packet_codec.h"
#include "openwow/game/session/handlers/commerce/mail_packets.h"
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
#include "openwow/game/interaction_sender.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
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

constexpr int kAuctionOutbidSystemMessageId = 404;
constexpr int kAuctionWonSystemMessageId = 405;
constexpr int kAuctionSoldSystemMessageId = 406;
constexpr int kAuctionExpiredSystemMessageId = 407;
constexpr int kAuctionRemovedSystemMessageId = 408;

enum class AuctionNotificationDisplayOrigin : std::uint8_t {
  kImmediate,
  kDeferredItemTemplate,
};

constexpr std::uint8_t AuctionSelectionListToRefreshMask(
    const AuctionSelectionList list) {
  switch (list) {
    case AuctionSelectionList::kOwner:
      return 1u << 1;
    case AuctionSelectionList::kBidder:
      return 1u << 2;
    case AuctionSelectionList::kList:
    default:
      return 1u << 0;
  }
}

void PresentAuctionListChanged(const AuctionSelectionList list) {
  auto& events = ui::game::ScriptEventDispatch::Get();
  switch (list) {
    case AuctionSelectionList::kOwner:
      events.FireAuctionOwnedListUpdate();
      break;
    case AuctionSelectionList::kBidder:
      events.FireAuctionBidderListUpdate();
      break;
    case AuctionSelectionList::kList:
      events.FireAuctionItemListUpdate();
      break;
  }
}

void PresentAuctionListRefreshMask(const std::uint8_t refresh_mask) {
  if ((refresh_mask & (1u << 0)) != 0) {
    PresentAuctionListChanged(AuctionSelectionList::kList);
  }
  if ((refresh_mask & (1u << 1)) != 0) {
    PresentAuctionListChanged(AuctionSelectionList::kOwner);
  }
  if ((refresh_mask & (1u << 2)) != 0) {
    PresentAuctionListChanged(AuctionSelectionList::kBidder);
  }
}

void ReleaseAuctionSellItems(
    const std::vector<std::uint64_t>& item_guids,
    const bool failed = false) {
  for (const auto guid : item_guids) {
    ui::game::GameUI_OnMouseoverUnitLeave(guid);
  }
  auto& events = ui::game::ScriptEventDispatch::Get();
  if (!item_guids.empty()) {
    events.FireGlobalEvent(ui::game::events::NEW_AUCTION_UPDATE);
  }
  if (failed) {
    events.FireAuctionMultiSellFailure();
  }
}

std::optional<int> ResolveAuctionCommandErrorSystemMessageId(
    const AuctionCommandResult &result) {
  switch (result.error_code) {
  case 0:
    return std::nullopt;
  case 1:
    return InventoryResultCodeToSystemMessageId(result.bid_error);
  case 3:
    return 40;
  case 4:
    return 25;
  case 5:
    return 398;
  case 7:
    return 397;
  case 10:
    return 396;
  case 13:
    return 488;
  default:
    return 395;
  }
}

std::uint32_t ResolveAuctionHouseDepositRate(
    const data::dbc::DbcLoader* dbc,
    const std::uint32_t auction_house_id) {
  if (dbc != nullptr) {
    if (const auto *entry = dbc->auction_house().LookupEntry(auction_house_id)) {
      return entry->deposit_rate;
    }
  }

  return 0;
}

void PresentAuctionRemovalEvents(const AuctionRemovalMask removal_mask) {
  if ((static_cast<std::uint8_t>(removal_mask) &
       static_cast<std::uint8_t>(AuctionRemovalMask::kList)) != 0) {
    PresentAuctionListChanged(AuctionSelectionList::kList);
  }
  if ((static_cast<std::uint8_t>(removal_mask) &
       static_cast<std::uint8_t>(AuctionRemovalMask::kOwner)) != 0) {
    PresentAuctionListChanged(AuctionSelectionList::kOwner);
  }
  if ((static_cast<std::uint8_t>(removal_mask) &
       static_cast<std::uint8_t>(AuctionRemovalMask::kBidder)) != 0) {
    PresentAuctionListChanged(AuctionSelectionList::kBidder);
  }
}

void RemoveAuctionFromUiState(AuctionInteraction& auction,
                              InteractionSender& interaction,
                              const std::uint32_t auction_id) {
  auction.RemoveAuctionById(auction_id);
  auto &auction_state = auction.state();
  const auto removal_mask = auction_state.RemoveAuction(auction_id);
  PresentAuctionRemovalEvents(removal_mask);

  if ((static_cast<std::uint8_t>(removal_mask) &
       static_cast<std::uint8_t>(AuctionRemovalMask::kBidder)) != 0) {
    constexpr std::uint32_t kOwnerQueryType = 2;
    if (auction_state.CanSendAuctionQuery(kOwnerQueryType)) {
      interaction.SendAuctionListPendingSales(auction.auctioneer_guid());
      auction_state.MarkQuerySent(kOwnerQueryType, 1);
    }
  }
}

std::string BuildAuctionNotificationItemName(
                                             const data::dbc::DbcLoader* dbc,
                                             const ItemTemplate &item_template,
                                             const std::int32_t random_property_id) {
  return FormatItemDisplayNameWithRandomProperty(Localization::Get(),
                                                 dbc,
                                                 item_template.name,
                                                 random_property_id);
}

void DispatchAuctionNotificationLcdMessage(
    const std::string &item_name,
    const int system_message_id,
    const AuctionNotificationDisplayOrigin origin) {
  switch (system_message_id) {
  case kAuctionOutbidSystemMessageId:
    if (origin == AuctionNotificationDisplayOrigin::kImmediate) {
      LCD_OnAuctionOutbidNotification(item_name.c_str());
    }
    return;
  case kAuctionWonSystemMessageId:
    LCD_OnAuctionWonNotification(item_name.c_str());
    return;
  case kAuctionSoldSystemMessageId:
    LCD_OnAuctionSoldNotification(item_name.c_str());
    return;
  case kAuctionExpiredSystemMessageId:
    LCD_OnAuctionExpiredNotification(item_name.c_str());
    return;
  default:
    return;
  }
}

void DisplayAuctionNotificationItemMessage(
    const std::string &item_name,
    const int system_message_id,
    const AuctionNotificationDisplayOrigin origin) {
  ui::game::DisplaySystemMessage(system_message_id, item_name.c_str());
  DispatchAuctionNotificationLcdMessage(item_name, system_message_id, origin);
}

void ResolveAuctionNotificationItemMessage(
    QueryCache& queries,
    const data::dbc::DbcLoader* dbc,
    const std::uint32_t item_template_entry,
    const std::int32_t random_property_id,
    const int system_message_id) {
  const auto *item_template = queries.GetOrRequestItemTemplate(
      item_template_entry,
      QueryCache::QueryRequestOptions{
          .dedupe_callbacks = false,
          .callback = [&queries, dbc, item_template_entry, random_property_id,
                       system_message_id](const bool success) {
            if (!success) {
              return;
            }

            const auto *resolved_template =
                queries.GetItemTemplate(item_template_entry);
            if (resolved_template == nullptr) {
              return;
            }

            DisplayAuctionNotificationItemMessage(
                BuildAuctionNotificationItemName(dbc, *resolved_template,
                                                 random_property_id),
                system_message_id,
                AuctionNotificationDisplayOrigin::kDeferredItemTemplate);
          }});
  if (item_template == nullptr) {
    return;
  }

  DisplayAuctionNotificationItemMessage(
      BuildAuctionNotificationItemName(dbc, *item_template,
                                       random_property_id),
      system_message_id,
      AuctionNotificationDisplayOrigin::kImmediate);
}

std::uint32_t ClassifyAuctionBrowseTimeLeft(std::uint32_t remaining_ms) {
  if (remaining_ms < 1800000u) {
    return 1;
  }
  if (remaining_ms < 7200000u) {
    return 2;
  }
  if (remaining_ms < 43200000u) {
    return 3;
  }
  return 4;
}

std::vector<AuctionItem> BuildAuctionStateResults(const AuctionListResult &result,
                                                   const QueryCache &query_cache) {
  std::vector<AuctionItem> items;
  items.reserve(result.auctions.size());

  for (const auto &auction : result.auctions) {
    AuctionItem item;
    item.auction_id = auction.auction_id;
    item.item_entry = auction.item_entry;
    item.count = auction.stack_count;
    item.remaining_time_ms = auction.time_left_ms;
    item.owner_guid = auction.owner_guid;
    item.start_bid = auction.start_bid;
    item.minimum_increment = auction.minimum_outbid;
    item.buyout = auction.buyout;
    item.current_bid = auction.current_bid;
    item.time_left = ClassifyAuctionBrowseTimeLeft(auction.time_left_ms);
    item.expiration_tick_ms = auction.expiration_tick_ms;
    item.sale_status = auction.pending_sale_flag;
    item.bidder_guid = auction.bidder_guid;
    item.random_property = auction.random_property_id;
    item.random_suffix = auction.suffix_factor;
    item.enchant_id = auction.enchants[0].id;
    item.gem_enchant_ids = {
        auction.enchants[1].id,
        auction.enchants[2].id,
        auction.enchants[3].id,
    };

    if (const auto *tmpl = query_cache.GetItemTemplate(auction.item_entry)) {
      item.item_name = tmpl->name;
      item.required_level = tmpl->required_level;
      item.has_item_template = true;
      item.quality = static_cast<std::uint8_t>(tmpl->quality);
    }
    if (const auto *owner = query_cache.GetPlayerName(auction.owner_guid)) {
      item.owner_name = owner->name;
    }

    items.push_back(std::move(item));
  }

  return items;
}

void SyncAuctionSearchResultsToLua(AuctionInteraction &auction, const QueryCache &query_cache) {
  auto items = BuildAuctionStateResults(auction.search_results(), query_cache);
  auction.state().SetSearchResults(items, auction.search_results().total_count);
}

std::uint32_t PendingSaleDaysToMilliseconds(float days_remaining) {
  if (days_remaining <= 0.0f) {
    return 0;
  }

  const double remaining_ms = std::trunc(static_cast<double>(days_remaining) * 86400000.0);
  if (!std::isfinite(remaining_ms) || remaining_ms <= 0.0) {
    return 0;
  }

  return static_cast<std::uint32_t>(std::fmod(remaining_ms, 4294967296.0));
}

void SyncAuctionOwnerResultsToLua(AuctionInteraction &auction, const QueryCache &query_cache) {
  auto items = BuildAuctionStateResults(auction.owner_results(), query_cache);
  items.reserve(items.size() + auction.pending_sales().size());

  const auto active_player_guid = CGObject_C::GetActivePlayerGuid().GetRawValue();
  const auto now_tick = core::GameClock::GetTickCount32();
  for (const auto &pending_sale : auction.pending_sales()) {
    AuctionItem item;
    item.auction_id = pending_sale.parsed_auction_id;
    item.item_entry = pending_sale.parsed_item_id;
    item.count = pending_sale.parsed_stack_count;
    item.owner_guid = active_player_guid;
    item.buyout = pending_sale.buyout;
    item.current_bid = pending_sale.current_bid;
    item.remaining_time_ms = PendingSaleDaysToMilliseconds(pending_sale.time_remaining_days);
    item.time_left = ClassifyAuctionBrowseTimeLeft(item.remaining_time_ms);
    item.expiration_tick_ms = now_tick + item.remaining_time_ms;
    item.sale_status = 1;
    item.bidder_guid = pending_sale.bidder_guid;

    if (const auto *tmpl = query_cache.GetItemTemplate(item.item_entry)) {
      item.item_name = tmpl->name;
      item.required_level = tmpl->required_level;
      item.has_item_template = true;
      item.quality = static_cast<std::uint8_t>(tmpl->quality);
    }
    if (const auto *owner = query_cache.GetPlayerName(active_player_guid)) {
      item.owner_name = owner->name;
    }

    items.push_back(std::move(item));
  }

  auction.state().SetOwnAuctions(items);
}

void SyncAuctionBidderResultsToLua(AuctionInteraction &auction, const QueryCache &query_cache) {
  auto items = BuildAuctionStateResults(auction.bidder_results(), query_cache);
  auction.state().SetMyBids(items, auction.bidder_results().total_count);
}

}

AuctionPacketHandler::AuctionPacketHandler(
    AuctionInteraction& auction, MailInteraction& mail, QueryCache& queries,
    InteractionSender& interaction, SendPacket send)
    : auction_(auction),
      mail_(mail),
      queries_(queries),
      interaction_(interaction),
      send_(std::move(send)) {}

void AuctionPacketHandler::SetDbcLoader(const data::dbc::DbcLoader* dbc) {
  dbc_ = dbc;
}

bool AuctionPacketHandler::TrySendOwnerRefresh() {
  if (auction_.auctioneer_guid() == 0 || !auction_.owner_refresh_enabled()) {
    return false;
  }

  auto &auction_state = auction_.state();
  if (!auction_state.CanSendAuctionQuery(2)) {
    return false;
  }

  auction_.BeginOwnerAuctionRefresh();
  interaction_.SendAuctionListOwnerItems(auction_.auctioneer_guid());
  auction_state.MarkQuerySent(2, 2);
  auction_.MarkOwnerRefreshSent();
  return true;
}

void AuctionPacketHandler::RequestListRefreshOnNameResolve(
    const std::uint64_t guid, const AuctionSelectionList list) {
  if (guid == 0) {
    return;
  }

  pending_name_queries_[guid] |= AuctionSelectionListToRefreshMask(list);
  (void)queries_.RequestNameQuery(guid);
}

void AuctionPacketHandler::ResolvePendingNameQuery(
    const std::uint64_t guid, const bool name_cache_updated) {
  if (guid == 0) {
    return;
  }

  const auto it = pending_name_queries_.find(guid);
  if (it == pending_name_queries_.end()) {
    return;
  }

  const auto refresh_mask = it->second;
  pending_name_queries_.erase(it);
  if (!name_cache_updated) {
    return;
  }

  PresentAuctionListRefreshMask(refresh_mask);
}

void AuctionPacketHandler::Clear() {
  pending_name_queries_.clear();
}

void AuctionPacketHandler::HandleHello(const net::wotlk::WorldPacket &pkt) {
  auto hello =
      auction_protocol::DecodeHello(pkt.payload.data(), pkt.payload.size());
  if (!hello) {

    ui::game::ScriptEventDispatch::Get().FireGlobalEvent(
        ui::game::events::AUCTION_HOUSE_DISABLED);
    return;
  }

  if (!hello->enabled) {
    ui::game::ScriptEventDispatch::Get().FireGlobalEvent(
        ui::game::events::AUCTION_HOUSE_DISABLED);
    return;
  }

  auction_.HandleAuctionHello(*hello);
  auction_.SetDepositRate(
      ResolveAuctionHouseDepositRate(dbc_, hello->auction_house_id));
  ReleaseAuctionSellItems(auction_.state().OpenAuctionHouse());
  ui::game::ScriptEventDispatch::Get().FireAuctionHouseShow();
}

void AuctionPacketHandler::HandleListResult(const net::wotlk::WorldPacket &pkt) {
  auto result =
      auction_protocol::DecodeList(pkt.payload.data(), pkt.payload.size());
  if (!result) {
    return;
  }
  auction_.HandleAuctionListResult(std::move(*result));

  auto &auction_state = auction_.state();
  auction_state.SetQueryCooldown(auction_.search_results().search_delay);
  auction_state.MarkQueryComplete(0);

  auction_.FinalizeSearchResultsWhenItemTemplatesReady(queries_, [this]() {
    SyncAuctionSearchResultsToLua(auction_, queries_);
    PresentAuctionListChanged(AuctionSelectionList::kList);
  });
}

void AuctionPacketHandler::HandleOwnerListResult(
    const net::wotlk::WorldPacket &pkt) {
  auto result =
      auction_protocol::DecodeList(pkt.payload.data(), pkt.payload.size());
  if (!result) {
    return;
  }
  auction_.HandleAuctionOwnerListResult(std::move(*result));

  auto &auction_state = auction_.state();
  auction_state.SetQueryCooldown(auction_.owner_results().search_delay);
  auction_state.MarkQueryPacketReceived(2);

  auction_.FinalizeOwnerListPacketWhenItemTemplatesReady(queries_, [this]() {
    SyncAuctionOwnerResultsToLua(auction_, queries_);
    PresentAuctionListChanged(AuctionSelectionList::kOwner);
  });
}

void AuctionPacketHandler::HandleBidderListResult(
    const net::wotlk::WorldPacket &pkt) {
  auto result =
      auction_protocol::DecodeList(pkt.payload.data(), pkt.payload.size());
  if (!result) {
    return;
  }
  auction_.HandleAuctionBidderListResult(std::move(*result));

  auto &auction_state = auction_.state();
  auction_state.SetQueryCooldown(auction_.bidder_results().search_delay);
  auction_state.MarkQueryComplete(1);

  auction_.FinalizeBidderResultsWhenItemTemplatesReady(queries_, [this]() {
    SyncAuctionBidderResultsToLua(auction_, queries_);
    PresentAuctionListChanged(AuctionSelectionList::kBidder);
  });
}

void AuctionPacketHandler::HandleCommandResult(
    const net::wotlk::WorldPacket &pkt) {
  auto result = auction_protocol::DecodeCommandResult(
      pkt.payload.data(), pkt.payload.size());
  if (!result) {
    return;
  }
  auction_.HandleAuctionCommandResult(std::move(*result));

  auto &auction_state = auction_.state();
  const auto &command = auction_.last_command();
  if (!command.has_value()) {
    return;
  }

  const bool active_multi_sell =
      command->action == AuctionAction::kSellItem &&
      auction_state.HasActiveMultiSell();
  if (active_multi_sell) {
    if (command->error_code != 0) {
      ReleaseAuctionSellItems(auction_state.AbortMultiSell(), true);
    } else {
      ui::game::ScriptEventDispatch::Get().FireAuctionMultiSellUpdate(
          static_cast<int>(auction_state.GetMultiSellCompletedStacks()),
          static_cast<int>(auction_state.GetMultiSellTotalStacks()));

      if (const auto request = auction_state.PrepareNextMultiSellRequest();
          request.has_value()) {
        if (send_) {
          send_(auction_protocol::EncodeSellItem(
              request->auctioneer_guid, request->items, request->min_bid,
              request->buyout, request->duration_minutes));
        }
      } else {
        ReleaseAuctionSellItems(auction_state.CompleteMultiSell());
      }
    }
  }

  if (command->error_code != 0) {
    if (command->error_code == 5 && auction_.auctioneer_guid() != 0) {
      const auto modified = auction_state.ApplyHigherBidResultToLists(
          command->auction_id, command->competing_bid,
          command->competing_bidder_guid, command->minimum_increment);
      if ((modified & static_cast<std::uint8_t>(AuctionRemovalMask::kList)) != 0 &&
          !auction_state.IsQueryPending(0)) {
        PresentAuctionListChanged(AuctionSelectionList::kList);
      }
      if ((modified & static_cast<std::uint8_t>(AuctionRemovalMask::kBidder)) != 0 &&
          !auction_state.IsQueryPending(1)) {
        PresentAuctionListChanged(AuctionSelectionList::kBidder);
      }
    }
    if (const auto message_id = ResolveAuctionCommandErrorSystemMessageId(*command);
        message_id.has_value()) {
      ui::game::DisplaySystemMessage(*message_id);
    }
    return;
  }

  if (command->action == AuctionAction::kSellItem) {
    ui::game::DisplaySystemMessage(402);
    auction_.EnableOwnerRefresh();
    TrySendOwnerRefresh();
    return;
  }

  if (command->action == AuctionAction::kCancel) {
    ui::game::DisplaySystemMessage(403);
    mail_.ApplyPendingMailDelay(0.0f);
    FlushMailProtocolUpdates(
        mail_, send_);
    RemoveAuctionFromUiState(auction_, interaction_, command->auction_id);
    return;
  }

  if (command->action != AuctionAction::kPlaceBid) {
    return;
  }

  ui::game::DisplaySystemMessage(409);
  auction_.RemoveOutbidAuction(command->auction_id);
  auction_.EnableBidderListRequest();
  interaction_.SendAuctionListBidderItems(
      auction_.auctioneer_guid(), auction_.bidder_list_offset(),
      auction_.CollectOutbidAuctionIdsForCurrentHouse());
  auction_state.MarkQuerySent(1);
  auction_.MarkBidderListRequestSent(auction_.bidder_list_offset());

  const auto bidder_guid = CGObject_C::GetActivePlayerGuid().GetRawValue();
  if (auction_.auctioneer_guid() != 0 &&
      auction_state.ApplySuccessfulBidToBrowse(
          command->auction_id,
          auction_.PendingBidAmountFor(command->auction_id), bidder_guid,
          command->minimum_increment) &&
      !auction_state.IsQueryPending(0)) {
    PresentAuctionListChanged(AuctionSelectionList::kList);
  }
  if (send_) {
    send_(net::wotlk::PacketSender::BuildEmote(3));
  }
}

void AuctionPacketHandler::HandleBidderNotification(
    const net::wotlk::WorldPacket &pkt) {
  auto decoded_notification = auction_protocol::DecodeBidderNotification(
      pkt.payload.data(), pkt.payload.size());
  if (!decoded_notification) {
    return;
  }
  auction_.HandleAuctionBidderNotification(std::move(*decoded_notification));

  if (const auto &notification = auction_.last_bidder_notification();
      notification.has_value()) {
    ResolveAuctionNotificationItemMessage(
        queries_, dbc_, notification->item_template,
        notification->random_property_id,
        notification->current_bid != 0 ? kAuctionOutbidSystemMessageId
                                       : kAuctionWonSystemMessageId);
    if (notification->current_bid == 0) {
      RemoveAuctionFromUiState(
          auction_, interaction_, notification->auction_id);
    }

    if (notification->current_bid != 0 &&
        auction_.auction_house_id() == notification->auction_house_id) {
      auto &system = auction_.state();
      const auto modified = system.ApplyBidderNotificationToLists(
          notification->auction_id, notification->current_bid,
          notification->bidder_guid, notification->time_left_ms);
      if ((modified & static_cast<std::uint8_t>(AuctionRemovalMask::kBidder)) &&
          !system.IsQueryPending(1)) {
        PresentAuctionListChanged(AuctionSelectionList::kBidder);
      }
      if ((modified & static_cast<std::uint8_t>(AuctionRemovalMask::kList)) &&
          !system.IsQueryPending(0)) {
        PresentAuctionListChanged(AuctionSelectionList::kList);
      }
    }
  }

  mail_.ApplyPendingMailDelay(0.0f);
  FlushMailProtocolUpdates(
      mail_, send_);
}

void AuctionPacketHandler::HandleOwnerNotification(
    const net::wotlk::WorldPacket &pkt) {
  auto decoded_notification = auction_protocol::DecodeOwnerNotification(
      pkt.payload.data(), pkt.payload.size());
  if (!decoded_notification) {
    return;
  }
  auction_.HandleAuctionOwnerNotification(
      std::move(*decoded_notification));

  const auto &state_notification = auction_.last_owner_notification();
  if (!state_notification.has_value()) {
    return;
  }

  if (state_notification->bidder_guid != 0) {
    if (auction_.auctioneer_guid() == 0) {
      return;
    }

    auto &system = auction_.state();
    const auto modified = system.ApplyOwnerNotificationToLists(
        state_notification->auction_id, state_notification->bid,
        state_notification->bidder_guid,
        state_notification->time_left_ms);
    if ((modified & static_cast<std::uint8_t>(AuctionRemovalMask::kOwner)) &&
        !system.IsQueryPending(2)) {
      PresentAuctionListChanged(AuctionSelectionList::kOwner);
    }
    if ((modified & static_cast<std::uint8_t>(AuctionRemovalMask::kList)) &&
        !system.IsQueryPending(0)) {
      PresentAuctionListChanged(AuctionSelectionList::kList);
    }
    return;
  }

  ResolveAuctionNotificationItemMessage(
      queries_, dbc_, state_notification->item_template,
      state_notification->random_property_id,
      state_notification->bid != 0 ? kAuctionSoldSystemMessageId
                                   : kAuctionExpiredSystemMessageId);
  mail_.ApplyPendingMailDelay(
      state_notification->bid != 0 ? state_notification->money : 0.0f);
  FlushMailProtocolUpdates(
      mail_, send_);
  RemoveAuctionFromUiState(
      auction_, interaction_, state_notification->auction_id);
}

void AuctionPacketHandler::HandleListPendingSales(
    const net::wotlk::WorldPacket &pkt) {
  auto sales =
      auction_protocol::DecodePendingSales(pkt.payload.data(), pkt.payload.size());
  if (!sales) {
    return;
  }
  auction_.HandleAuctionListPendingSales(std::move(*sales));

  auction_.state().MarkQueryPacketReceived(2);

  auction_.FinalizePendingSalesPacketWhenItemTemplatesReady(queries_, [this]() {
    SyncAuctionOwnerResultsToLua(auction_, queries_);
    PresentAuctionListChanged(AuctionSelectionList::kOwner);
  });
}

void AuctionPacketHandler::HandleRemovedNotification(
    const net::wotlk::WorldPacket &pkt) {
  auto decoded_notification = auction_protocol::DecodeRemovedNotification(
      pkt.payload.data(), pkt.payload.size());
  if (!decoded_notification) {
    return;
  }
  auction_.HandleAuctionRemovedNotification(std::move(*decoded_notification));

  if (const auto &notification = auction_.last_removed_notification();
      notification.has_value()) {
    ResolveAuctionNotificationItemMessage(
        queries_, dbc_, notification->item_template,
        static_cast<std::int32_t>(notification->random_property_id),
        kAuctionRemovedSystemMessageId);
    RemoveAuctionFromUiState(
        auction_, interaction_, notification->auction_id);
  }
}

}
