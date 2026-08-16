
#include "openwow/game/commerce/banking/bank_frame.h"

#include <algorithm>

namespace openwow::game {

void BankFrame::SetBankSlots(std::vector<BankSlotDisplay> slots) {

    if (slots.size() > kBankFrameStandardSlots) {
        slots.resize(kBankFrameStandardSlots);
    }
    slots_ = std::move(slots);
}

const std::vector<BankSlotDisplay>& BankFrame::GetBankSlots() const {
    return slots_;
}

std::optional<BankSlotDisplay> BankFrame::GetBankSlot(std::uint8_t slot) const {
    if (slot >= kBankFrameStandardSlots) return std::nullopt;
    for (const auto& s : slots_) {
        if (s.slot == slot) return s;
    }
    return std::nullopt;
}

void BankFrame::SetBankBagCount(std::uint8_t count) {
    bankBagCount_ = std::min(count, kBankFrameMaxBagSlots);
}

std::uint32_t BankFrame::GetNextBankBagCost() const {
    if (bankBagCount_ >= kBankFrameMaxBagSlots) return 0;
    return kBankFrameBagCosts[bankBagCount_];
}

void BankFrame::SetBankBagItem(std::uint8_t bagSlot, std::uint32_t itemId) {
    if (bagSlot < kBankFrameMaxBagSlots)
        bankBagItems_[bagSlot] = itemId;
}

std::uint32_t BankFrame::GetBankBagItem(std::uint8_t bagSlot) const {
    if (bagSlot < kBankFrameMaxBagSlots)
        return bankBagItems_[bagSlot];
    return 0;
}

bool BankFrame::IsBankBagSlotPurchased(std::uint8_t bagSlot) const {
    return bagSlot < bankBagCount_;
}

std::uint32_t BankFrame::GetFreeBankSlots() const {
    std::uint32_t used = 0;
    for (const auto& s : slots_) {
        if (s.itemId != 0) ++used;
    }
    if (used >= kBankFrameStandardSlots) return 0;
    return kBankFrameStandardSlots - used;
}

std::uint32_t BankFrame::GetUsedSlotCount() const {
    std::uint32_t used = 0;
    for (const auto& s : slots_) {
        if (s.itemId != 0) ++used;
    }
    return used;
}

std::uint32_t BankFrame::GetTotalAvailableSlots() const {

    return kBankFrameStandardSlots;
}

void BankFrame::DepositToBank(std::uint8_t srcBag, std::uint8_t srcSlot) {
    pendingDeposit_  = true;
    depositSrcBag_   = srcBag;
    depositSrcSlot_  = srcSlot;
}

void BankFrame::WithdrawFromBank(std::uint8_t bankSlot) {
    if (bankSlot >= kBankFrameStandardSlots) return;
    pendingWithdraw_  = true;
    withdrawBankSlot_ = bankSlot;
}

void BankFrame::ClearPendingDeposit() {
    pendingDeposit_ = false;
    depositSrcBag_  = 0;
    depositSrcSlot_ = 0;
}

void BankFrame::ClearPendingWithdraw() {
    pendingWithdraw_  = false;
    withdrawBankSlot_ = 0;
}

std::optional<std::uint8_t> BankFrame::FindFirstFreeSlot() const {

    bool occupied[kBankFrameStandardSlots] = {};
    for (const auto& s : slots_) {
        if (s.slot < kBankFrameStandardSlots && s.itemId != 0) {
            occupied[s.slot] = true;
        }
    }
    for (std::uint8_t i = 0; i < kBankFrameStandardSlots; ++i) {
        if (!occupied[i]) return i;
    }
    return std::nullopt;
}

std::optional<BankSlotDisplay> BankFrame::FindItemById(
    std::uint32_t itemId) const {
    if (itemId == 0) return std::nullopt;
    for (const auto& s : slots_) {
        if (s.itemId == itemId) return s;
    }
    return std::nullopt;
}

bool BankFrame::IsSlotValid(std::uint8_t slot) const {
    return slot < kBankFrameStandardSlots;
}

void BankFrame::SortSlotsByQuality() {

    std::sort(slots_.begin(), slots_.end(),
              [](const BankSlotDisplay& a, const BankSlotDisplay& b) {

                  bool aEmpty = (a.itemId == 0);
                  bool bEmpty = (b.itemId == 0);
                  if (aEmpty != bEmpty) return bEmpty;
                  return a.quality > b.quality;
              });
}

void BankFrame::SortSlotsByName() {
    std::sort(slots_.begin(), slots_.end(),
              [](const BankSlotDisplay& a, const BankSlotDisplay& b) {
                  bool aEmpty = (a.itemId == 0);
                  bool bEmpty = (b.itemId == 0);
                  if (aEmpty != bEmpty) return bEmpty;
                  return a.itemName < b.itemName;
              });
}

void BankFrame::Reset() {
    slots_.clear();
    bankBagItems_.fill(0);
    bankBagCount_     = 0;
    bankOpen_         = false;
    pendingDeposit_   = false;
    depositSrcBag_    = 0;
    depositSrcSlot_   = 0;
    pendingWithdraw_  = false;
    withdrawBankSlot_ = 0;
}

}
