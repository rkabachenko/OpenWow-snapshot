
#pragma once

#include <cstdint>
#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/commerce/banking/adapters/protocol/guild_bank_protocol_session_state.h"
#include "openwow/game/inventory/player_inventory_replica.h"

namespace openwow::game {

struct GuildRank {
    uint32_t id = 0;
    std::string name;
    uint32_t rights = 0;
    uint32_t money_per_day = 0;
    std::array<uint32_t, 6> bank_tab_flags = {};
    std::array<uint32_t, 6> bank_tab_withdraw_item_limits = {};
};

struct GuildSystemMember {
    uint64_t guid = 0;
    std::string name;
    uint32_t rank_index = 0;
    uint32_t level = 0;
    uint32_t class_id = 0;
    uint32_t gender_id = 0;
    uint32_t area_id = 0;
    float last_online = 0.0f;
    std::string public_note;
    std::string officer_note;
    std::uint8_t status = 0;
    bool is_online = false;
};

struct GuildBankTab {
    std::string name;
    std::string icon;
    std::vector<ItemInstance> items;
    std::vector<bool> item_locks;
};

class GuildSystem {
 public:
    static constexpr std::size_t kGuildBankMaxTabs = 6;
    static constexpr std::size_t kGuildBankSlotsPerTab = 98;
    static constexpr std::size_t kGuildBankLogPages = openwow::game::kGuildBankLogPages;
    static constexpr std::size_t kGuildBankMoneyLogPage = openwow::game::kGuildBankMoneyLogPage;
    static constexpr std::size_t kGuildBankLogEntriesPerPage =
        openwow::game::kGuildBankLogEntriesPerPage;

    static GuildSystem& Get();

    void SetGuildInfo(uint32_t guildId, const std::string& name,
                      const std::string& motd, const std::string& info);
    void SetGuildIdentity(uint32_t guildId, const std::string& name);
    void SetGuildMOTD(const std::string& motd);
    [[nodiscard]] uint32_t GetGuildId() const;
    [[nodiscard]] const std::string& GetGuildName() const;
    [[nodiscard]] const std::string& GetGuildMOTD() const;
    [[nodiscard]] const std::string& GetGuildInfo() const;
    [[nodiscard]] bool IsInGuild() const;

    void SetRanks(const std::vector<GuildRank>& ranks);
    [[nodiscard]] size_t GetNumRanks() const;
    [[nodiscard]] const GuildRank* GetRank(size_t index) const;
    [[nodiscard]] std::optional<std::uint32_t> GetRankRights(
        std::size_t index) const;

    void SetRoster(const std::vector<GuildSystemMember>& members);
    [[nodiscard]] size_t GetNumMembers() const;
    [[nodiscard]] const GuildSystemMember* GetMember(size_t index) const;
    [[nodiscard]] size_t GetNumOnlineMembers() const;

    [[nodiscard]] bool IsMemberByName(const std::string& name) const;

    void SetBankTabs(const std::vector<GuildBankTab>& tabs);
    void ClearGuildBankTabItems(std::uint8_t tab_index);
    void SetGuildBankTabItem(std::uint8_t tab_index, std::uint8_t slot_index,
                             const ItemInstance& item);
    void ClearGuildBankTabItem(std::uint8_t tab_index, std::uint8_t slot_index);
    [[nodiscard]] const ItemInstance* GetGuildBankTabItem(
        std::uint8_t tab_index, std::uint8_t slot_index) const;
    [[nodiscard]] bool IsGuildBankTabItemLocked(
        std::uint8_t tab_index, std::uint8_t slot_index) const;

    [[nodiscard]] const ItemInstance* GetGuildBankTabItemSlotState(
        std::uint32_t tab_index, std::uint32_t slot_index) const;

    static constexpr void DecodeGuildBankLinearSlot(
        std::uint32_t linear_slot, std::uint32_t& out_tab,
        std::uint32_t& out_slot) {
        out_tab = linear_slot / kGuildBankSlotsPerTab;
        out_slot = linear_slot % kGuildBankSlotsPerTab;
    }

    [[nodiscard]] bool SetGuildBankItemLockByLinearIndexIfEntryMatches(
        std::uint32_t linear_slot, std::uint32_t item_entry, bool locked);
    void SetGuildBankTabCount(std::uint8_t count);

    void UpdateGuildBankTabCacheEntry(std::uint8_t tab_index,
                                     const std::string& icon,
                                     const std::string& name);
    [[nodiscard]] size_t GetNumBankTabs() const;
    [[nodiscard]] const GuildBankTab* GetBankTab(size_t index) const;
    [[nodiscard]] uint64_t GetGuildBankMoney() const;
    void SetGuildBankMoney(uint64_t copper);
    [[nodiscard]] std::int32_t GetGuildBankMoneyWithdrawRemaining() const;
    void SetGuildBankMoneyWithdrawRemaining(std::int32_t copper);
    [[nodiscard]] std::int32_t GetLastGuildBankTabWithdrawalsRemaining() const;
    void SetLastGuildBankTabWithdrawalsRemaining(std::int32_t remaining);
    void SetGuildBankLog(const GuildBankLog& log);
    [[nodiscard]] std::uint8_t GetGuildBankLogEntryCount(std::uint8_t tab) const;
    [[nodiscard]] GuildBankLogEntry GetGuildBankLogEntry(std::uint8_t tab,
                                                         std::uint8_t index) const;
    [[nodiscard]] std::uint8_t GetGuildBankMoneyLogEntryCount() const;
    [[nodiscard]] GuildBankLogEntry GetGuildBankMoneyLogEntry(std::uint8_t index) const;
    void SetGuildBankTabText(std::uint8_t tab, const std::string& text);
    [[nodiscard]] std::optional<std::string> GetGuildBankTabText(std::uint8_t tab) const;
    [[nodiscard]] bool BeginGuildBankTextQuery(std::uint8_t tab);
    [[nodiscard]] bool IsGuildBankTabContentsRefreshPending(std::uint8_t tab) const;
    void SetGuildBankTabContentsRefreshPending(std::uint8_t tab, bool pending);
    void SetGuildBankTabTextRefreshPending(std::uint8_t tab, bool pending);
    void SetCurrentGuildBankTabIndex(std::uint8_t tab_index);
    [[nodiscard]] std::uint8_t GetCurrentGuildBankTabIndex() const;

