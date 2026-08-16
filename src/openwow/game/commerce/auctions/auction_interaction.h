
#pragma once

#include "openwow/game/commerce/auctions/auction_state.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

class QueryCache;

namespace detail {

inline int StrCmpNoCase(const char* a, const char* b) {
  while (*a && *b) {
    int d = std::tolower(static_cast<unsigned char>(*a)) -
            std::tolower(static_cast<unsigned char>(*b));
    if (d != 0) return d;
    ++a; ++b;
  }
  return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
}

inline constexpr int kMaxAuctionItems = 160;
inline constexpr int kAuctionEnchantSlots = 7;
inline constexpr int kAuctionSearchDelay = 300;

enum class AuctionAction : std::uint32_t {
  kSellItem = 0,
  kCancel = 1,
  kPlaceBid = 2,
};

enum class AuctionSortColumnId : std::uint32_t {
  kLevel = 0,
  kQuality = 1,
  kBuyoutThenBid = 2,
  kDuration = 3,
  kStatus = 4,
  kName = 5,
  kMinBidBuyout = 6,
  kSeller = 7,
  kBid = 8,
  kQuantity = 9,
  kBuyout = 10,
  kCount = 11,
};

enum class AuctionListType : std::uint32_t {
  kList = 0,
  kBidder = 1,
  kOwner = 2,
};

inline const char* GetAuctionSortColumnName(AuctionSortColumnId col) {
  switch (col) {
    case AuctionSortColumnId::kLevel:          return "level";
    case AuctionSortColumnId::kQuality:        return "quality";
    case AuctionSortColumnId::kBuyoutThenBid:  return "buyoutthenbid";
    case AuctionSortColumnId::kDuration:       return "duration";
    case AuctionSortColumnId::kStatus:         return "status";
    case AuctionSortColumnId::kName:           return "name";
    case AuctionSortColumnId::kMinBidBuyout:   return "minbidbuyout";
    case AuctionSortColumnId::kSeller:         return "seller";
    case AuctionSortColumnId::kBid:            return "bid";
    case AuctionSortColumnId::kQuantity:       return "quantity";
    case AuctionSortColumnId::kBuyout:         return "buyout";
    default: return nullptr;
  }
}

inline bool ParseAuctionSortColumnName(const char* name,
                                       AuctionSortColumnId& out) {
  struct Entry { const char* name; AuctionSortColumnId id; };
  static constexpr Entry kMap[] = {
    {"quality",       AuctionSortColumnId::kQuality},
    {"level",         AuctionSortColumnId::kLevel},
    {"duration",      AuctionSortColumnId::kDuration},
    {"status",        AuctionSortColumnId::kStatus},
    {"bid",           AuctionSortColumnId::kBid},
    {"name",          AuctionSortColumnId::kName},
    {"minbidbuyout",  AuctionSortColumnId::kMinBidBuyout},
    {"seller",        AuctionSortColumnId::kSeller},
    {"buyoutthenbid", AuctionSortColumnId::kBuyoutThenBid},
    {"quantity",      AuctionSortColumnId::kQuantity},
    {"buyout",        AuctionSortColumnId::kBuyout},
  };
  for (const auto& e : kMap) {
    if (detail::StrCmpNoCase(name, e.name) == 0) {
      out = e.id;
      return true;
    }
  }
  return false;
}

inline bool ParseAuctionListType(const char* name, AuctionListType& out) {
  if (detail::StrCmpNoCase(name, "list") == 0)   { out = AuctionListType::kList;   return true; }
  if (detail::StrCmpNoCase(name, "owner") == 0)  { out = AuctionListType::kOwner;  return true; }
  if (detail::StrCmpNoCase(name, "bidder") == 0) { out = AuctionListType::kBidder; return true; }
  return false;
}

struct AuctionEnchant {
  std::uint32_t id = 0;
  std::uint32_t duration = 0;
  std::uint32_t charges = 0;
};

struct AuctionEntry {
  std::uint32_t auction_id = 0;
  std::uint32_t item_entry = 0;
  AuctionEnchant enchants[kAuctionEnchantSlots] = {};
  std::int32_t random_property_id = 0;
  std::uint32_t suffix_factor = 0;
  std::uint32_t stack_count = 0;
  std::uint32_t spell_charges = 0;
  std::uint32_t unk_flags = 0;
  std::uint64_t owner_guid = 0;
  std::uint32_t start_bid = 0;
  std::uint32_t minimum_outbid = 0;
  std::uint32_t buyout = 0;
  std::uint32_t time_left_ms = 0;
  std::uint32_t expiration_tick_ms = 0;
  std::uint64_t bidder_guid = 0;
  std::uint32_t current_bid = 0;
  std::uint32_t pending_sale_flag = 0;

};

struct AuctionListResult {
  std::vector<AuctionEntry> auctions;
  std::uint32_t received_at_tick_ms = 0;
  std::uint32_t total_count = 0;
  std::uint32_t search_delay = 0;
};

struct AuctionCommandResult {
  std::uint32_t auction_id = 0;
  AuctionAction action = AuctionAction::kSellItem;
  std::uint32_t error_code = 0;
  std::uint32_t bid_error = 0;
  std::uint64_t competing_bidder_guid = 0;
  std::uint32_t competing_bid = 0;
  std::uint32_t minimum_increment = 0;
};

struct AuctionBidderNotification {
  std::uint32_t auction_house_id = 0;
  std::uint32_t auction_id = 0;
  std::uint64_t bidder_guid = 0;
  std::uint32_t current_bid = 0;
  std::uint32_t time_left_ms = 0;
  std::uint32_t item_template = 0;
  std::int32_t random_property_id = 0;
};

struct AuctionOwnerNotification {
  std::uint32_t auction_id = 0;
  std::uint32_t bid = 0;
  std::uint32_t time_left_ms = 0;
  std::uint64_t bidder_guid = 0;
  std::uint32_t item_template = 0;
  std::int32_t random_property_id = 0;
  float         money = 0.0f;
};

struct AuctionSortCriteria {
  std::uint8_t sort_mode = 0;
  std::uint8_t is_desc = 0;
};

struct AuctionRemovedNotification {
  std::uint32_t auction_id = 0;
  std::uint32_t item_template = 0;
  std::uint32_t random_property_id = 0;
};

struct AuctionOutbidEntry {
  std::uint32_t auction_house_id = 0;
  std::uint32_t auction_id = 0;
};

struct AuctionHelloPayload {
  std::uint64_t auctioneer_guid = 0;
  std::uint32_t auction_house_id = 0;
  bool enabled = false;
};

struct PendingSaleEntry {
  std::string item_key;
  std::string full_item_desc;
  std::uint32_t amount = 0;
  std::uint32_t count = 0;
  float         time_remaining_days = 0.0f;

