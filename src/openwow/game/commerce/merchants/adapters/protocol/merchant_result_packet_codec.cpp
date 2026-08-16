#include "openwow/game/commerce/merchants/adapters/protocol/merchant_result_packet_codec.h"

#include "openwow/game/packet_reader.h"

namespace openwow::game {

int GetBuyFailSystemMessageIndex(const BuyFailReason reason) {
  switch (reason) {
    case BuyFailReason::kAlreadySold:
    case BuyFailReason::kItemAlreadySold:
      return 37;
    case BuyFailReason::kNotEnoughMoney:
      return 40;
    case BuyFailReason::kSellerDontLikeYou:
      return 36;
    case BuyFailReason::kDistanceTooFar:
      return 38;
    case BuyFailReason::kItemSoldOut:
      return -1;
    case BuyFailReason::kCantCarryMore:
      return 20;
    case BuyFailReason::kRankRequire:
      return 5;
    case BuyFailReason::kReputationRequire:
      return 7;
    default:
      return 25;
  }
}

const char* GetSellResultGlobalStringKey(const SellResult result) {
  switch (result) {
    case SellResult::kItemNotFound:
      return "ERR_ITEM_NOT_FOUND";
    case SellResult::kVendorNotInterested:
      return "ERR_VENDOR_NOT_INTERESTED";
    case SellResult::kVendorHatesYou:
      return "ERR_VENDOR_HATES_YOU";
    case SellResult::kNotOwner:
      return "ERR_NOT_OWNER";
    case SellResult::kOnlyEmptyBag:
      return "ERR_DESTROY_NONEMPTY_BAG";
    case SellResult::kVendorDoesNotBuy:
      return "ERR_VENDOR_DOESNT_BUY";
    case SellResult::kMustRepairDurability:
      return "ERR_MUST_REPAIR_DURABILITY";
    case SellResult::kInternalBagError:
      return "ERR_INTERNAL_BAG_ERROR";
    default:
      return nullptr;
  }
}

namespace merchant_protocol {

std::optional<BuyItemResult> DecodeBuyItem(
    const std::uint8_t* data, std::size_t size) {
  PacketReader reader(data, size);
  BuyItemResult result;
  std::uint32_t new_count = 0;
  if (!reader.ReadU64(result.vendor_guid) ||
      !reader.ReadU32(result.vendor_slot) ||
      !reader.ReadU32(new_count) ||
      !reader.ReadU32(result.bought_count)) {
    return std::nullopt;
  }
  result.new_count = static_cast<std::int32_t>(new_count);
  return result;
}

std::optional<SellItemResult> DecodeSellItem(
    const std::uint8_t* data, std::size_t size) {
  PacketReader reader(data, size);
  SellItemResult result;
  std::uint8_t wire_result = 0;
  if (!reader.ReadU64(result.vendor_guid) ||
      !reader.ReadU64(result.item_guid) ||
      !reader.ReadU8(wire_result)) {
    return std::nullopt;
  }
  result.result = static_cast<SellResult>(wire_result);
  return result;
}

std::optional<BuyFailResult> DecodeBuyFailed(
    const std::uint8_t* data, std::size_t size) {
  PacketReader reader(data, size);
  BuyFailResult result;
  std::uint8_t wire_reason = 0;
  if (!reader.ReadU64(result.vendor_guid) ||
      !reader.ReadU32(result.item_id) ||
      !reader.ReadU8(wire_reason)) {
    return std::nullopt;
  }
  result.reason = static_cast<BuyFailReason>(wire_reason);
  return result;
}

}
}
