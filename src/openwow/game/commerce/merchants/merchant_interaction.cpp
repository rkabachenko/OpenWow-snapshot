#include "openwow/game/commerce/merchants/merchant_interaction.h"

#include <utility>

namespace openwow::game {

void MerchantInteraction::ObserveSnapshot(VendorList snapshot,
                                          const VendorListResult result) {
  last_vendor_guid_ = snapshot.vendor_guid;
  last_list_result_ = result;
  ++generation_;
  snapshot_ = std::move(snapshot);
}

void MerchantInteraction::ObserveFailure(const ObjectGuid vendor_guid,
                                         const VendorListResult result) {
  last_vendor_guid_ = vendor_guid;
  last_list_result_ = result;
}

void MerchantInteraction::Close() {
  if (snapshot_.has_value()) {
    ++generation_;
  }
  snapshot_.reset();
  ResetItemInfoRefresh();
}

bool MerchantInteraction::UpdateItemCount(const std::uint64_t vendor_guid,
                                          const std::uint32_t vendor_slot,
                                          const std::int32_t new_count) {
  if (!snapshot_.has_value() ||
      snapshot_->vendor_guid.GetRawValue() != vendor_guid) {
    return false;
  }
  for (auto& item : snapshot_->items) {
    if (item.slot == vendor_slot) {
      item.max_count = new_count;
      return true;
    }
  }
  return true;
}

AsyncQueryChannel::CallbackKey
MerchantInteraction::ItemInfoRefreshCallbackKey() {
  static const char tag = 0;
  return AsyncQueryChannel::CallbackKey(
      reinterpret_cast<std::uintptr_t>(&tag), 0);
}

bool MerchantInteraction::BeginItemInfoRefresh(const std::uint32_t item_id) {
  if (item_id == 0) {
    return false;
  }
  const auto [_, inserted] = pending_item_ids_.insert(item_id);
  if (inserted) {
    ++pending_item_refreshes_;
  }
  return inserted;
}

bool MerchantInteraction::FinishItemInfoRefresh() {
  if (pending_item_refreshes_ != 0) {
    --pending_item_refreshes_;
  }
  return pending_item_refreshes_ == 0 && snapshot_.has_value() &&
         !snapshot_->vendor_guid.IsEmpty();
}

void MerchantInteraction::ResetItemInfoRefresh() {
  pending_item_ids_.clear();
  pending_item_refreshes_ = 0;
}

}