  std::uint32_t parsed_item_id = 0;
  std::uint32_t parsed_stack_count = 0;
  std::uint32_t parsed_type = 0;
  std::uint32_t parsed_auction_id = 0;

  std::uint64_t bidder_guid = 0;
  std::uint32_t current_bid = 0;
  std::uint32_t buyout = 0;
};

class AuctionInteraction {
 public:
  [[nodiscard]] AuctionState& state() noexcept { return state_; }
  [[nodiscard]] const AuctionState& state() const noexcept { return state_; }
  void ApplyAuctionHello(const AuctionHelloPayload& hello);
  void HandleAuctionHello(AuctionHelloPayload hello);
  void HandleAuctionListResult(AuctionListResult result);
  void HandleAuctionOwnerListResult(AuctionListResult result);
  void HandleAuctionBidderListResult(AuctionListResult result);
  void HandleAuctionCommandResult(AuctionCommandResult result);
  void HandleAuctionBidderNotification(AuctionBidderNotification notification);
  void HandleAuctionOwnerNotification(AuctionOwnerNotification notification);
  void HandleAuctionListPendingSales(std::vector<PendingSaleEntry> sales);
  void HandleAuctionRemovedNotification(AuctionRemovedNotification notification);
  [[nodiscard]] bool bidder_list_request_enabled() const {
    return bidder_list_request_enabled_;
  }
  [[nodiscard]] std::uint32_t bidder_list_offset() const {
    return bidder_list_offset_;
  }
  [[nodiscard]] std::vector<std::uint32_t> CollectOutbidAuctionIdsForCurrentHouse()
      const;
  void EnableBidderListRequest() {
    bidder_list_request_enabled_ = true;
  }
  void MarkBidderListRequestSent(std::uint32_t list_from);
  void RemoveOutbidAuction(std::uint32_t auction_id);
  void RemoveAuctionById(std::uint32_t auction_id);
  void RecordPendingBid(std::uint32_t auction_id, std::uint32_t amount) {
    pending_bid_auction_id_ = auction_id;
    pending_bid_amount_ = amount;
  }
  [[nodiscard]] std::uint32_t PendingBidAmountFor(
      std::uint32_t auction_id) const {
    return pending_bid_auction_id_ == auction_id ? pending_bid_amount_ : 0;
  }
  [[nodiscard]] bool owner_refresh_enabled() const {
    return owner_refresh_enabled_;
  }
  void EnableOwnerRefresh() {
    owner_refresh_enabled_ = true;
  }
  void MarkOwnerRefreshSent() {
    owner_refresh_enabled_ = false;
  }

  void FinalizeSearchResultsWhenItemTemplatesReady(
      QueryCache& query_cache, std::function<void()> on_ready);
  void FinalizeBidderResultsWhenItemTemplatesReady(
      QueryCache& query_cache, std::function<void()> on_ready);
  void BeginOwnerAuctionRefresh();
  void FinalizeOwnerListPacketWhenItemTemplatesReady(
      QueryCache& query_cache, std::function<void()> on_ready);
  void FinalizePendingSalesPacketWhenItemTemplatesReady(
      QueryCache& query_cache, std::function<void()> on_ready);

