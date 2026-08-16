
#include "openwow/game/commerce/auctions/auction_state.h"

#include <algorithm>
#include <utility>

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/objects/cgobject.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kAuctionOpenQueryCooldownMs = 5000;

int CompareUnsignedAscending(std::uint32_t lhs, std::uint32_t rhs) {
    if (lhs == rhs) {
        return 0;
    }
    return lhs < rhs ? -1 : 1;
}

int CompareUnsignedDescending(std::uint32_t lhs, std::uint32_t rhs) {
    if (lhs == rhs) {
        return 0;
    }
    return lhs < rhs ? 1 : -1;
}

int CompareUnsigned64(std::uint64_t lhs, std::uint64_t rhs) {
    if (lhs == rhs) {
        return 0;
    }
    return lhs < rhs ? -1 : 1;
}

std::uint32_t ActiveBidValue(const AuctionItem& item) {
    return item.current_bid != 0 ? item.current_bid : item.start_bid;
}

std::uint32_t BuyoutOrBidValue(const AuctionItem& item) {
    return item.buyout != 0 ? item.buyout : ActiveBidValue(item);
}

std::uint32_t SellerStatusRank(const AuctionItem& item,
                               std::uint64_t active_player_guid) {
    if (item.bidder_guid == active_player_guid) {
        return 2;
    }
    if (item.bidder_guid != 0) {
        return 1;
    }
    return 0;
}

int CompareAuctionField(const AuctionItem& lhs, const AuctionItem& rhs,
                        std::uint32_t column,
                        std::uint64_t active_player_guid) {
    switch (static_cast<AuctionSortColumnId>(column)) {
        case AuctionSortColumnId::kLevel:
            if (!lhs.has_item_template || !rhs.has_item_template) {
                return 0;
            }
            return CompareUnsignedAscending(std::max(lhs.required_level, 1u),
                                            std::max(rhs.required_level, 1u));
        case AuctionSortColumnId::kQuality:
            if (!lhs.has_item_template || !rhs.has_item_template) {
                return 0;
            }
            return CompareUnsignedDescending(lhs.quality, rhs.quality);
        case AuctionSortColumnId::kBuyoutThenBid: {
            const auto lhs_value = BuyoutOrBidValue(lhs);
            const auto rhs_value = BuyoutOrBidValue(rhs);
            const auto primary = CompareUnsignedAscending(lhs_value, rhs_value);
            if (primary != 0) {
                return primary;
            }
            return CompareUnsignedAscending(ActiveBidValue(lhs),
                                            ActiveBidValue(rhs));
        }
        case AuctionSortColumnId::kDuration:
            if (lhs.sale_status != rhs.sale_status) {
                return CompareUnsignedDescending(lhs.sale_status,
                                                 rhs.sale_status);
            }
            return CompareUnsignedAscending(lhs.remaining_time_ms,
                                            rhs.remaining_time_ms);
        case AuctionSortColumnId::kStatus:
            return CompareUnsignedDescending(
                SellerStatusRank(lhs, active_player_guid),
                SellerStatusRank(rhs, active_player_guid));
        case AuctionSortColumnId::kName:
            if (!lhs.has_item_template || !rhs.has_item_template) {
                return 0;
            }
            return openwow::core::SStrCmpNoCaseCollate(
                lhs.item_name.c_str(), rhs.item_name.c_str(), 0x7FFFFFFFu);
        case AuctionSortColumnId::kMinBidBuyout:
            return CompareUnsignedAscending(BuyoutOrBidValue(lhs),
                                            BuyoutOrBidValue(rhs));
        case AuctionSortColumnId::kSeller:
            if (!lhs.owner_name.empty() && !rhs.owner_name.empty()) {
                return openwow::core::SStrCmpI(
                    lhs.owner_name.c_str(), rhs.owner_name.c_str(),
                    0x7FFFFFFFu);
            }
            return CompareUnsigned64(lhs.owner_guid, rhs.owner_guid);
        case AuctionSortColumnId::kBid:
            return CompareUnsignedAscending(ActiveBidValue(lhs),
                                            ActiveBidValue(rhs));
        case AuctionSortColumnId::kQuantity:
            return CompareUnsignedAscending(lhs.count, rhs.count);
        case AuctionSortColumnId::kBuyout:
            return CompareUnsignedAscending(lhs.buyout, rhs.buyout);
        default:
            return 0;
    }
}

