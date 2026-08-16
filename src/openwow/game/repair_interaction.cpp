
#include "openwow/game/repair_interaction.h"

namespace openwow::game {

RepairInteractionDisplay::RepairInteractionDisplay(
    EquipmentDurabilityTracker& tracker)
    : tracker_(tracker) {}

void RepairInteractionDisplay::Open(uint64_t npcGuid, bool canRepairAll) {
    std::lock_guard lock(mutex_);
    state_.npcGuid      = npcGuid;
    state_.isOpen       = true;
    state_.canRepairAll = canRepairAll;
    state_.useGuildBank = false;
    hasRequest_         = false;
    RecalcTotalCost();
}

void RepairInteractionDisplay::Close() {
    std::lock_guard lock(mutex_);
    state_.isOpen  = false;
    state_.npcGuid = 0;
    hasRequest_    = false;
}

bool RepairInteractionDisplay::IsOpen() const {
    std::lock_guard lock(mutex_);
    return state_.isOpen;
}

RepairInteractionState RepairInteractionDisplay::GetState() const {
    std::lock_guard lock(mutex_);

    auto self = const_cast<RepairInteractionDisplay*>(this);
    self->RecalcTotalCost();
    return state_;
}

void RepairInteractionDisplay::SetPlayerGold(uint32_t copper) {
    std::lock_guard lock(mutex_);
    state_.playerGold = copper;
}

void RepairInteractionDisplay::SetGuildBankRepairRemaining(uint32_t copper) {
    std::lock_guard lock(mutex_);
    state_.guildBankRepairRemaining = copper;
}

void RepairInteractionDisplay::RequestRepairAll(bool useGuild) {
    std::lock_guard lock(mutex_);
    state_.useGuildBank = useGuild;
    lastMode_     = RepairDisplayMode::RepairAll;
    lastUseGuild_ = useGuild;
    hasRequest_   = true;
}

void RepairInteractionDisplay::RequestRepairSlot(EquipDurabilitySlot slot,
                                                  bool useGuild) {
    std::lock_guard lock(mutex_);
    state_.useGuildBank = useGuild;
    lastMode_     = RepairDisplayMode::RepairSingle;
    lastSlot_     = slot;
    lastUseGuild_ = useGuild;
    hasRequest_   = true;
}

bool RepairInteractionDisplay::CanAffordRepairAll() const {
    std::lock_guard lock(mutex_);
    uint32_t cost = tracker_.GetRepairCost();
    uint32_t available = state_.playerGold;
    if (state_.useGuildBank) {
        available += state_.guildBankRepairRemaining;
    }
    return available >= cost;
}

bool RepairInteractionDisplay::CanAffordSlotRepair(
    EquipDurabilitySlot slot) const {
    std::lock_guard lock(mutex_);
    auto info = tracker_.GetSlotDurability(slot);
    if (info.maxDurability == 0) return true;
    uint32_t damage = info.maxDurability - info.currentDurability;
    uint32_t cost = damage * EquipmentDurabilityTracker::kDefaultCostPerPoint;
    uint32_t available = state_.playerGold;
    if (state_.useGuildBank) {
        available += state_.guildBankRepairRemaining;
    }
    return available >= cost;
}

uint32_t RepairInteractionDisplay::GetSlotRepairCost(
    EquipDurabilitySlot slot) const {
    std::lock_guard lock(mutex_);
    auto info = tracker_.GetSlotDurability(slot);
    if (info.maxDurability == 0) return 0;
    uint32_t damage = info.maxDurability - info.currentDurability;
    return damage * EquipmentDurabilityTracker::kDefaultCostPerPoint;
}

RepairDisplayMode RepairInteractionDisplay::GetLastRequestMode() const {
    std::lock_guard lock(mutex_);
    return lastMode_;
}

EquipDurabilitySlot RepairInteractionDisplay::GetLastRequestSlot() const {
    std::lock_guard lock(mutex_);
    return lastSlot_;
}

bool RepairInteractionDisplay::GetLastRequestUseGuild() const {
    std::lock_guard lock(mutex_);
    return lastUseGuild_;
}

void RepairInteractionDisplay::RecalcTotalCost() {

    state_.totalCost = tracker_.GetRepairCost();
}

}
