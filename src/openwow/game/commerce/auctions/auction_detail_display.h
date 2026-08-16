
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class AuctionSortType : uint8_t {
    ByName   = 0,
    ByLevel  = 1,
    ByBid    = 2,
    ByBuyout = 3,
    ByTime   = 4,
    BySeller = 5,
};

enum class AuctionTimeLeft : uint8_t {
    Short    = 0,
    Medium   = 1,
    Long     = 2,
    VeryLong = 3,
};

struct AuctionListingInfo {
    uint32_t    auctionId  = 0;
    uint32_t    itemId     = 0;
    std::string itemName;
    uint32_t    quantity   = 0;
    uint8_t     quality    = 0;
    uint16_t    level      = 0;
    uint32_t    currentBid = 0;
    uint32_t    buyout     = 0;
    std::string sellerName;
    uint8_t     timeLeft   = 0;
    uint32_t    iconId     = 0;
};

inline constexpr uint32_t kAuctionDetailMaxResults  = 50;

class AuctionDetailDisplay {
public:
    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const;

    void SetListings(const std::vector<AuctionListingInfo>& listings);
    [[nodiscard]] const std::vector<AuctionListingInfo>& GetListings() const;
    [[nodiscard]] uint32_t GetListingCount() const;

    void SortBy(AuctionSortType sortType, bool ascending);

    [[nodiscard]] std::vector<AuctionListingInfo>
        SearchByName(const std::string& query) const;
    [[nodiscard]] std::vector<AuctionListingInfo>
        FilterByQuality(uint8_t minQuality) const;
    [[nodiscard]] std::vector<AuctionListingInfo>
        FilterByLevel(uint16_t minLevel, uint16_t maxLevel) const;

    [[nodiscard]] std::optional<AuctionListingInfo>
        GetListing(uint32_t auctionId) const;
    void SetSelectedListing(uint32_t auctionId);
    [[nodiscard]] std::optional<AuctionListingInfo>
        GetSelectedListing() const;

    [[nodiscard]] bool CanBid(uint32_t auctionId, uint32_t bidAmount,
                              uint32_t playerGold) const;
    [[nodiscard]] bool CanBuyout(uint32_t auctionId,
                                 uint32_t playerGold) const;
    [[nodiscard]] uint32_t GetMinBidIncrement(uint32_t auctionId) const;

    void SetPlayerGold(uint32_t copper);
    [[nodiscard]] uint32_t GetPlayerGold() const;

    [[nodiscard]] static std::string GetTimeLeftString(uint8_t timeLeft);

private:
    [[nodiscard]] const AuctionListingInfo*
        FindListing(uint32_t auctionId) const;

    std::vector<AuctionListingInfo> listings_;
    uint32_t selectedAuctionId_ = 0;
    uint32_t playerGold_        = 0;
    bool     open_              = false;
};

}
