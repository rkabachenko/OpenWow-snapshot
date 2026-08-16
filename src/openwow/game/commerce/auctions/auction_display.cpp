
#include "openwow/game/commerce/auctions/auction_display.h"

#include <algorithm>

namespace openwow::game {

void AuctionDisplay::SetSearchResults(AuctionDisplayListResult results) {
  searchResults_ = std::move(results);
}

AuctionDisplayListResult AuctionDisplay::GetSearchResults() const {
  return searchResults_;
}

void AuctionDisplay::SetMyAuctions(
    std::vector<AuctionDisplayItemEntry> auctions) {
  myAuctions_ = std::move(auctions);
}

std::vector<AuctionDisplayItemEntry> AuctionDisplay::GetMyAuctions() const {
  return myAuctions_;
}

void AuctionDisplay::SetMyBids(
    std::vector<AuctionDisplayItemEntry> bids) {
  myBids_ = std::move(bids);
}

std::vector<AuctionDisplayItemEntry> AuctionDisplay::GetMyBids() const {
  return myBids_;
}

std::vector<AuctionDisplayItemEntry> AuctionDisplay::SortResults(
    AuctionDisplaySortColumn col, bool ascending) const {
  auto sorted = searchResults_.items;

  auto comparator = [col](const AuctionDisplayItemEntry& a,
                          const AuctionDisplayItemEntry& b) -> bool {
    switch (col) {
      case AuctionDisplaySortColumn::Quality:
        return a.quality < b.quality;
      case AuctionDisplaySortColumn::Level:
        return a.itemLevel < b.itemLevel;
      case AuctionDisplaySortColumn::Time:
        return static_cast<uint8_t>(a.timeLeft) <
               static_cast<uint8_t>(b.timeLeft);
      case AuctionDisplaySortColumn::Seller:
        return a.seller < b.seller;
      case AuctionDisplaySortColumn::CurrentBid:
        return a.currentBid < b.currentBid;
      case AuctionDisplaySortColumn::Buyout:
        return a.buyout < b.buyout;
      case AuctionDisplaySortColumn::Name:
        return a.itemName < b.itemName;
      default:
        return false;
    }
  };

  if (ascending) {
    std::sort(sorted.begin(), sorted.end(), comparator);
  } else {
    std::sort(sorted.begin(), sorted.end(),
              [&comparator](const AuctionDisplayItemEntry& a,
                            const AuctionDisplayItemEntry& b) {
                return comparator(b, a);
              });
  }

  return sorted;
}

std::size_t AuctionDisplay::GetResultCount() const {
  return searchResults_.items.size();
}

std::size_t AuctionDisplay::GetMyAuctionCount() const {
  return myAuctions_.size();
}

void AuctionDisplay::SetSearchText(const std::string& text) {
  searchText_ = text;
}

std::string AuctionDisplay::GetSearchText() const {
  return searchText_;
}

void AuctionDisplay::SetMinLevel(std::uint16_t level) {
  minLevel_ = level;
}

std::uint16_t AuctionDisplay::GetMinLevel() const {
  return minLevel_;
}

void AuctionDisplay::SetMaxLevel(std::uint16_t level) {
  maxLevel_ = level;
}

std::uint16_t AuctionDisplay::GetMaxLevel() const {
  return maxLevel_;
}

void AuctionDisplay::SetQualityFilter(std::uint8_t quality) {
  qualityFilter_ = quality;
}

std::uint8_t AuctionDisplay::GetQualityFilter() const {
  return qualityFilter_;
}

void AuctionDisplay::SetAuctionOpen(bool open) {
  auctionOpen_ = open;
}

bool AuctionDisplay::IsAuctionOpen() const {
  return auctionOpen_;
}

void AuctionDisplay::Reset() {
  searchResults_ = {};
  myAuctions_.clear();
  myBids_.clear();
  searchText_.clear();
  minLevel_ = 0;
  maxLevel_ = 0;
  qualityFilter_ = 0;
  auctionOpen_ = false;
}

}