    void SetPendingBankerGuid(uint64_t guid);
    [[nodiscard]] uint64_t GetPendingBankerGuid() const;
    [[nodiscard]] uint64_t PromotePendingBankerGuid();
    void SetBankerGuid(uint64_t guid);
    [[nodiscard]] uint64_t GetBankerGuid() const;
    void CloseBankFrame();
    [[nodiscard]] bool IsBankFrameOpen() const;

    struct ControlState {
        int selected_rank_index = -1;
        uint32_t rights = 0;
        std::array<uint32_t, 6> bank_tab_flags = {};
        std::array<uint32_t, 6> bank_tab_withdraw_item_limits = {};
        uint32_t money_withdraw_limit = 0;
    };
    [[nodiscard]] ControlState GetControlState() const;
    [[nodiscard]] uint32_t GetControlBankTabFlags(std::size_t rank_index,
                                                  std::size_t tab_index) const;
    [[nodiscard]] uint32_t GetControlBankTabWithdrawLimit(
        std::size_t rank_index, std::size_t tab_index) const;
    void SetControlRankFlagMask(uint32_t mask, bool enabled);
    void SetControlBankTabFlagMask(std::size_t tab_index, uint32_t mask, bool enabled);
    void SetControlBankTabWithdrawLimit(std::uint8_t tab_index, uint32_t limit);
    void SetControlMoneyWithdrawLimit(uint32_t limit);

    void SetControlRankFromIndex(uint32_t rank_index);

    [[nodiscard]] int32_t GetSelectedMemberIndex() const;

    [[nodiscard]] bool TryBeginGuildRosterRequest(uint32_t current_time_ms);
    void MarkGuildRosterRequestSent(uint32_t current_time_ms);
    void ResetGuildRosterRequestCooldown();

    void HandleRosterUpdate(const uint8_t* data, size_t len);

    void SetSelectedMemberGuid(uint64_t guid) { selected_guid_ = guid; }
    [[nodiscard]] uint64_t GetSelectedMemberGuid() const { return selected_guid_; }

    void ClearGuildBankTabCacheRuntimeState();

    void ShutdownGuildBankRuntimeState();
    void ResetGuildBankRuntimeStateOnPlayerEnterWorld();
    void ResetGuildTextRuntimeStateOnPlayerEnterWorld();
    void Reset();

private:
    GuildSystem() = default;
    void ClearGuildBankTabCacheUnlocked();
    void SetBankFrameGuidUnlocked(std::uint64_t guid);
    void ResetGuildBankVisibleStateUnlocked();
    void ResetGuildBankContentsRefreshStateUnlocked();
    void ResetGuildBankTextStateUnlocked();
    void EnsureGuildBankTabStorageUnlocked(std::size_t tab_count);

    static constexpr std::size_t kGuildControlMaxEditableRanks = 17;
    static constexpr std::size_t kGuildControlWordsPerRank = 14;
    static constexpr std::size_t kGuildControlRightsWord = 0;
    static constexpr std::size_t kGuildControlMoneyWithdrawWord = 1;
    static constexpr std::size_t kGuildControlBankFlagsWordBase = 2;
    static constexpr std::size_t kGuildControlBankWithdrawWordBase = 8;

    uint32_t guild_id_ = 0;
    std::string name_;
    std::string motd_;
    std::string info_;
    std::vector<GuildRank> ranks_;
    std::vector<GuildSystemMember> members_;
    std::vector<GuildBankTab> bank_tabs_;
    std::uint8_t bank_tab_count_ = 0;
    uint64_t bank_money_ = 0;
    std::int32_t bank_money_withdraw_remaining_ = -1;
    std::int32_t last_bank_tab_withdrawals_remaining_ = -1;
    std::array<std::uint8_t, kGuildBankLogPages> bank_log_counts_{};
    std::array<std::array<GuildBankLogEntry, kGuildBankLogEntriesPerPage>,
               kGuildBankLogPages> bank_logs_{};
    uint64_t banker_guid_ = 0;
    uint64_t pending_banker_guid_ = 0;
    bool bank_frame_open_ = false;
    std::array<std::string, kGuildBankMaxTabs> bank_tab_texts_{};
    std::array<bool, kGuildBankMaxTabs> bank_tab_contents_refresh_pending_{
        true, true, true, true, true, true};
    std::array<bool, kGuildBankMaxTabs> bank_tab_text_refresh_pending_{
        true, true, true, true, true, true};
    std::uint8_t current_guild_bank_tab_index_ = 0;
    int selected_control_rank_index_ = -1;
    std::array<std::uint32_t,
               kGuildControlMaxEditableRanks * kGuildControlWordsPerRank>
        control_rank_words_{};
    uint64_t selected_guid_ = 0;
    uint32_t roster_cooldown_ms_ = 0;
    mutable std::mutex mutex_;
};

}
