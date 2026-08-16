
#include "openwow/game/commerce/banking/bank_interaction.h"

namespace openwow::game {

void BankInteraction::Open(ObjectGuid bankerGuid) {
    open_ = true;
    bankerGuid_ = bankerGuid;
}

void BankInteraction::Close() {
    open_ = false;
    bankerGuid_ = ObjectGuid{};
}

bool BankInteraction::IsOpen() const {
    return open_;
}

ObjectGuid BankInteraction::GetBankerGuid() const {
    return bankerGuid_;
}

void BankInteraction::SetSlot(uint32_t slot, uint32_t itemId, uint32_t count, uint32_t enchantId) {
    if (slot >= kBankBaseSlots) return;
    slots_[slot] = {slot, itemId, count, enchantId};
}

BankSlot BankInteraction::GetSlot(uint32_t slot) const {
    if (slot >= kBankBaseSlots) return {};
    return slots_[slot];
}

std::vector<BankSlot> BankInteraction::GetAllSlots() const {
    return {slots_.begin(), slots_.end()};
}

uint32_t BankInteraction::GetUsedSlotCount() const {
    uint32_t count = 0;
    for (const auto& s : slots_) {
        if (!s.isEmpty()) ++count;
    }
    return count;
}

bool BankInteraction::IsSlotEmpty(uint32_t slot) const {
    if (slot >= kBankBaseSlots) return true;
    return slots_[slot].isEmpty();
}

void BankInteraction::ClearSlot(uint32_t slot) {
    if (slot >= kBankBaseSlots) return;
    slots_[slot] = {slot, 0, 0, 0};
}

void BankInteraction::SetBagSlot(uint32_t bagIndex, uint32_t bagItemId, uint32_t numSlots) {
    if (bagIndex >= kBankMaxBagSlots) return;
    bags_[bagIndex] = {bagIndex, bagItemId, numSlots};
}

BankBag BankInteraction::GetBagSlot(uint32_t bagIndex) const {
    if (bagIndex >= kBankMaxBagSlots) return {};
    return bags_[bagIndex];
}

uint32_t BankInteraction::GetPurchasedBagSlots() const {
    uint32_t count = 0;
    for (const auto& b : bags_) {
        if (b.bagItemId != 0) ++count;
    }
    return count;
}

uint32_t BankInteraction::GetBagSlotCost(uint32_t slotIndex) {
    if (slotIndex >= kBankMaxBagSlots) return 0;
    return kBankBagSlotCosts[slotIndex];
}

uint32_t BankInteraction::GetTotalCapacity() const {
    uint32_t total = kBankBaseSlots;
    for (const auto& b : bags_) {
        total += b.numSlots;
    }
    return total;
}

uint32_t BankInteraction::GetFreeSlotCount() const {
    uint32_t free = 0;
    for (const auto& s : slots_) {
        if (s.isEmpty()) ++free;
    }

    return free;
}

void BankInteraction::Reset() {
    open_ = false;
    bankerGuid_ = ObjectGuid{};
    slots_ = {};
    bags_ = {};
}

}
