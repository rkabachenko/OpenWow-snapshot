
#include "openwow/game/dual_spec_ui.h"

namespace openwow::game {

void DualSpecUI::SetSpec(const SpecSlotInfo& info) {
    const auto i = Idx(info.slotIndex);
    if (i < 2) specs_[i] = info;
}

std::optional<SpecSlotInfo> DualSpecUI::GetSpec(SpecSlotIndex slot) const {
    const auto i = Idx(slot);
    if (i >= 2) return std::nullopt;
    return specs_[i];
}

SpecSlotIndex DualSpecUI::GetActiveSpec() const {
    return activeSpec_;
}

void DualSpecUI::SetActiveSpec(SpecSlotIndex slot) {
    activeSpec_ = slot;
}

bool DualSpecUI::CanSwitch() const {
    return secondPurchased_ && !inCombat_ && !inBGQueue_;
}

void DualSpecUI::SetInCombat(bool inCombat) {
    inCombat_ = inCombat;
}

void DualSpecUI::SetInBGQueue(bool inQueue) {
    inBGQueue_ = inQueue;
}

bool DualSpecUI::SwitchSpec() {
    if (!CanSwitch()) return false;
    activeSpec_ = (activeSpec_ == SpecSlotIndex::Primary)
                      ? SpecSlotIndex::Secondary
                      : SpecSlotIndex::Primary;
    return true;
}

bool DualSpecUI::IsSecondSpecPurchased() const {
    return secondPurchased_;
}

void DualSpecUI::PurchaseSecondSpec() {
    secondPurchased_ = true;
    specs_[1].isPurchased = true;
}

uint32_t DualSpecUI::GetPurchaseCost() const {
    return kDualSpecPurchaseCost;
}

void DualSpecUI::SetPlayerGold(uint32_t copper) {
    playerGold_ = copper;
}

bool DualSpecUI::CanAffordPurchase() const {
    return playerGold_ >= kDualSpecPurchaseCost;
}

std::string DualSpecUI::GetSpecName(SpecSlotIndex slot) const {
    const auto i = Idx(slot);
    if (i >= 2) return {};
    return specs_[i].name;
}

void DualSpecUI::SetSpecName(SpecSlotIndex slot, const std::string& name) {
    const auto i = Idx(slot);
    if (i < 2) specs_[i].name = name;
}

uint8_t DualSpecUI::GetTotalPointsForSpec(SpecSlotIndex slot) const {
    const auto i = Idx(slot);
    if (i >= 2) return 0;
    return specs_[i].totalPointsSpent;
}

std::string DualSpecUI::GetActiveSpecName() const {
    return specs_[Idx(activeSpec_)].name;
}

}
