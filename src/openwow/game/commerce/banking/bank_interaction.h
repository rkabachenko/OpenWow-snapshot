
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct BankSlot {
    uint32_t slotIndex = 0;
    uint32_t itemId    = 0;
    uint32_t itemCount = 0;
    uint32_t enchantId = 0;

    [[nodiscard]] bool isEmpty() const { return itemId == 0; }
};

struct BankBag {
    uint32_t bagSlotIndex = 0;
    uint32_t bagItemId    = 0;
    uint32_t numSlots     = 0;
};

inline constexpr uint32_t kBankBaseSlots   = 28;
inline constexpr uint32_t kBankMaxBagSlots = 7;

inline constexpr std::array<uint32_t, kBankMaxBagSlots> kBankBagSlotCosts = {
    10000,
    100000,
    250000,
    1000000,
    2500000,
    5000000,
    10000000,
};

class BankInteraction {
public:
    void Open(ObjectGuid bankerGuid);
    void Close();
    [[nodiscard]] bool IsOpen() const;
    [[nodiscard]] ObjectGuid GetBankerGuid() const;

    void SetSlot(uint32_t slot, uint32_t itemId, uint32_t count, uint32_t enchantId);
    [[nodiscard]] BankSlot GetSlot(uint32_t slot) const;
    [[nodiscard]] std::vector<BankSlot> GetAllSlots() const;
    [[nodiscard]] uint32_t GetUsedSlotCount() const;
    [[nodiscard]] static constexpr uint32_t GetTotalSlots() { return kBankBaseSlots; }
    [[nodiscard]] bool IsSlotEmpty(uint32_t slot) const;
    void ClearSlot(uint32_t slot);

    void SetBagSlot(uint32_t bagIndex, uint32_t bagItemId, uint32_t numSlots);
    [[nodiscard]] BankBag GetBagSlot(uint32_t bagIndex) const;
    [[nodiscard]] uint32_t GetPurchasedBagSlots() const;
    [[nodiscard]] static constexpr uint32_t GetMaxBagSlots() { return kBankMaxBagSlots; }
    [[nodiscard]] static uint32_t GetBagSlotCost(uint32_t slotIndex);

    [[nodiscard]] uint32_t GetTotalCapacity() const;
    [[nodiscard]] uint32_t GetFreeSlotCount() const;

    void Reset();

private:
    bool open_ = false;
    ObjectGuid bankerGuid_;
    std::array<BankSlot, kBankBaseSlots>   slots_{};
    std::array<BankBag,  kBankMaxBagSlots> bags_{};
};

}
