
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class AuctionDisplayDuration : std::uint8_t {
  Short = 1,
  Medium = 2,
  Long = 3,
};

enum class AuctionDisplaySortColumn : std::uint8_t {
  Quality = 0,
  Level = 1,
  Time = 2,
  Seller = 3,
  CurrentBid = 4,
  Buyout = 5,
  Name = 6,
};

struct AuctionDisplayItemEntry {
  std::uint32_t auctionId = 0;
  std::uint32_t itemId = 0;
  std::string itemName;
  std::uint32_t iconId = 0;
  std::uint8_t quality = 0;
  std::uint16_t itemLevel = 0;
  std::uint32_t stackCount = 0;
  std::uint32_t currentBid = 0;
  std::uint32_t buyout = 0;
  std::uint32_t minIncrement = 0;
  std::string seller;
  AuctionDisplayDuration timeLeft = AuctionDisplayDuration::Short;
};

struct AuctionDisplayListResult {
  std::vector<AuctionDisplayItemEntry> items;
  std::uint32_t totalCount = 0;
  std::uint32_t searchDelay = 0;
};

class AuctionDisplay {
 public:
  void SetSearchResults(AuctionDisplayListResult results);
  [[nodiscard]] AuctionDisplayListResult GetSearchResults() const;

  void SetMyAuctions(std::vector<AuctionDisplayItemEntry> auctions);
  [[nodiscard]] std::vector<AuctionDisplayItemEntry> GetMyAuctions() const;

  void SetMyBids(std::vector<AuctionDisplayItemEntry> bids);
  [[nodiscard]] std::vector<AuctionDisplayItemEntry> GetMyBids() const;

  [[nodiscard]] std::vector<AuctionDisplayItemEntry> SortResults(
      AuctionDisplaySortColumn col, bool ascending) const;

  [[nodiscard]] std::size_t GetResultCount() const;
  [[nodiscard]] std::size_t GetMyAuctionCount() const;

  void SetSearchText(const std::string& text);
  [[nodiscard]] std::string GetSearchText() const;
  void SetMinLevel(std::uint16_t level);
  [[nodiscard]] std::uint16_t GetMinLevel() const;
  void SetMaxLevel(std::uint16_t level);
  [[nodiscard]] std::uint16_t GetMaxLevel() const;
  void SetQualityFilter(std::uint8_t quality);
  [[nodiscard]] std::uint8_t GetQualityFilter() const;

  void SetAuctionOpen(bool open);
  [[nodiscard]] bool IsAuctionOpen() const;

  void Reset();

 private:
  AuctionDisplayListResult searchResults_;
  std::vector<AuctionDisplayItemEntry> myAuctions_;
  std::vector<AuctionDisplayItemEntry> myBids_;
  std::string searchText_;
  std::uint16_t minLevel_ = 0;
  std::uint16_t maxLevel_ = 0;
  std::uint8_t qualityFilter_ = 0;
  bool auctionOpen_ = false;
};

}