  [[nodiscard]] std::uint64_t auctioneer_guid() const {
    return auctioneer_guid_;
  }
  [[nodiscard]] std::uint32_t auction_house_id() const {
    return auction_house_id_;
  }
  [[nodiscard]] std::uint32_t deposit_rate() const {
    return deposit_rate_;
  }
  [[nodiscard]] bool enabled() const { return enabled_; }
  void SetDepositRate(std::uint32_t deposit_rate) {
    deposit_rate_ = deposit_rate;
  }
  void CloseAuctionHouse() {
    ResetSessionData();
    auctioneer_guid_ = 0;
    auction_house_id_ = 0;
    deposit_rate_ = 0;
    enabled_ = false;
    bidder_list_request_enabled_ = false;
    owner_refresh_enabled_ = false;
    bidder_list_offset_ = 0;
  }
  [[nodiscard]] const AuctionListResult& search_results() const {
    return search_results_;
  }
  [[nodiscard]] const AuctionListResult& owner_results() const {
    return owner_results_;
  }
  [[nodiscard]] const AuctionListResult& bidder_results() const {
    return bidder_results_;
  }
  [[nodiscard]] const std::optional<AuctionCommandResult>& last_command()
      const {
    return last_command_;
  }
  [[nodiscard]] const std::optional<AuctionBidderNotification>&
  last_bidder_notification() const {
    return last_bidder_notification_;
  }
  [[nodiscard]] const std::optional<AuctionOwnerNotification>&
  last_owner_notification() const {
    return last_owner_notification_;
  }
  [[nodiscard]] const std::vector<PendingSaleEntry>& pending_sales() const {
    return pending_sales_;
  }
  [[nodiscard]] const std::optional<AuctionRemovedNotification>&
  last_removed_notification() const {
    return last_removed_notification_;
  }

  void Clear();

 private:
  AuctionState state_;
  enum class OwnerRefreshPacketKind : std::uint8_t {
    kOwnerList,
    kPendingSales,
  };

  void OnSearchResultItemTemplateResolved(std::uint32_t generation);
  void OnBidderResultItemTemplateResolved(std::uint32_t generation);
  void FinalizeOwnerRefreshPacketWhenItemTemplatesReady(
      OwnerRefreshPacketKind kind,
      QueryCache& query_cache,
      std::function<void()> on_ready);
  [[nodiscard]] bool ShouldFinalizeOwnerRefreshLocked() const;
  std::uint32_t& OwnerRefreshGeneration(OwnerRefreshPacketKind kind);
  std::uint32_t& OwnerRefreshPendingTemplateCounter(
      OwnerRefreshPacketKind kind);
  void OnOwnerRefreshItemTemplateResolved(OwnerRefreshPacketKind kind,
                                          std::uint32_t generation);
  void ResetSessionData();

  std::uint64_t auctioneer_guid_ = 0;
  std::uint32_t auction_house_id_ = 0;
  std::uint32_t deposit_rate_ = 0;
  bool enabled_ = false;
  AuctionListResult search_results_;
  AuctionListResult owner_results_;
  AuctionListResult bidder_results_;
  std::optional<AuctionCommandResult> last_command_;
  std::optional<AuctionBidderNotification> last_bidder_notification_;
  std::optional<AuctionOwnerNotification> last_owner_notification_;
  std::vector<PendingSaleEntry> pending_sales_;
  std::optional<AuctionRemovedNotification> last_removed_notification_;
  bool bidder_list_request_enabled_ = false;
  bool owner_refresh_enabled_ = false;
  std::uint32_t bidder_list_offset_ = 0;
  std::uint32_t pending_bid_auction_id_ = 0;
  std::uint32_t pending_bid_amount_ = 0;
  std::vector<AuctionOutbidEntry> outbid_auctions_;
  std::mutex search_result_resolution_mutex_;
  std::uint32_t search_result_resolution_generation_ = 0;
  std::uint32_t pending_search_result_item_templates_ = 0;
  std::function<void()> pending_search_results_ready_callback_;
  std::mutex bidder_result_resolution_mutex_;
  std::uint32_t bidder_result_resolution_generation_ = 0;
  std::uint32_t pending_bidder_result_item_templates_ = 0;
  std::function<void()> pending_bidder_results_ready_callback_;
  std::mutex owner_refresh_resolution_mutex_;
  std::uint32_t owner_list_resolution_generation_ = 0;
  std::uint32_t pending_sale_resolution_generation_ = 0;
  std::uint32_t pending_owner_refresh_server_packets_ = 0;
  std::uint32_t pending_owner_list_item_templates_ = 0;
  std::uint32_t pending_pending_sale_item_templates_ = 0;
  std::function<void()> pending_owner_refresh_ready_callback_;
};

}
