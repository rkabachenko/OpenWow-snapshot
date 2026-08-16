
#include "openwow/game/inventory/equipment/equipment_durability.h"

#include <algorithm>

namespace openwow::game {

EquipmentDurabilityTracker::EquipmentDurabilityTracker() {
    for (uint8_t i = 0; i < kEquipDurabilitySlotCount; ++i) {
        slots_[i].slot = static_cast<EquipDurabilitySlot>(i);
        slots_[i].currentDurability = 0;
        slots_[i].maxDurability     = 0;
    }
}

void EquipmentDurabilityTracker::SetSlotDurability(EquipDurabilitySlot slot,
                                                    uint32_t current,
                                                    uint32_t max) {
    auto idx = static_cast<uint8_t>(slot);
    if (idx >= kEquipDurabilitySlotCount) return;
    slots_[idx].currentDurability = current;
    slots_[idx].maxDurability     = max;
}

DurabilityEntryInfo EquipmentDurabilityTracker::GetSlotDurability(
    EquipDurabilitySlot slot) const {
    auto idx = static_cast<uint8_t>(slot);
    if (idx >= kEquipDurabilitySlotCount) return {};
    return slots_[idx];
}

float EquipmentDurabilityTracker::GetOverallPercent() const {
    uint32_t totalCurrent = 0;
    uint32_t totalMax     = 0;
    for (uint8_t i = 0; i < kEquipDurabilitySlotCount; ++i) {
        if (slots_[i].maxDurability > 0) {
            totalCurrent += slots_[i].currentDurability;
            totalMax     += slots_[i].maxDurability;
        }
    }
    if (totalMax == 0) return 100.0f;
    return (static_cast<float>(totalCurrent) / static_cast<float>(totalMax)) * 100.0f;
}

DurabilityWarningLevel EquipmentDurabilityTracker::GetWarningLevel() const {
    DurabilityWarningLevel worst = DurabilityWarningLevel::None;
    for (uint8_t i = 0; i < kEquipDurabilitySlotCount; ++i) {
        if (slots_[i].maxDurability == 0) continue;
        auto level = WarningForSlot(slots_[i]);
        if (static_cast<uint8_t>(level) > static_cast<uint8_t>(worst)) {
            worst = level;
        }
    }
    return worst;
}

std::vector<EquipDurabilitySlot> EquipmentDurabilityTracker::GetBrokenSlots() const {
    std::vector<EquipDurabilitySlot> result;
    for (uint8_t i = 0; i < kEquipDurabilitySlotCount; ++i) {
        if (slots_[i].maxDurability > 0 && slots_[i].currentDurability == 0) {
            result.push_back(static_cast<EquipDurabilitySlot>(i));
        }
    }
    return result;
}

bool EquipmentDurabilityTracker::HasAnyDamage() const {
    for (uint8_t i = 0; i < kEquipDurabilitySlotCount; ++i) {
        if (slots_[i].maxDurability > 0 &&
            slots_[i].currentDurability < slots_[i].maxDurability) {
            return true;
        }
    }
    return false;
}

uint32_t EquipmentDurabilityTracker::GetRepairCost() const {
    uint32_t totalCost = 0;
    for (uint8_t i = 0; i < kEquipDurabilitySlotCount; ++i) {
        if (slots_[i].maxDurability > 0 &&
            slots_[i].currentDurability < slots_[i].maxDurability) {
            uint32_t damage = slots_[i].maxDurability - slots_[i].currentDurability;
            totalCost += damage * kDefaultCostPerPoint;
        }
    }
    return totalCost;
}

bool EquipmentDurabilityTracker::IsEquipped(EquipDurabilitySlot slot) const {
    auto idx = static_cast<uint8_t>(slot);
    if (idx >= kEquipDurabilitySlotCount) return false;
    return slots_[idx].maxDurability > 0;
}

DurabilityWarningLevel EquipmentDurabilityTracker::WarningForSlot(
    const DurabilityEntryInfo& info) const {
    if (info.maxDurability == 0) return DurabilityWarningLevel::None;
    if (info.currentDurability == 0) return DurabilityWarningLevel::Broken;
    float pct = static_cast<float>(info.currentDurability) /
                static_cast<float>(info.maxDurability) * 100.0f;
    if (pct <= 10.0f) return DurabilityWarningLevel::Critical;
    if (pct <= 25.0f) return DurabilityWarningLevel::Low;
    return DurabilityWarningLevel::None;
}

}
