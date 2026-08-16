#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace openwow::game {

struct BuyItemResult {
  std::uint64_t vendor_guid = 0;
  std::uint32_t vendor_slot = 0;
  std::int32_t new_count = 0;
  std::uint32_t bought_count = 0;
};

enum class SellResult : std::uint8_t {
  kOk = 0,
  kItemNotFound = 1,
  kVendorNotInterested = 2,
  kVendorHatesYou = 3,
  kNotOwner = 4,
  kUnhandled5 = 5,
  kOnlyEmptyBag = 6,
  kVendorDoesNotBuy = 7,
  kMustRepairDurability = 8,
  kInternalBagError = 9,
};

struct SellItemResult {
  std::uint64_t vendor_guid = 0;
  std::uint64_t item_guid = 0;
  SellResult result = SellResult::kOk;
};

enum class BuyFailReason : std::uint8_t {
  kCantFindItem = 0,
  kAlreadySold = 1,
  kNotEnoughMoney = 2,
  kSellerDontLikeYou = 4,
  kDistanceTooFar = 5,
  kItemSoldOut = 6,
  kItemAlreadySold = 7,
  kCantCarryMore = 8,
  kRankRequire = 11,
  kReputationRequire = 12,
};

struct BuyFailResult {
  std::uint64_t vendor_guid = 0;
  std::uint32_t item_id = 0;
  BuyFailReason reason = BuyFailReason::kCantFindItem;
};

[[nodiscard]] const char* GetSellResultGlobalStringKey(SellResult result);
[[nodiscard]] int GetBuyFailSystemMessageIndex(BuyFailReason reason);

namespace merchant_protocol {

[[nodiscard]] std::optional<BuyItemResult> DecodeBuyItem(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<SellItemResult> DecodeSellItem(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<BuyFailResult> DecodeBuyFailed(
    const std::uint8_t* data, std::size_t size);

}
}
