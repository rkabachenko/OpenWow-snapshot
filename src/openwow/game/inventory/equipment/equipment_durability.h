
#pragma once

#include <cstdint>
#include <vector>

namespace openwow::game {

enum class EquipDurabilitySlot : uint8_t {
    Head     = 0,
    Neck     = 1,
    Shoulder = 2,
    Chest    = 3,
    Waist    = 4,
    Legs     = 5,
    Feet     = 6,
    Wrist    = 7,
    Hands    = 8,
    MainHand = 9,
    OffHand  = 10,
    Ranged   = 11,
    Tabard   = 12,
    Back     = 13,
    Shirt    = 14,
};

static constexpr uint8_t kEquipDurabilitySlotCount = 15;

struct DurabilityEntryInfo {
    EquipDurabilitySlot slot = EquipDurabilitySlot::Head;
    uint32_t currentDurability = 0;
    uint32_t maxDurability     = 0;
};

enum class DurabilityWarningLevel : uint8_t {
    None     = 0,
    Low      = 1,
    Critical = 2,
    Broken   = 3,
};

class EquipmentDurabilityTracker {
public:
    static constexpr uint32_t kDefaultCostPerPoint = 10;

    EquipmentDurabilityTracker();

    void SetSlotDurability(EquipDurabilitySlot slot, uint32_t current, uint32_t max);

    [[nodiscard]] DurabilityEntryInfo GetSlotDurability(EquipDurabilitySlot slot) const;

    [[nodiscard]] float GetOverallPercent() const;

    [[nodiscard]] DurabilityWarningLevel GetWarningLevel() const;

    [[nodiscard]] std::vector<EquipDurabilitySlot> GetBrokenSlots() const;

    [[nodiscard]] bool HasAnyDamage() const;

    [[nodiscard]] uint32_t GetRepairCost() const;

    [[nodiscard]] bool IsEquipped(EquipDurabilitySlot slot) const;

private:
    [[nodiscard]] DurabilityWarningLevel WarningForSlot(const DurabilityEntryInfo& info) const;

    DurabilityEntryInfo slots_[kEquipDurabilitySlotCount];
};

}