void ApplyAuctionSortEntries(std::vector<AuctionItem>& items,
                             const std::array<AuctionState::AuctionSortEntry,
                                              AuctionState::kMaxAuctionSortEntries>& entries) {
    const auto active_player_guid = CGObject_C::GetActivePlayerGuid().GetRawValue();
    std::sort(items.begin(), items.end(),
              [&](const AuctionItem& lhs, const AuctionItem& rhs) {

                  for (const auto& entry : entries) {
                      const auto result = CompareAuctionField(
                          lhs, rhs, entry.column, active_player_guid);
                      if (result == 0) {
                          continue;
                      }
                      return entry.reversed != 0 ? result > 0 : result < 0;
                  }
                  return false;
              });
}

bool RemoveAuctionFromList(std::vector<AuctionItem>& items,
                           std::uint32_t auction_id,
                           bool decrement_total_count,
                           std::uint32_t* total_count) {
    const auto it = std::find_if(
        items.begin(), items.end(),
        [auction_id](const AuctionItem& item) {
            return item.auction_id == auction_id;
        });
    if (it == items.end()) {
        return false;
    }

    items.erase(it);
    if (decrement_total_count && total_count != nullptr && *total_count > 0) {
        --(*total_count);
    }
    return true;
}

bool PatchAuctionBidStateInList(std::vector<AuctionItem>& items,
                                const std::uint32_t auction_id,
                                const std::uint32_t new_bid,
                                const std::uint64_t bidder_guid,
                                const std::uint32_t time_left_ms,
                                const std::uint32_t now_tick) {
    const auto it = std::find_if(
        items.begin(), items.end(),
        [auction_id](const AuctionItem& item) {
            return item.auction_id == auction_id;
        });
    if (it == items.end()) {
        return false;
    }

    it->current_bid = new_bid;
    it->bidder_guid = bidder_guid;
    it->remaining_time_ms = time_left_ms;

    constexpr std::uint32_t kMinExpirationExtensionMs = 90000;
    const auto min_expiry = now_tick + kMinExpirationExtensionMs;
    if (it->expiration_tick_ms <= min_expiry) {
        it->expiration_tick_ms = min_expiry;
    }
    return true;
}

bool PatchAuctionCommandBidStateInList(
    std::vector<AuctionItem>& items, const std::uint32_t auction_id,
    const std::uint32_t new_bid, const std::uint64_t bidder_guid,
    const std::uint32_t minimum_increment, const bool extend_expiration,
    const std::uint32_t now_tick) {
    const auto it = std::find_if(
        items.begin(), items.end(),
        [auction_id](const AuctionItem& item) {
            return item.auction_id == auction_id;
        });
    if (it == items.end()) {
        return false;
    }

    it->current_bid = new_bid;
    it->minimum_increment = minimum_increment;
    it->bidder_guid = bidder_guid;
    if (extend_expiration) {
        const auto minimum_expiration = now_tick + 90000u;
        if (static_cast<std::int32_t>(it->expiration_tick_ms -
                                      minimum_expiration) < 0) {
            it->expiration_tick_ms = minimum_expiration;
        }
    }
    return true;
}

}

AuctionState::AuctionState() {
    ResetSortEntries();
}

void AuctionState::SetSearchResults(const std::vector<AuctionItem>& items,
                                     uint32_t totalCount) {
    results_ = items;
    total_count_ = totalCount;
    ApplySortInternal(AuctionSelectionList::kList);
}

size_t AuctionState::GetNumResults() const {
    return results_.size();
}

std::optional<AuctionItem> AuctionState::GetResult(size_t index) const {
    if (index >= results_.size()) return std::nullopt;
    return results_[index];
}

uint32_t AuctionState::GetTotalResultCount() const {
    return total_count_;
}

void AuctionState::SetOwnAuctions(const std::vector<AuctionItem>& items) {
    own_auctions_ = items;
    ApplySortInternal(AuctionSelectionList::kOwner);
}

size_t AuctionState::GetNumOwnAuctions() const {
    return own_auctions_.size();
}

std::optional<AuctionItem> AuctionState::GetOwnAuction(size_t index) const {
    if (index >= own_auctions_.size()) return std::nullopt;
    return own_auctions_[index];
}

bool AuctionState::CanCancelOwnAuction(size_t index) const {
    if (index >= own_auctions_.size()) {
        return false;
    }

    return own_auctions_[index].sale_status == 0;
}

void AuctionState::SetMyBids(const std::vector<AuctionItem>& items,
                              const uint32_t totalCount) {
    bids_ = items;
    total_bid_count_ = totalCount;
    ApplySortInternal(AuctionSelectionList::kBidder);
}

size_t AuctionState::GetNumBids() const {
    return bids_.size();
}

