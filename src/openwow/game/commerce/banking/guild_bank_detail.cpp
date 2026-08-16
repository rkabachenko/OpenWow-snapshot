
#include "openwow/game/commerce/banking/guild_bank_detail.h"

#include <algorithm>

namespace openwow::game {

void GuildBankDetail::Open()  { open_ = true; }
void GuildBankDetail::Close() {
    open_ = false;
    tabs_.clear();
    for (auto& ts : tabSlots_) ts.slots.clear();
    for (auto& tl : tabLogs_) tl.clear();
    moneyLog_.clear();
    guildMoney_ = 0;
    activeTab_  = 0;
}
bool GuildBankDetail::IsOpen() const { return open_; }

void GuildBankDetail::SetTabs(
    const std::vector<GuildBankTabDetailInfo>& tabs) {
    if (tabs.size() > kGuildBankDetailMaxTabs) {
        tabs_.assign(tabs.begin(), tabs.begin() + kGuildBankDetailMaxTabs);
    } else {
        tabs_ = tabs;
    }
}

const std::vector<GuildBankTabDetailInfo>& GuildBankDetail::GetTabs() const {
    return tabs_;
}

std::optional<GuildBankTabDetailInfo>
GuildBankDetail::GetTab(uint8_t index) const {
    for (const auto& t : tabs_) {
        if (t.tabIndex == index) return t;
    }
    return std::nullopt;
}

void    GuildBankDetail::SetActiveTab(uint8_t index) { activeTab_ = index; }
uint8_t GuildBankDetail::GetActiveTab() const { return activeTab_; }

void GuildBankDetail::SetSlots(uint8_t tabIndex,
                               const std::vector<GuildBankSlotInfo>& slots) {
    if (tabIndex >= kGuildBankDetailMaxTabs) return;
    auto& ts = tabSlots_[tabIndex];
    if (slots.size() > kGuildBankDetailMaxSlotsPerTab) {
        ts.slots.assign(slots.begin(),
                        slots.begin() + kGuildBankDetailMaxSlotsPerTab);
    } else {
        ts.slots = slots;
    }
}

std::vector<GuildBankSlotInfo>
GuildBankDetail::GetSlots(uint8_t tabIndex) const {
    if (tabIndex >= kGuildBankDetailMaxTabs) return {};
    return tabSlots_[tabIndex].slots;
}

std::optional<GuildBankSlotInfo>
GuildBankDetail::GetSlot(uint8_t tabIndex, uint8_t slotIndex) const {
    if (tabIndex >= kGuildBankDetailMaxTabs) return std::nullopt;
    for (const auto& s : tabSlots_[tabIndex].slots) {
        if (s.slotIndex == slotIndex) return s;
    }
    return std::nullopt;
}

bool GuildBankDetail::IsSlotEmpty(uint8_t tabIndex,
                                  uint8_t slotIndex) const {
    return !GetSlot(tabIndex, slotIndex).has_value();
}

uint32_t GuildBankDetail::GetUsedSlotCount(uint8_t tabIndex) const {
    if (tabIndex >= kGuildBankDetailMaxTabs) return 0;
    return static_cast<uint32_t>(tabSlots_[tabIndex].slots.size());
}

uint32_t GuildBankDetail::GetFreeSlotCount(uint8_t tabIndex) const {
    return kGuildBankDetailMaxSlotsPerTab - GetUsedSlotCount(tabIndex);
}

uint64_t GuildBankDetail::GetGuildMoney() const { return guildMoney_; }
void GuildBankDetail::SetGuildMoney(uint64_t copper) { guildMoney_ = copper; }

void GuildBankDetail::AddLogEntry(const GuildBankLogDetailEntry& entry) {

    if (entry.type == 3 || (entry.itemId == 0 && entry.money > 0)) {
        moneyLog_.push_back(entry);
        if (moneyLog_.size() > kGuildBankDetailMaxLogEntries) {
            moneyLog_.erase(moneyLog_.begin());
        }
    }

    uint8_t tab = activeTab_;
    if (tab < kGuildBankDetailMaxTabs) {
        tabLogs_[tab].push_back(entry);
        if (tabLogs_[tab].size() > kGuildBankDetailMaxLogEntries) {
            tabLogs_[tab].erase(tabLogs_[tab].begin());
        }
    }
}

std::vector<GuildBankLogDetailEntry>
GuildBankDetail::GetLog(uint8_t tabIndex) const {
    if (tabIndex >= kGuildBankDetailMaxTabs) return {};
    return tabLogs_[tabIndex];
}

std::vector<GuildBankLogDetailEntry>
GuildBankDetail::GetMoneyLog() const {
    return moneyLog_;
}

bool GuildBankDetail::CanWithdraw(uint8_t tabIndex) const {
    auto tab = GetTab(tabIndex);
    if (!tab) return false;
    return tab->canWithdraw && tab->withdrawsRemaining > 0;
}

uint32_t GuildBankDetail::GetWithdrawsRemaining(uint8_t tabIndex) const {
    auto tab = GetTab(tabIndex);
    if (!tab) return 0;
    return tab->withdrawsRemaining;
}

}
