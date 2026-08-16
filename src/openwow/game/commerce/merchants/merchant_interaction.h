#pragma once

#include "openwow/game/async_query_channel.h"
#include "openwow/game/object_guid.h"

#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

namespace openwow::game {

struct VendorItem {
  std::uint32_t slot = 0;
  std::uint32_t item_id = 0;
  std::uint32_t display_info_id = 0;
  std::int32_t max_count = -1;
  std::uint32_t price = 0;
  std::uint32_t max_durability = 0;
  std::uint32_t buy_count = 0;
  std::uint32_t extended_cost = 0;
};

struct VendorList {
  ObjectGuid vendor_guid;
  std::vector<VendorItem> items;
};

enum class VendorListResult : std::uint8_t {
  kItems = 0,
  kNoInventory = 1,
  kDisliked = 2,
  kTooFarAway = 3,
  kVendorDead = 4,
  kPlayerDead = 5,
  kUnknownFailure = 6,
};

class MerchantInteraction final {
 public:
  [[nodiscard]] bool active() const noexcept { return snapshot_.has_value(); }
  [[nodiscard]] const VendorList& snapshot() const { return snapshot_.value(); }
  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
  [[nodiscard]] VendorListResult last_list_result() const noexcept {
    return last_list_result_;
  }
  [[nodiscard]] const ObjectGuid& last_vendor_guid() const noexcept {
    return last_vendor_guid_;
  }

  void ObserveSnapshot(VendorList snapshot, VendorListResult result);
  void ObserveFailure(ObjectGuid vendor_guid, VendorListResult result);
  void Close();

  [[nodiscard]] bool UpdateItemCount(std::uint64_t vendor_guid,
                                     std::uint32_t vendor_slot,
                                     std::int32_t new_count);

  [[nodiscard]] static AsyncQueryChannel::CallbackKey
  ItemInfoRefreshCallbackKey();
  [[nodiscard]] bool BeginItemInfoRefresh(std::uint32_t item_id);
  [[nodiscard]] bool FinishItemInfoRefresh();
  void ResetItemInfoRefresh();

 private:
  std::optional<VendorList> snapshot_;
  std::uint64_t generation_{0};
  VendorListResult last_list_result_{VendorListResult::kUnknownFailure};
  ObjectGuid last_vendor_guid_{};
  std::unordered_set<std::uint32_t> pending_item_ids_;
  std::uint32_t pending_item_refreshes_{0};
};

}
