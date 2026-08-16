
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

enum class SpecSlotIndex : uint8_t {
    Primary   = 0,
    Secondary = 1,
};

struct SpecSlotInfo {
    SpecSlotIndex            slotIndex       = SpecSlotIndex::Primary;
    std::string              name;
    std::array<uint8_t, 3>   talentPoints    = {0, 0, 0};
    uint8_t                  totalPointsSpent = 0;
    uint32_t                 iconId          = 0;
    bool                     isPurchased     = true;
};

inline constexpr uint32_t kDualSpecPurchaseCost = 10000000;

class DualSpecUI {
public:
    void SetSpec(const SpecSlotInfo& info);
    [[nodiscard]] std::optional<SpecSlotInfo> GetSpec(SpecSlotIndex slot) const;

    [[nodiscard]] SpecSlotIndex GetActiveSpec() const;
    void SetActiveSpec(SpecSlotIndex slot);

    [[nodiscard]] bool CanSwitch() const;
    void SetInCombat(bool inCombat);
    void SetInBGQueue(bool inQueue);
    bool SwitchSpec();

    [[nodiscard]] bool     IsSecondSpecPurchased() const;
    void                   PurchaseSecondSpec();
    [[nodiscard]] uint32_t GetPurchaseCost() const;
    void                   SetPlayerGold(uint32_t copper);
    [[nodiscard]] bool     CanAffordPurchase() const;

    [[nodiscard]] std::string GetSpecName(SpecSlotIndex slot) const;
    void SetSpecName(SpecSlotIndex slot, const std::string& name);

    [[nodiscard]] uint8_t GetTotalPointsForSpec(SpecSlotIndex slot) const;
    [[nodiscard]] std::string GetActiveSpecName() const;

private:
    [[nodiscard]] static uint8_t Idx(SpecSlotIndex s) { return static_cast<uint8_t>(s); }

    SpecSlotInfo   specs_[2]   = {
        {SpecSlotIndex::Primary,   "Primary",   {0, 0, 0}, 0, 0, true},
        {SpecSlotIndex::Secondary, "Secondary", {0, 0, 0}, 0, 0, false},
    };
    SpecSlotIndex  activeSpec_       = SpecSlotIndex::Primary;
    bool           secondPurchased_  = false;
    bool           inCombat_         = false;
    bool           inBGQueue_        = false;
    uint32_t       playerGold_       = 0;
};

}