std::optional<AuctionItem> AuctionState::GetBid(size_t index) const {
    if (index >= bids_.size()) return std::nullopt;
    return bids_[index];
}

uint32_t AuctionState::GetTotalBidCount() const {
    return total_bid_count_;
}

uint8_t AuctionState::ApplyBidderNotificationToLists(
    const uint32_t auction_id, const uint32_t new_bid,
    const uint64_t bidder_guid, const uint32_t time_left_ms) {

    const auto now_tick = core::GameClock::GetTickCount32();
    uint8_t modified = 0;
    if (PatchAuctionBidStateInList(bids_, auction_id, new_bid, bidder_guid,
                                   time_left_ms, now_tick)) {
        modified |= static_cast<uint8_t>(AuctionRemovalMask::kBidder);
    }
    if (PatchAuctionBidStateInList(results_, auction_id, new_bid, bidder_guid,
                                   time_left_ms, now_tick)) {
        modified |= static_cast<uint8_t>(AuctionRemovalMask::kList);
    }
    return modified;
}

uint8_t AuctionState::ApplyOwnerNotificationToLists(
    const uint32_t auction_id, const uint32_t new_bid,
    const uint64_t bidder_guid, const uint32_t time_left_ms) {

    const auto now_tick = core::GameClock::GetTickCount32();
    uint8_t modified = 0;
    if (PatchAuctionBidStateInList(own_auctions_, auction_id, new_bid,
                                   bidder_guid, time_left_ms, now_tick)) {
        modified |= static_cast<uint8_t>(AuctionRemovalMask::kOwner);
    }
    if (PatchAuctionBidStateInList(results_, auction_id, new_bid, bidder_guid,
                                   time_left_ms, now_tick)) {
        modified |= static_cast<uint8_t>(AuctionRemovalMask::kList);
    }
    return modified;
}

bool AuctionState::ApplySuccessfulBidToBrowse(
    const uint32_t auction_id, const uint32_t new_bid,
    const uint64_t bidder_guid, const uint32_t minimum_increment) {
    return PatchAuctionCommandBidStateInList(
        results_, auction_id, new_bid, bidder_guid, minimum_increment,
        true, core::GameClock::GetTickCount32());
}

uint8_t AuctionState::ApplyHigherBidResultToLists(
    const uint32_t auction_id, const uint32_t new_bid,
    const uint64_t bidder_guid, const uint32_t minimum_increment) {
    const auto now_tick = core::GameClock::GetTickCount32();
    uint8_t modified = 0;
    if (PatchAuctionCommandBidStateInList(
            results_, auction_id, new_bid, bidder_guid, minimum_increment,
            false, now_tick)) {
        modified |= static_cast<uint8_t>(AuctionRemovalMask::kList);
    }
    if (PatchAuctionCommandBidStateInList(
            bids_, auction_id, new_bid, bidder_guid, minimum_increment,
            false, now_tick)) {
        modified |= static_cast<uint8_t>(AuctionRemovalMask::kBidder);
    }
    return modified;
}

bool AuctionState::IsQueryPending(const uint32_t queryType) const {
    if (queryType >= kMaxQueryTypes) {
        return false;
    }
    return query_pending_[queryType] > 0;
}

void AuctionState::SetSelectedAuctionItem(AuctionSelectionList list,
                                           size_t index) {
    auto& items = GetListForSelection(list);
    auto& selected_auction_id = GetSelectedAuctionId(list);
    if (index >= items.size()) {
        selected_auction_id.reset();
        return;
    }

    selected_auction_id = items[index].auction_id;
}

uint32_t AuctionState::GetSelectedAuctionItem(
    AuctionSelectionList list) const {
    const auto& selected_auction_id = GetSelectedAuctionId(list);
    if (!selected_auction_id.has_value()) {
        return 0;
    }

    const auto& items = GetListForSelection(list);
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (items[index].auction_id == *selected_auction_id) {
            return static_cast<uint32_t>(index + 1);
        }
    }

    return 0;
}

void AuctionState::SetAtAH(bool at) {
    bool abort_multi_sell = false;
    bool clear_selection_after_reset = false;
    {
        at_ah_ = at;
        if (!at) {
            auctions_tab_showing_ = false;
            abort_multi_sell = multi_sell_.active;
            clear_selection_after_reset =
                !abort_multi_sell && !sell_item_selection_.IsEmpty();
        }
    }

    if (at) {
        return;
    }

    if (abort_multi_sell) {
        (void)AbortMultiSell();
    }

    {
        results_.clear();
        total_count_ = 0;
        own_auctions_.clear();
        bids_.clear();
        total_bid_count_ = 0;
        selected_auction_ids_.fill(std::nullopt);
        deposit_ = 0;
    }

    if (clear_selection_after_reset) {
        (void)TakeSellItemSelection();
    }
}

