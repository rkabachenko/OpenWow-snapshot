
#include "openwow/game/commerce/auctions/auction_detail_display.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

void AuctionDetailDisplay::Open()  { open_ = true; }
void AuctionDetailDisplay::Close() {
    open_ = false;
    listings_.clear();
    selectedAuctionId_ = 0;
}
bool AuctionDetailDisplay::IsOpen() const { return open_; }

void AuctionDetailDisplay::SetListings(
    const std::vector<AuctionListingInfo>& listings) {
    if (listings.size() > kAuctionDetailMaxResults) {
        listings_.assign(listings.begin(),
                         listings.begin() + kAuctionDetailMaxResults);
    } else {
        listings_ = listings;
    }
}

const std::vector<AuctionListingInfo>&
AuctionDetailDisplay::GetListings() const {
    return listings_;
}

uint32_t AuctionDetailDisplay::GetListingCount() const {
    return static_cast<uint32_t>(listings_.size());
}

void AuctionDetailDisplay::SortBy(AuctionSortType sortType, bool ascending) {
    auto cmp = [&](const AuctionListingInfo& a,
                   const AuctionListingInfo& b) -> bool {
        switch (sortType) {
            case AuctionSortType::ByName:
                return ascending ? (a.itemName < b.itemName)
                                 : (a.itemName > b.itemName);
            case AuctionSortType::ByLevel:
                return ascending ? (a.level < b.level) : (a.level > b.level);
            case AuctionSortType::ByBid:
                return ascending ? (a.currentBid < b.currentBid)
                                 : (a.currentBid > b.currentBid);
            case AuctionSortType::ByBuyout:
                return ascending ? (a.buyout < b.buyout)
                                 : (a.buyout > b.buyout);
            case AuctionSortType::ByTime:
                return ascending ? (a.timeLeft < b.timeLeft)
                                 : (a.timeLeft > b.timeLeft);
            case AuctionSortType::BySeller:
                return ascending ? (a.sellerName < b.sellerName)
                                 : (a.sellerName > b.sellerName);
            default:
                return false;
        }
    };
    std::sort(listings_.begin(), listings_.end(), cmp);
}

static std::string ToLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(
                         static_cast<unsigned char>(c))));
    return out;
}

std::vector<AuctionListingInfo>
AuctionDetailDisplay::SearchByName(const std::string& query) const {
    std::vector<AuctionListingInfo> result;
    std::string lq = ToLower(query);
    for (const auto& l : listings_) {
        if (ToLower(l.itemName).find(lq) != std::string::npos) {
            result.push_back(l);
        }
    }
    return result;
}

std::vector<AuctionListingInfo>
AuctionDetailDisplay::FilterByQuality(uint8_t minQuality) const {
    std::vector<AuctionListingInfo> result;
    for (const auto& l : listings_) {
        if (l.quality >= minQuality) result.push_back(l);
    }
    return result;
}

std::vector<AuctionListingInfo>
AuctionDetailDisplay::FilterByLevel(uint16_t minLevel,
                                    uint16_t maxLevel) const {
    std::vector<AuctionListingInfo> result;
    for (const auto& l : listings_) {
        if (l.level >= minLevel && l.level <= maxLevel) result.push_back(l);
    }
    return result;
}

std::optional<AuctionListingInfo>
AuctionDetailDisplay::GetListing(uint32_t auctionId) const {
    const auto* p = FindListing(auctionId);
    if (p) return *p;
    return std::nullopt;
}

void AuctionDetailDisplay::SetSelectedListing(uint32_t auctionId) {
    selectedAuctionId_ = auctionId;
}

std::optional<AuctionListingInfo>
AuctionDetailDisplay::GetSelectedListing() const {
    return GetListing(selectedAuctionId_);
}

bool AuctionDetailDisplay::CanBid(uint32_t auctionId, uint32_t bidAmount,
                                  uint32_t playerGold) const {
    const auto* l = FindListing(auctionId);
    if (!l) return false;
    uint32_t minBid = l->currentBid + GetMinBidIncrement(auctionId);

    if (l->currentBid == 0) minBid = 1;
    return bidAmount >= minBid && playerGold >= bidAmount;
}

bool AuctionDetailDisplay::CanBuyout(uint32_t auctionId,
                                     uint32_t playerGold) const {
    const auto* l = FindListing(auctionId);
    if (!l) return false;
    if (l->buyout == 0) return false;
    return playerGold >= l->buyout;
}

uint32_t AuctionDetailDisplay::GetMinBidIncrement(uint32_t auctionId) const {
    const auto* l = FindListing(auctionId);
    if (!l) return 0;

    uint32_t inc = l->currentBid / 20;
    return (inc < 1) ? 1 : inc;
}

void AuctionDetailDisplay::SetPlayerGold(uint32_t copper) {
    playerGold_ = copper;
}

uint32_t AuctionDetailDisplay::GetPlayerGold() const { return playerGold_; }

std::string AuctionDetailDisplay::GetTimeLeftString(uint8_t timeLeft) {
    switch (timeLeft) {
        case 0:  return "Short";
        case 1:  return "Medium";
        case 2:  return "Long";
        case 3:  return "Very Long";
        default: return "Unknown";
    }
}

const AuctionListingInfo*
AuctionDetailDisplay::FindListing(uint32_t auctionId) const {
    for (const auto& l : listings_) {
        if (l.auctionId == auctionId) return &l;
    }
    return nullptr;
}

}
