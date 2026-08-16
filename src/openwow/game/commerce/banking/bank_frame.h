#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class BankSlotType : std::uint8_t {
    Standard = 0,
    BagSlot  = 1,
    Reagent  = 2,
};

struct BankSlotDisplay {
    std::uint8_t  slot       = 0;
    std::uint32_t itemId     = 0;
    std::string   itemName;
    std::uint32_t iconId     = 0;
    std::uint32_t stackCount = 0;
    std::uint8_t  quality    = 0;
    bool          isLocked   = false;
};

inline constexpr std::uint8_t  kBankFrameStandardSlots = 28;
inline constexpr std::uint8_t  kBankFrameMaxBagSlots   = 7;

inline constexpr std::array<std::uint32_t, kBankFrameMaxBagSlots>
    kBankFrameBagCosts = {
        1000,
        10000,
        100000,
        250000,
        250000,
        250000,
        250000,
};

class BankFrame {
public:
    void SetBankSlots(std::vector<BankSlotDisplay> slots);
    [[nodiscard]] const std::vector<BankSlotDisplay>& GetBankSlots() const;
    [[nodiscard]] std::optional<BankSlotDisplay> GetBankSlot(std::uint8_t slot) const;
    [[nodiscard]] std::size_t GetBankSlotCount() const { return kBankFrameStandardSlots; }

    [[nodiscard]] std::uint8_t GetBankBagCount() const { return bankBagCount_; }
    void SetBankBagCount(std::uint8_t count);

    [[nodiscard]] std::uint32_t GetNextBankBagCost() const;

    void SetBankBagItem(std::uint8_t bagSlot, std::uint32_t itemId);
    [[nodiscard]] std::uint32_t GetBankBagItem(std::uint8_t bagSlot) const;

    [[nodiscard]] bool IsBankOpen() const { return bankOpen_; }
    void SetBankOpen(bool open) { bankOpen_ = open; }

    [[nodiscard]] std::uint32_t GetFreeBankSlots() const;
    void DepositToBank(std::uint8_t srcBag, std::uint8_t srcSlot);
    void WithdrawFromBank(std::uint8_t bankSlot);

    [[nodiscard]] bool HasPendingDeposit() const { return pendingDeposit_; }
    [[nodiscard]] std::uint8_t DepositSrcBag() const { return depositSrcBag_; }
    [[nodiscard]] std::uint8_t DepositSrcSlot() const { return depositSrcSlot_; }

    [[nodiscard]] bool HasPendingWithdraw() const { return pendingWithdraw_; }
    [[nodiscard]] std::uint8_t WithdrawBankSlot() const { return withdrawBankSlot_; }
    void ClearPendingDeposit();
    void ClearPendingWithdraw();

    [[nodiscard]] std::optional<std::uint8_t> FindFirstFreeSlot() const;
    [[nodiscard]] std::optional<BankSlotDisplay> FindItemById(std::uint32_t itemId) const;
    [[nodiscard]] std::uint32_t GetUsedSlotCount() const;
    [[nodiscard]] std::uint32_t GetTotalAvailableSlots() const;
    [[nodiscard]] bool IsSlotValid(std::uint8_t slot) const;
    [[nodiscard]] bool IsBankBagSlotPurchased(std::uint8_t bagSlot) const;
    void SortSlotsByQuality();
    void SortSlotsByName();

    void Reset();

private:
    std::vector<BankSlotDisplay>                          slots_;
    std::array<std::uint32_t, kBankFrameMaxBagSlots>      bankBagItems_{};
    std::uint8_t                                           bankBagCount_ = 0;
    bool                                                   bankOpen_     = false;

    bool         pendingDeposit_  = false;
    std::uint8_t depositSrcBag_  = 0;
    std::uint8_t depositSrcSlot_ = 0;

    bool         pendingWithdraw_  = false;
    std::uint8_t withdrawBankSlot_ = 0;
};

}