bool AuctionState::IsAtAH() const {
    return at_ah_;
}

std::vector<std::uint64_t> AuctionState::OpenAuctionHouse() {
    std::vector<std::uint64_t> cleared_item_guids;
    {
        results_.clear();
        total_count_ = 0;
        own_auctions_.clear();
        bids_.clear();
        total_bid_count_ = 0;
        selected_auction_ids_.fill(std::nullopt);
        at_ah_ = true;
        auctions_tab_showing_ = false;
        deposit_ = 0;

        for (std::uint32_t i = 0; i < kMaxQueryTypes; ++i) {
            query_pending_[i] = 0;
            query_last_time_[i] = 0;
        }
        query_cooldown_ms_ = kAuctionOpenQueryCooldownMs;

        if (!sell_item_selection_.IsEmpty()) {
            cleared_item_guids.push_back(sell_item_selection_.item_guid);
            sell_item_selection_ = {};
        }

        if (multi_sell_.active) {
            for (const auto& source : multi_sell_.sources) {
                if (source.item_guid != 0 &&
                    std::find(cleared_item_guids.begin(),
                              cleared_item_guids.end(),
                              source.item_guid) == cleared_item_guids.end()) {
                    cleared_item_guids.push_back(source.item_guid);
                }
            }
            multi_sell_ = {};
        }
    }

    return cleared_item_guids;
}

void AuctionState::SetAuctionsTabShowing(bool showing) {
    auctions_tab_showing_ = showing;
}

bool AuctionState::auctions_tab_showing() const {
    return auctions_tab_showing_;
}

std::optional<AuctionState::AuctionSortEntry> AuctionState::GetSortEntry(
    AuctionSelectionList list, uint32_t index) const {
    if (index >= kMaxAuctionSortEntries) {
        return std::nullopt;
    }
    return sort_entries_[ToSelectionIndex(list)][index];
}

void AuctionState::PromoteSortEntry(AuctionSelectionList list, uint32_t column,
                                     bool reversed) {
    auto& entries = sort_entries_[ToSelectionIndex(list)];

    std::size_t index = 0;
    while (index < kMaxAuctionSortEntries &&
           entries[index].column != column &&
           entries[index].active != 0) {
        ++index;
    }

    if (index >= kMaxAuctionSortEntries) {
        return;
    }

    while (index > 0) {
        entries[index] = entries[index - 1];
        --index;
    }

    entries[0].column = column;
    entries[0].reversed = reversed ? 1u : 0u;
    entries[0].active = 1;
}

void AuctionState::ClearSortEntries(AuctionSelectionList list) {
    for (auto& entry : sort_entries_[ToSelectionIndex(list)]) {
        entry.active = 0;
    }
}

void AuctionState::ApplySort(AuctionSelectionList list) {
    ApplySortInternal(list);
}

AuctionRemovalMask AuctionState::RemoveAuction(std::uint32_t auctionId) {

    AuctionRemovalMask removed = AuctionRemovalMask::kNone;
    if (RemoveAuctionFromList(results_, auctionId, true, &total_count_)) {
        removed |= AuctionRemovalMask::kList;
    }
    if (RemoveAuctionFromList(own_auctions_, auctionId, false, nullptr)) {
        removed |= AuctionRemovalMask::kOwner;
    }
    if (RemoveAuctionFromList(bids_, auctionId, true, &total_bid_count_)) {
        removed |= AuctionRemovalMask::kBidder;
    }

    if (selected_auction_ids_[ToSelectionIndex(AuctionSelectionList::kList)] ==
        auctionId) {
        selected_auction_ids_[ToSelectionIndex(AuctionSelectionList::kList)] =
            std::nullopt;
    }
    if (selected_auction_ids_[ToSelectionIndex(AuctionSelectionList::kOwner)] ==
        auctionId) {
        selected_auction_ids_[ToSelectionIndex(AuctionSelectionList::kOwner)] =
            std::nullopt;
    }
    if (selected_auction_ids_[ToSelectionIndex(AuctionSelectionList::kBidder)] ==
        auctionId) {
        selected_auction_ids_[ToSelectionIndex(AuctionSelectionList::kBidder)] =
            std::nullopt;
    }

    return removed;
}

