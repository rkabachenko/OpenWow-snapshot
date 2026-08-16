
#pragma once

#include "openwow/game/inventory/equipment/equipment_durability.h"

#include <cstdint>
#include <mutex>

namespace openwow::game {

enum class RepairDisplayMode : uint8_t {
    RepairAll    = 0,
    RepairSingle = 1,
};

struct RepairInteractionState {
    uint64_t npcGuid                   = 0;
    bool isOpen                        = false;
    bool canRepairAll                  = false;
    uint32_t totalCost                 = 0;
    uint32_t playerGold                = 0;
    bool useGuildBank                  = false;
    uint32_t guildBankRepairRemaining  = 0;
};

class RepairInteractionDisplay {
public:

    explicit RepairInteractionDisplay(EquipmentDurabilityTracker& tracker);

    void Open(uint64_t npcGuid, bool canRepairAll);

    void Close();

    [[nodiscard]] bool IsOpen() const;
    [[nodiscard]] RepairInteractionState GetState() const;

    void SetPlayerGold(uint32_t copper);

    void SetGuildBankRepairRemaining(uint32_t copper);

    void RequestRepairAll(bool useGuild);

    void RequestRepairSlot(EquipDurabilitySlot slot, bool useGuild);

    [[nodiscard]] bool CanAffordRepairAll() const;

    [[nodiscard]] bool CanAffordSlotRepair(EquipDurabilitySlot slot) const;

    [[nodiscard]] uint32_t GetSlotRepairCost(EquipDurabilitySlot slot) const;

    [[nodiscard]] RepairDisplayMode GetLastRequestMode() const;

    [[nodiscard]] EquipDurabilitySlot GetLastRequestSlot() const;

    [[nodiscard]] bool GetLastRequestUseGuild() const;

private:
    void RecalcTotalCost();

    EquipmentDurabilityTracker& tracker_;
    RepairInteractionState state_;

    RepairDisplayMode lastMode_  = RepairDisplayMode::RepairAll;
    EquipDurabilitySlot lastSlot_ = EquipDurabilitySlot::Head;
    bool lastUseGuild_           = false;
    bool hasRequest_             = false;

    mutable std::mutex mutex_;
};

}
