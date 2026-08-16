
#include "openwow/game/commerce/auctions/auction_interaction.h"

#include <algorithm>
#include <utility>

#include "openwow/game/query_cache.h"

namespace openwow::game {

namespace {

template <typename Container, typename Predicate>
bool EraseFirstIf(Container& container, Predicate&& predicate) {
  const auto it = std::find_if(container.begin(), container.end(),
                               std::forward<Predicate>(predicate));
  if (it == container.end()) {
    return false;
  }

  container.erase(it);
  return true;
}

bool EraseFirstAuctionRow(AuctionListResult& result,
                          const std::uint32_t auction_id) {
  return EraseFirstIf(result.auctions, [auction_id](const AuctionEntry& auction) {
    return auction.auction_id == auction_id;
  });
}

}

void AuctionInteraction::ApplyAuctionHello(const AuctionHelloPayload& hello) {
  ResetSessionData();
  auctioneer_guid_ = hello.auctioneer_guid;
  auction_house_id_ = hello.auction_house_id;
  deposit_rate_ = 0;
  enabled_ = true;
  bidder_list_request_enabled_ = true;
  owner_refresh_enabled_ = true;
  bidder_list_offset_ = 0;
}

void AuctionInteraction::HandleAuctionHello(AuctionHelloPayload hello) {
  if (!hello.enabled) {
    return;
  }
  ApplyAuctionHello(hello);
}

void AuctionInteraction::HandleAuctionListResult(AuctionListResult result) {
  search_results_ = std::move(result);
}

void AuctionInteraction::FinalizeSearchResultsWhenItemTemplatesReady(
    QueryCache& query_cache, std::function<void()> on_ready) {
  std::uint32_t generation = 0;
  {
    std::lock_guard lock(search_result_resolution_mutex_);
    ++search_result_resolution_generation_;
    generation = search_result_resolution_generation_;
    pending_search_result_item_templates_ = 0;
    pending_search_results_ready_callback_ = std::move(on_ready);
  }

  std::uint32_t pending_templates = 0;
  for (const auto& auction : search_results_.auctions) {
    if (query_cache.GetOrRequestItemTemplate(
            auction.item_entry,
            {.dedupe_callbacks = false,
             .callback = [this, generation](bool ) {
               OnSearchResultItemTemplateResolved(generation);
             }}) == nullptr) {
      ++pending_templates;
    }
  }

  std::function<void()> ready_callback;
  {
    std::lock_guard lock(search_result_resolution_mutex_);
    if (generation != search_result_resolution_generation_) {
      return;
    }

    pending_search_result_item_templates_ = pending_templates;
    if (pending_search_result_item_templates_ == 0) {
      ready_callback = std::move(pending_search_results_ready_callback_);
    }
  }

  if (ready_callback) {
    ready_callback();
  }
}

void AuctionInteraction::OnSearchResultItemTemplateResolved(
    std::uint32_t generation) {
  std::function<void()> ready_callback;
  {
    std::lock_guard lock(search_result_resolution_mutex_);
    if (generation != search_result_resolution_generation_ ||
        pending_search_result_item_templates_ == 0) {
      return;
    }

    --pending_search_result_item_templates_;
    if (pending_search_result_item_templates_ == 0) {
      ready_callback = std::move(pending_search_results_ready_callback_);
    }
  }

  if (ready_callback) {
    ready_callback();
  }
}

void AuctionInteraction::BeginOwnerAuctionRefresh() {
  std::lock_guard lock(owner_refresh_resolution_mutex_);
  ++owner_list_resolution_generation_;
  ++pending_sale_resolution_generation_;
  pending_owner_refresh_server_packets_ = 2;
  pending_owner_list_item_templates_ = 0;
  pending_pending_sale_item_templates_ = 0;
  pending_owner_refresh_ready_callback_ = {};
}

void AuctionInteraction::FinalizeOwnerListPacketWhenItemTemplatesReady(
    QueryCache& query_cache, std::function<void()> on_ready) {
  FinalizeOwnerRefreshPacketWhenItemTemplatesReady(
      OwnerRefreshPacketKind::kOwnerList, query_cache, std::move(on_ready));
}

void AuctionInteraction::FinalizePendingSalesPacketWhenItemTemplatesReady(
    QueryCache& query_cache, std::function<void()> on_ready) {
  FinalizeOwnerRefreshPacketWhenItemTemplatesReady(
      OwnerRefreshPacketKind::kPendingSales, query_cache, std::move(on_ready));
}

void AuctionInteraction::FinalizeOwnerRefreshPacketWhenItemTemplatesReady(
    OwnerRefreshPacketKind kind,
    QueryCache& query_cache,
    std::function<void()> on_ready) {
  std::uint32_t generation = 0;
  {
    std::lock_guard lock(owner_refresh_resolution_mutex_);
    generation = ++OwnerRefreshGeneration(kind);
    OwnerRefreshPendingTemplateCounter(kind) = 0;
    if (on_ready) {
      pending_owner_refresh_ready_callback_ = std::move(on_ready);
    }
  }

  std::uint32_t pending_templates = 0;
  if (kind == OwnerRefreshPacketKind::kOwnerList) {
    for (const auto& auction : owner_results_.auctions) {
      if (query_cache.GetOrRequestItemTemplate(
              auction.item_entry,
              {.dedupe_callbacks = false,
               .callback = [this, generation](bool ) {
                 OnOwnerRefreshItemTemplateResolved(
                     OwnerRefreshPacketKind::kOwnerList, generation);
               }}) == nullptr) {
        ++pending_templates;
      }
    }
  } else {
    for (const auto& pending_sale : pending_sales_) {
      if (query_cache.GetOrRequestItemTemplate(
              pending_sale.parsed_item_id,
              {.dedupe_callbacks = false,
               .callback = [this, generation](bool ) {
                 OnOwnerRefreshItemTemplateResolved(
                     OwnerRefreshPacketKind::kPendingSales, generation);
               }}) == nullptr) {
        ++pending_templates;
      }
    }
  }

  std::function<void()> ready_callback;
  {
    std::lock_guard lock(owner_refresh_resolution_mutex_);
    if (generation != OwnerRefreshGeneration(kind)) {
      return;
    }

    OwnerRefreshPendingTemplateCounter(kind) = pending_templates;

    if (pending_owner_refresh_server_packets_ > 0) {
      --pending_owner_refresh_server_packets_;
    }

    if (ShouldFinalizeOwnerRefreshLocked()) {
      ready_callback = std::move(pending_owner_refresh_ready_callback_);
    }
  }

  if (ready_callback) {
    ready_callback();
  }
}

void AuctionInteraction::OnOwnerRefreshItemTemplateResolved(
    OwnerRefreshPacketKind kind,
    std::uint32_t generation) {
  std::function<void()> ready_callback;
  {
    std::lock_guard lock(owner_refresh_resolution_mutex_);
    if (generation != OwnerRefreshGeneration(kind)) {
      return;
    }

    std::uint32_t& pending_templates = OwnerRefreshPendingTemplateCounter(kind);
    if (pending_templates == 0) {
      return;
    }

    --pending_templates;
    if (ShouldFinalizeOwnerRefreshLocked()) {
      ready_callback = std::move(pending_owner_refresh_ready_callback_);
    }
  }

  if (ready_callback) {
    ready_callback();
  }
}

bool AuctionInteraction::ShouldFinalizeOwnerRefreshLocked() const {
  if (pending_owner_list_item_templates_ != 0 ||
      pending_pending_sale_item_templates_ != 0) {
    return false;
  }

  return pending_owner_refresh_server_packets_ == 0;
}

std::uint32_t& AuctionInteraction::OwnerRefreshGeneration(
    OwnerRefreshPacketKind kind) {
  if (kind == OwnerRefreshPacketKind::kOwnerList) {
    return owner_list_resolution_generation_;
  }
  return pending_sale_resolution_generation_;
}

std::uint32_t& AuctionInteraction::OwnerRefreshPendingTemplateCounter(
    OwnerRefreshPacketKind kind) {
  if (kind == OwnerRefreshPacketKind::kOwnerList) {
    return pending_owner_list_item_templates_;
  }
  return pending_pending_sale_item_templates_;
}

void AuctionInteraction::HandleAuctionOwnerListResult(AuctionListResult result) {
  owner_results_ = std::move(result);
}

void AuctionInteraction::HandleAuctionBidderListResult(
    AuctionListResult result) {
  bidder_results_ = std::move(result);
  bidder_list_request_enabled_ =
      bidder_results_.auctions.size() != bidder_results_.total_count;
}

void AuctionInteraction::FinalizeBidderResultsWhenItemTemplatesReady(
    QueryCache& query_cache, std::function<void()> on_ready) {
  std::uint32_t generation = 0;
  {
    std::lock_guard lock(bidder_result_resolution_mutex_);
    ++bidder_result_resolution_generation_;
    generation = bidder_result_resolution_generation_;
    pending_bidder_result_item_templates_ = 0;
    pending_bidder_results_ready_callback_ = std::move(on_ready);
  }

  std::uint32_t pending_templates = 0;
  for (const auto& auction : bidder_results_.auctions) {
    if (query_cache.GetOrRequestItemTemplate(
            auction.item_entry,
            {.dedupe_callbacks = false,
             .callback = [this, generation](bool ) {
               OnBidderResultItemTemplateResolved(generation);
             }}) == nullptr) {
      ++pending_templates;
    }
  }

  std::function<void()> ready_callback;
  {
    std::lock_guard lock(bidder_result_resolution_mutex_);
    if (generation != bidder_result_resolution_generation_) {
      return;
    }

    pending_bidder_result_item_templates_ = pending_templates;
    if (pending_bidder_result_item_templates_ == 0) {
      ready_callback = std::move(pending_bidder_results_ready_callback_);
    }
  }

  if (ready_callback) {
    ready_callback();
  }
}

void AuctionInteraction::OnBidderResultItemTemplateResolved(
    std::uint32_t generation) {
  std::function<void()> ready_callback;
  {
    std::lock_guard lock(bidder_result_resolution_mutex_);
    if (generation != bidder_result_resolution_generation_ ||
        pending_bidder_result_item_templates_ == 0) {
      return;
    }

    --pending_bidder_result_item_templates_;
    if (pending_bidder_result_item_templates_ == 0) {
      ready_callback = std::move(pending_bidder_results_ready_callback_);
    }
  }

  if (ready_callback) {
    ready_callback();
  }
}

void AuctionInteraction::HandleAuctionCommandResult(
    AuctionCommandResult result) {
  last_command_ = result;
}

void AuctionInteraction::HandleAuctionBidderNotification(
    AuctionBidderNotification notification) {
  last_bidder_notification_ = notification;
  if (notification.current_bid != 0) {
    outbid_auctions_.push_back(
        {.auction_house_id = notification.auction_house_id,
         .auction_id = notification.auction_id});
  }
}

void AuctionInteraction::HandleAuctionOwnerNotification(
    AuctionOwnerNotification notification) {
  last_owner_notification_ = notification;
}

std::vector<std::uint32_t> AuctionInteraction::CollectOutbidAuctionIdsForCurrentHouse()
    const {
  std::vector<std::uint32_t> auction_ids;
  auction_ids.reserve(outbid_auctions_.size());
  for (const auto& entry : outbid_auctions_) {
    if (entry.auction_house_id == auction_house_id_) {
      auction_ids.push_back(entry.auction_id);
    }
  }
  return auction_ids;
}

void AuctionInteraction::MarkBidderListRequestSent(std::uint32_t list_from) {
  bidder_list_request_enabled_ = false;
  bidder_list_offset_ = list_from;
}

void AuctionInteraction::RemoveOutbidAuction(std::uint32_t auction_id) {
  const auto it = std::find_if(
      outbid_auctions_.begin(),
      outbid_auctions_.end(),
      [auction_id](const AuctionOutbidEntry& entry) {
        return entry.auction_id == auction_id;
      });
  if (it != outbid_auctions_.end()) {
    outbid_auctions_.erase(it);
  }
}

void AuctionInteraction::RemoveAuctionById(const std::uint32_t auction_id) {
  if (EraseFirstAuctionRow(search_results_, auction_id) &&
      search_results_.total_count > 0) {
    --search_results_.total_count;
  }

  (void)EraseFirstAuctionRow(owner_results_, auction_id);
  (void)EraseFirstAuctionRow(bidder_results_, auction_id);
  (void)EraseFirstIf(pending_sales_, [auction_id](const PendingSaleEntry& entry) {
    return entry.parsed_auction_id == auction_id;
  });
  RemoveOutbidAuction(auction_id);
}

void AuctionInteraction::Clear() {
  state_.Reset();
  ResetSessionData();
  auctioneer_guid_ = 0;
  auction_house_id_ = 0;
  deposit_rate_ = 0;
  enabled_ = false;
  bidder_list_request_enabled_ = false;
  owner_refresh_enabled_ = false;
  bidder_list_offset_ = 0;
  outbid_auctions_.clear();
}

void AuctionInteraction::ResetSessionData() {
  search_results_ = {};
  owner_results_ = {};
  bidder_results_ = {};
  last_command_.reset();
  last_bidder_notification_.reset();
  last_owner_notification_.reset();
  pending_sales_.clear();
  last_removed_notification_.reset();
  pending_bid_auction_id_ = 0;
  pending_bid_amount_ = 0;
  {
    std::lock_guard lock(search_result_resolution_mutex_);
    ++search_result_resolution_generation_;
    pending_search_result_item_templates_ = 0;
    pending_search_results_ready_callback_ = {};
  }
  {
    std::lock_guard lock(bidder_result_resolution_mutex_);
    ++bidder_result_resolution_generation_;
    pending_bidder_result_item_templates_ = 0;
    pending_bidder_results_ready_callback_ = {};
  }
  {
    std::lock_guard lock(owner_refresh_resolution_mutex_);
    ++owner_list_resolution_generation_;
    ++pending_sale_resolution_generation_;
    pending_owner_refresh_server_packets_ = 0;
    pending_owner_list_item_templates_ = 0;
    pending_pending_sale_item_templates_ = 0;
    pending_owner_refresh_ready_callback_ = {};
  }
}

void AuctionInteraction::HandleAuctionListPendingSales(
    std::vector<PendingSaleEntry> sales) {
  pending_sales_ = std::move(sales);
}

void AuctionInteraction::HandleAuctionRemovedNotification(
    AuctionRemovedNotification notification) {
  last_removed_notification_ = notification;
}

}