void AuctionState::Reset() {
    sell_item_selection_ = {};

    results_.clear();
    total_count_ = 0;
    own_auctions_.clear();
    bids_.clear();
    total_bid_count_ = 0;
    selected_auction_ids_.fill(std::nullopt);
    at_ah_ = false;
    auctions_tab_showing_ = false;
    multi_sell_ = {};
    deposit_ = 0;

    for (uint32_t i = 0; i < kMaxQueryTypes; ++i) {
        query_pending_[i] = 0;
        query_last_time_[i] = 0;
    }
    query_cooldown_ms_ = kAuctionOpenQueryCooldownMs;
    browse_get_all_last_time_ = 0;
    ResetSortEntries();
}

const std::vector<AuctionItem>& AuctionState::GetListForSelection(
    AuctionSelectionList list) const {
    switch (list) {
        case AuctionSelectionList::kOwner:
            return own_auctions_;
        case AuctionSelectionList::kBidder:
            return bids_;
        case AuctionSelectionList::kList:
        default:
            return results_;
    }
}

std::vector<AuctionItem>& AuctionState::GetListForSelection(
    AuctionSelectionList list) {
    switch (list) {
        case AuctionSelectionList::kOwner:
            return own_auctions_;
        case AuctionSelectionList::kBidder:
            return bids_;
        case AuctionSelectionList::kList:
        default:
            return results_;
    }
}

std::optional<uint32_t>& AuctionState::GetSelectedAuctionId(
    AuctionSelectionList list) {
    return selected_auction_ids_[ToSelectionIndex(list)];
}

const std::optional<uint32_t>& AuctionState::GetSelectedAuctionId(
    AuctionSelectionList list) const {
    return selected_auction_ids_[ToSelectionIndex(list)];
}

void AuctionState::ResetSortEntries() {
    const auto promote = [](auto& entries, std::uint32_t column, bool reversed) {
        std::size_t index = 0;
        while (index < kMaxAuctionSortEntries &&
               entries[index].column != column &&
               entries[index].active != 0) {
            ++index;
        }

        if (index >= kMaxAuctionSortEntries) {
            return;
        }

        while (index > 0) {
            entries[index] = entries[index - 1];
            --index;
        }

        entries[0].column = column;
        entries[0].reversed = reversed ? 1u : 0u;
        entries[0].active = 1;
    };

    for (auto& list_entries : sort_entries_) {
        for (auto& entry : list_entries) {
            entry = {};
        }
    }

    auto& list_entries = sort_entries_[ToSelectionIndex(AuctionSelectionList::kList)];
    promote(list_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kDuration), false);
    promote(list_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kBid), false);
    promote(list_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kQuantity), true);
    promote(list_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kMinBidBuyout), false);
    promote(list_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kName), false);
    promote(list_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kLevel), true);
    promote(list_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kQuality), false);

    auto& owner_entries = sort_entries_[ToSelectionIndex(AuctionSelectionList::kOwner)];
    promote(owner_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kQuantity), true);
    promote(owner_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kBid), false);
    promote(owner_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kName), false);
    promote(owner_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kLevel), true);
    promote(owner_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kQuality), false);
    promote(owner_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kStatus), false);
    promote(owner_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kDuration), false);

    auto& bidder_entries = sort_entries_[ToSelectionIndex(AuctionSelectionList::kBidder)];
    promote(bidder_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kQuantity), false);
    promote(bidder_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kName), false);
    promote(bidder_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kLevel), true);
    promote(bidder_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kQuality), false);
    promote(bidder_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kMinBidBuyout), false);
    promote(bidder_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kStatus), false);
    promote(bidder_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kBid), false);
    promote(bidder_entries,
            static_cast<std::uint32_t>(AuctionSortColumnId::kDuration), false);
}

void AuctionState::ApplySortInternal(AuctionSelectionList list) {
    auto& items = GetListForSelection(list);
    ApplyAuctionSortEntries(items, sort_entries_[ToSelectionIndex(list)]);
}

std::vector<std::uint64_t> AuctionState::EndMultiSell() {
    std::vector<std::uint64_t> source_guids;
    {
        if (!multi_sell_.active) {
            return {};
        }

        source_guids.reserve(multi_sell_.sources.size());
        for (const auto& source : multi_sell_.sources) {
            source_guids.push_back(source.item_guid);
        }
        multi_sell_ = {};
    }

    if (const auto selected = TakeSellItemSelection();
        selected.has_value() &&
        std::find(source_guids.begin(), source_guids.end(), *selected) ==
            source_guids.end()) {
        source_guids.push_back(*selected);
    }
    return source_guids;
}

}
