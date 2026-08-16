#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct AuctionItem {
    uint32_t auction_id = 0;
    uint32_t item_entry = 0;
    uint32_t count = 0;
    std::string item_name;
    uint32_t required_level = 0;
    bool has_item_template = false;
    uint64_t owner_guid = 0;
    std::string owner_name;
    uint32_t start_bid = 0;
    uint32_t minimum_increment = 0;
    uint32_t buyout = 0;
    uint32_t current_bid = 0;
    uint32_t time_left = 0;
    uint32_t remaining_time_ms = 0;
    uint32_t expiration_tick_ms = 0;
    uint32_t sale_status = 0;

    uint64_t bidder_guid = 0;
    int32_t random_property = 0;
    uint32_t random_suffix = 0;
    uint8_t quality = 0;
    uint32_t enchant_id = 0;
    std::array<uint32_t, 3> gem_enchant_ids = {};
};

enum class AuctionSelectionList : std::uint8_t {
    kList = 0,
    kOwner = 1,
    kBidder = 2,
};

enum class AuctionRemovalMask : std::uint8_t {
    kNone = 0,
    kList = 1 << 0,
    kOwner = 1 << 1,
    kBidder = 1 << 2,
};

[[nodiscard]] constexpr AuctionRemovalMask operator|(
    AuctionRemovalMask lhs, AuctionRemovalMask rhs) noexcept {
    return static_cast<AuctionRemovalMask>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

inline AuctionRemovalMask& operator|=(
    AuctionRemovalMask& lhs, AuctionRemovalMask rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

struct AuctionSellItemSelection {
    std::uint64_t item_guid = 0;
    std::uint64_t container_guid = 0;
    int slot_id = 0;

    [[nodiscard]] bool IsEmpty() const { return item_guid == 0; }
};

struct AuctionMultiSellSource {
    std::uint64_t item_guid = 0;
    std::uint32_t remaining_count = 0;
};

struct AuctionMultiSellRequest {
    std::uint64_t auctioneer_guid = 0;
    std::uint32_t min_bid = 0;
    std::uint32_t buyout = 0;
    std::uint32_t duration_minutes = 0;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> items;
};

class AuctionState {
 public:
    AuctionState();

    void SetSearchResults(const std::vector<AuctionItem>& items,
                          uint32_t totalCount);
    [[nodiscard]] size_t GetNumResults() const;
    [[nodiscard]] std::optional<AuctionItem> GetResult(size_t index) const;
    [[nodiscard]] uint32_t GetTotalResultCount() const;

    void SetOwnAuctions(const std::vector<AuctionItem>& items);
    [[nodiscard]] size_t GetNumOwnAuctions() const;
    [[nodiscard]] std::optional<AuctionItem> GetOwnAuction(size_t index) const;
    [[nodiscard]] bool CanCancelOwnAuction(size_t index) const;

    void SetMyBids(const std::vector<AuctionItem>& items,
                   uint32_t totalCount);
    void SetMyBids(const std::vector<AuctionItem>& items) {
        SetMyBids(items, static_cast<uint32_t>(items.size()));
    }
    [[nodiscard]] size_t GetNumBids() const;
    [[nodiscard]] std::optional<AuctionItem> GetBid(size_t index) const;
    [[nodiscard]] uint32_t GetTotalBidCount() const;

    [[nodiscard]] uint8_t ApplyBidderNotificationToLists(
        uint32_t auction_id, uint32_t new_bid,
        uint64_t bidder_guid, uint32_t time_left_ms);
    [[nodiscard]] uint8_t ApplyOwnerNotificationToLists(
        uint32_t auction_id, uint32_t new_bid,
        uint64_t bidder_guid, uint32_t time_left_ms);
    [[nodiscard]] bool ApplySuccessfulBidToBrowse(
        uint32_t auction_id, uint32_t new_bid, uint64_t bidder_guid,
        uint32_t minimum_increment);
    [[nodiscard]] uint8_t ApplyHigherBidResultToLists(
        uint32_t auction_id, uint32_t new_bid, uint64_t bidder_guid,
        uint32_t minimum_increment);

    [[nodiscard]] bool IsQueryPending(uint32_t queryType) const;

    void SetSelectedAuctionItem(AuctionSelectionList list, size_t index);
    [[nodiscard]] uint32_t GetSelectedAuctionItem(
        AuctionSelectionList list) const;

    void SetAtAH(bool at);
    [[nodiscard]] bool IsAtAH() const;
    [[nodiscard]] std::vector<std::uint64_t> OpenAuctionHouse();
    void SetAuctionsTabShowing(bool showing);
    [[nodiscard]] bool auctions_tab_showing() const;

    void SetSellItemSelection(AuctionSellItemSelection selection);
    void ClearSellItemSelection();
    [[nodiscard]] std::optional<std::uint64_t> TakeSellItemSelection();
    [[nodiscard]] AuctionSellItemSelection GetSellItemSelection() const;
    [[nodiscard]] bool HasSellItemSelection() const;

    void BeginMultiSell(std::uint64_t auctioneer_guid,
                        std::uint32_t min_bid,
                        std::uint32_t buyout,
                        std::uint32_t duration_minutes,
                        std::uint32_t stack_size,
                        std::uint32_t total_stacks,
                        std::vector<AuctionMultiSellSource> sources);
    [[nodiscard]] std::vector<std::uint64_t> AbortMultiSell();
    [[nodiscard]] std::vector<std::uint64_t> CompleteMultiSell();
    [[nodiscard]] std::optional<AuctionMultiSellRequest>
    PrepareNextMultiSellRequest();
    [[nodiscard]] bool HasActiveMultiSell() const;
    [[nodiscard]] bool IsTrackedMultiSellSource(std::uint64_t item_guid) const;
    [[nodiscard]] std::uint32_t GetMultiSellCompletedStacks() const;
    [[nodiscard]] std::uint32_t GetMultiSellTotalStacks() const;

    void SetDepositCost(uint32_t copper);
    [[nodiscard]] uint32_t GetDepositCost() const;

    [[nodiscard]] bool CanSendAuctionQuery(uint32_t queryType) const;
    [[nodiscard]] bool CanSendGetAllAuctionQuery(uint32_t queryType) const;
    void MarkQuerySent(uint32_t queryType, uint32_t pendingPackets = 1);
    void MarkBrowseQuerySent(bool getAllRequest);
    void MarkQueryPacketReceived(uint32_t queryType);
    void MarkQueryComplete(uint32_t queryType);
    void SetQueryCooldown(uint32_t ms);

    static constexpr uint32_t kMaxAuctionSortEntries = 12;

    struct AuctionSortEntry {
        uint32_t column   = 0;
        uint32_t reversed = 0;
        uint32_t active   = 0;
    };

    [[nodiscard]] std::optional<AuctionSortEntry> GetSortEntry(
        AuctionSelectionList list, uint32_t index) const;
    void PromoteSortEntry(AuctionSelectionList list, uint32_t column,
                          bool reversed);
    void ClearSortEntries(AuctionSelectionList list);
    void ApplySort(AuctionSelectionList list);
    [[nodiscard]] AuctionRemovalMask RemoveAuction(std::uint32_t auctionId);

    void Reset();

 private:
    [[nodiscard]] static constexpr std::size_t ToSelectionIndex(
        AuctionSelectionList list) {
        return static_cast<std::size_t>(list);
    }

    [[nodiscard]] const std::vector<AuctionItem>& GetListForSelection(
        AuctionSelectionList list) const;
    [[nodiscard]] std::vector<AuctionItem>& GetListForSelection(
        AuctionSelectionList list);
    [[nodiscard]] std::optional<uint32_t>& GetSelectedAuctionId(
        AuctionSelectionList list);
    [[nodiscard]] const std::optional<uint32_t>& GetSelectedAuctionId(
        AuctionSelectionList list) const;

    [[nodiscard]] bool CanSendAuctionQueryInternal(uint32_t queryType) const;
    void MarkQuerySentInternal(uint32_t queryType, uint32_t pendingPackets,
                               uint32_t now_tick);
    void ResetSortEntries();
    void ApplySortInternal(AuctionSelectionList list);
    [[nodiscard]] std::vector<std::uint64_t> EndMultiSell();

    struct AuctionMultiSellState {
        bool active = false;
        std::uint64_t auctioneer_guid = 0;
        std::uint32_t min_bid = 0;
        std::uint32_t buyout = 0;
        std::uint32_t duration_minutes = 0;
        std::uint32_t stack_size = 0;
        std::uint32_t stacks_remaining = 0;
        std::uint32_t total_stacks = 0;
        std::vector<AuctionMultiSellSource> sources;
    };

    std::vector<AuctionItem> results_;
    uint32_t total_count_ = 0;
    std::vector<AuctionItem> own_auctions_;
    std::vector<AuctionItem> bids_;
    uint32_t total_bid_count_ = 0;
    std::array<std::optional<uint32_t>, 3> selected_auction_ids_ = {};
    bool at_ah_ = false;
    bool auctions_tab_showing_ = false;
    AuctionSellItemSelection sell_item_selection_{};
    AuctionMultiSellState multi_sell_{};
    uint32_t deposit_ = 0;

    static constexpr uint32_t kMaxQueryTypes = 3;
    static constexpr uint32_t kGetAllQueryCooldownMs = 900000;
    uint32_t query_pending_[kMaxQueryTypes] = {};
    uint32_t query_last_time_[kMaxQueryTypes] = {};
    uint32_t query_cooldown_ms_ = 0;
    uint32_t browse_get_all_last_time_ = 0;

    std::array<std::array<AuctionSortEntry, kMaxAuctionSortEntries>, 3>
        sort_entries_ = {};
};

}
