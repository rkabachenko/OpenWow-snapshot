
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct GuildBankTabDetailInfo {
    uint8_t     tabIndex          = 0;
    std::string name;
    uint32_t    iconId            = 0;
    bool        canView           = false;
    bool        canDeposit        = false;
    bool        canWithdraw       = false;
    uint32_t    withdrawsRemaining = 0;
};

struct GuildBankSlotInfo {
    uint8_t     tabIndex   = 0;
    uint8_t     slotIndex  = 0;
    uint32_t    itemId     = 0;
    std::string name;
    uint32_t    quantity   = 0;
    uint8_t     quality    = 0;
    uint32_t    iconId     = 0;
    uint32_t    enchantId  = 0;
};

struct GuildBankLogDetailEntry {
    uint8_t     type       = 0;
    std::string playerName;
    uint32_t    itemId     = 0;
    uint32_t    quantity   = 0;
    uint32_t    money      = 0;
    uint32_t    timestamp  = 0;
};

inline constexpr uint8_t  kGuildBankDetailMaxTabs       = 6;
inline constexpr uint8_t  kGuildBankDetailMaxSlotsPerTab = 98;
inline constexpr uint32_t kGuildBankDetailMaxLogEntries  = 25;

class GuildBankDetail {
public:
    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const;

    void SetTabs(const std::vector<GuildBankTabDetailInfo>& tabs);
    [[nodiscard]] const std::vector<GuildBankTabDetailInfo>& GetTabs() const;
    [[nodiscard]] std::optional<GuildBankTabDetailInfo>
        GetTab(uint8_t index) const;
    void    SetActiveTab(uint8_t index);
    [[nodiscard]] uint8_t GetActiveTab() const;

    void SetSlots(uint8_t tabIndex,
                  const std::vector<GuildBankSlotInfo>& slots);
    [[nodiscard]] std::vector<GuildBankSlotInfo>
        GetSlots(uint8_t tabIndex) const;
    [[nodiscard]] std::optional<GuildBankSlotInfo>
        GetSlot(uint8_t tabIndex, uint8_t slotIndex) const;
    [[nodiscard]] bool     IsSlotEmpty(uint8_t tabIndex,
                                       uint8_t slotIndex) const;
    [[nodiscard]] uint32_t GetUsedSlotCount(uint8_t tabIndex) const;
    [[nodiscard]] uint32_t GetFreeSlotCount(uint8_t tabIndex) const;

    [[nodiscard]] uint64_t GetGuildMoney() const;
    void SetGuildMoney(uint64_t copper);

    void AddLogEntry(const GuildBankLogDetailEntry& entry);
    [[nodiscard]] std::vector<GuildBankLogDetailEntry>
        GetLog(uint8_t tabIndex) const;
    [[nodiscard]] std::vector<GuildBankLogDetailEntry> GetMoneyLog() const;

    [[nodiscard]] bool     CanWithdraw(uint8_t tabIndex) const;
    [[nodiscard]] uint32_t GetWithdrawsRemaining(uint8_t tabIndex) const;

private:
    struct TabSlots {
        std::vector<GuildBankSlotInfo> slots;
    };

    std::vector<GuildBankTabDetailInfo>  tabs_;
    TabSlots                             tabSlots_[kGuildBankDetailMaxTabs] = {};
    std::vector<GuildBankLogDetailEntry> tabLogs_[kGuildBankDetailMaxTabs];
    std::vector<GuildBankLogDetailEntry> moneyLog_;
    uint64_t guildMoney_ = 0;
    uint8_t  activeTab_  = 0;
    bool     open_       = false;
};

}
