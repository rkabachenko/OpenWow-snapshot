
#include "openwow/game/guild_system.h"

#include <algorithm>
#include <cctype>

#include "openwow/game/guild_manager.h"

namespace openwow::game {

namespace {

constexpr std::size_t kGuildControlWordsPerRank = 14;

constexpr std::size_t GuildControlWordOffset(std::size_t rank_index,
                                             std::size_t word_index) {
    return rank_index * kGuildControlWordsPerRank + word_index;
}

}

void GuildSystem::ClearGuildBankTabCacheUnlocked() {
    bank_tabs_.clear();
    bank_tab_count_ = 0;
    current_guild_bank_tab_index_ = 0;
    ResetGuildBankContentsRefreshStateUnlocked();
    ResetGuildBankTextStateUnlocked();
}

void GuildSystem::EnsureGuildBankTabStorageUnlocked(std::size_t tab_count) {

    const auto bounded_count = std::min(tab_count, kGuildBankMaxTabs);
    if (bank_tabs_.size() < bounded_count) {
        bank_tabs_.resize(bounded_count);
    }

    for (auto& tab : bank_tabs_) {
        if (tab.items.size() < kGuildBankSlotsPerTab) {
            tab.items.resize(kGuildBankSlotsPerTab);
        }
    }
}

void GuildSystem::SetBankFrameGuidUnlocked(std::uint64_t guid) {
    banker_guid_ = guid;
    bank_frame_open_ = (guid != 0);
    ResetGuildBankContentsRefreshStateUnlocked();
    ResetGuildBankTextStateUnlocked();
}

void GuildSystem::ResetGuildBankVisibleStateUnlocked() {
    ClearGuildBankTabCacheUnlocked();
    pending_banker_guid_ = 0;
    banker_guid_ = 0;
    bank_frame_open_ = false;
}

void GuildSystem::ResetGuildBankContentsRefreshStateUnlocked() {
    bank_tab_contents_refresh_pending_.fill(true);
}

void GuildSystem::ResetGuildBankTextStateUnlocked() {
    for (auto& text : bank_tab_texts_) {
        text.clear();
    }
    bank_tab_text_refresh_pending_.fill(true);
}

GuildSystem& GuildSystem::Get() {
    static GuildSystem instance;
    return instance;
}

void GuildSystem::SetGuildInfo(uint32_t guildId, const std::string& name,
                               const std::string& motd,
                               const std::string& info) {
    std::lock_guard lock(mutex_);
    guild_id_ = guildId;
    name_ = name;
    motd_ = motd;
    info_ = info;
}

void GuildSystem::SetGuildIdentity(uint32_t guildId, const std::string& name) {
    std::lock_guard lock(mutex_);
    guild_id_ = guildId;
    name_ = name;
}

void GuildSystem::SetGuildMOTD(const std::string& motd) {
    std::lock_guard lock(mutex_);
    motd_ = motd;
}

uint32_t GuildSystem::GetGuildId() const {
    std::lock_guard lock(mutex_);
    return guild_id_;
}

const std::string& GuildSystem::GetGuildName() const {
    std::lock_guard lock(mutex_);
    return name_;
}

const std::string& GuildSystem::GetGuildMOTD() const {
    std::lock_guard lock(mutex_);
    return motd_;
}

const std::string& GuildSystem::GetGuildInfo() const {
    std::lock_guard lock(mutex_);
    return info_;
}

bool GuildSystem::IsInGuild() const {
    std::lock_guard lock(mutex_);
    return guild_id_ != 0;
}

void GuildSystem::SetRanks(const std::vector<GuildRank>& ranks) {
    std::lock_guard lock(mutex_);
    ranks_ = ranks;
    control_rank_words_.fill(0);

    const auto cached_count =
        std::min(ranks.size(), kGuildControlMaxEditableRanks);
    for (std::size_t i = 0; i < cached_count; ++i) {
        const auto& rank = ranks[i];
        control_rank_words_[GuildControlWordOffset(i, kGuildControlRightsWord)] =
            rank.rights;
        control_rank_words_[GuildControlWordOffset(
            i, kGuildControlMoneyWithdrawWord)] = rank.money_per_day;
        for (std::size_t tab = 0; tab < kGuildBankMaxTabs; ++tab) {
            control_rank_words_[GuildControlWordOffset(
                i, kGuildControlBankFlagsWordBase + tab)] =
                rank.bank_tab_flags[tab];
            control_rank_words_[GuildControlWordOffset(
                i, kGuildControlBankWithdrawWordBase + tab)] =
                rank.bank_tab_withdraw_item_limits[tab];
        }
    }
}

size_t GuildSystem::GetNumRanks() const {
    std::lock_guard lock(mutex_);
    return ranks_.size();
}

const GuildRank* GuildSystem::GetRank(size_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= ranks_.size()) return nullptr;
    return &ranks_[index];
}

std::optional<std::uint32_t> GuildSystem::GetRankRights(
    std::size_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= ranks_.size()) {
        return std::nullopt;
    }
    return ranks_[index].rights;
}

void GuildSystem::SetRoster(const std::vector<GuildSystemMember>& members) {
    std::lock_guard lock(mutex_);
    members_ = members;
}

size_t GuildSystem::GetNumMembers() const {
    std::lock_guard lock(mutex_);
    return members_.size();
}

const GuildSystemMember* GuildSystem::GetMember(size_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= members_.size()) return nullptr;
    return &members_[index];
}

size_t GuildSystem::GetNumOnlineMembers() const {
    std::lock_guard lock(mutex_);
    return static_cast<size_t>(
        std::count_if(members_.begin(), members_.end(),
                      [](const GuildSystemMember& m) { return m.is_online; }));
}

bool GuildSystem::IsMemberByName(const std::string& name) const {
    std::lock_guard lock(mutex_);
    for (const auto& m : members_) {
        if (m.name.size() == name.size() &&
            std::equal(m.name.begin(), m.name.end(), name.begin(),
                       [](char a, char b) {
                           return std::tolower(static_cast<unsigned char>(a)) ==
                                  std::tolower(static_cast<unsigned char>(b));
                       })) {
            return true;
        }
    }
    return false;
}

void GuildSystem::SetBankTabs(const std::vector<GuildBankTab>& tabs) {
    std::lock_guard lock(mutex_);
    std::vector<GuildBankTab> updated_tabs;
    updated_tabs.resize(std::min<std::size_t>(tabs.size(), kGuildBankMaxTabs));

    for (std::size_t i = 0; i < updated_tabs.size(); ++i) {
        updated_tabs[i].name = tabs[i].name;
        updated_tabs[i].icon = tabs[i].icon;
        if (i < bank_tabs_.size()) {
            updated_tabs[i].items = bank_tabs_[i].items;
        }
        if (updated_tabs[i].items.size() < kGuildBankSlotsPerTab) {
            updated_tabs[i].items.resize(kGuildBankSlotsPerTab);
            updated_tabs[i].item_locks.resize(kGuildBankSlotsPerTab, false);
        }
    }

    bank_tabs_ = std::move(updated_tabs);
    bank_tab_count_ = static_cast<std::uint8_t>(std::min<std::size_t>(
        tabs.size(), kGuildBankMaxTabs));
}

void GuildSystem::ClearGuildBankTabItems(std::uint8_t tab_index) {
    std::lock_guard lock(mutex_);
    if (tab_index >= kGuildBankMaxTabs) {
        return;
    }

    EnsureGuildBankTabStorageUnlocked(static_cast<std::size_t>(tab_index) + 1);
    bank_tabs_[tab_index].items.assign(kGuildBankSlotsPerTab, ItemInstance{});
    bank_tabs_[tab_index].item_locks.assign(kGuildBankSlotsPerTab, false);
}

void GuildSystem::SetGuildBankTabItem(std::uint8_t tab_index,
                                      std::uint8_t slot_index,
                                      const ItemInstance& item) {
    std::lock_guard lock(mutex_);
    if (tab_index >= kGuildBankMaxTabs || slot_index >= kGuildBankSlotsPerTab) {
        return;
    }

    EnsureGuildBankTabStorageUnlocked(static_cast<std::size_t>(tab_index) + 1);
    bank_tabs_[tab_index].items[slot_index] = item;
    bank_tabs_[tab_index].item_locks[slot_index] = false;
}

void GuildSystem::ClearGuildBankTabItem(std::uint8_t tab_index,
                                        std::uint8_t slot_index) {
    std::lock_guard lock(mutex_);
    if (tab_index >= kGuildBankMaxTabs || slot_index >= kGuildBankSlotsPerTab) {
        return;
    }

    EnsureGuildBankTabStorageUnlocked(static_cast<std::size_t>(tab_index) + 1);
    bank_tabs_[tab_index].items[slot_index] = {};
    bank_tabs_[tab_index].item_locks[slot_index] = false;
}

const ItemInstance* GuildSystem::GetGuildBankTabItem(
    std::uint8_t tab_index, std::uint8_t slot_index) const {
    std::lock_guard lock(mutex_);
    if (tab_index >= bank_tab_count_ || tab_index >= bank_tabs_.size()) {
        return nullptr;
    }

    const auto& items = bank_tabs_[tab_index].items;
    if (slot_index >= items.size()) {
        return nullptr;
    }

    return &items[slot_index];
}

bool GuildSystem::IsGuildBankTabItemLocked(
    const std::uint8_t tab_index, const std::uint8_t slot_index) const {
    std::lock_guard lock(mutex_);
    if (tab_index >= bank_tab_count_ || tab_index >= bank_tabs_.size()) {
        return false;
    }
    const auto& locks = bank_tabs_[tab_index].item_locks;
    return slot_index < locks.size() && locks[slot_index];
}

const ItemInstance* GuildSystem::GetGuildBankTabItemSlotState(
    std::uint32_t tab_index, std::uint32_t slot_index) const {
    std::lock_guard lock(mutex_);
    if (tab_index >= kGuildBankMaxTabs) {
        return nullptr;
    }
    if (tab_index >= bank_tabs_.size()) {
        return nullptr;
    }

    const auto& items = bank_tabs_[tab_index].items;
    if (slot_index >= items.size()) {
        return nullptr;
    }

    const auto& item = items[slot_index];
    if (item.entry == 0) {
        return nullptr;
    }
    return &item;
}

bool GuildSystem::SetGuildBankItemLockByLinearIndexIfEntryMatches(
    std::uint32_t linear_slot, std::uint32_t item_entry, bool locked) {
    std::lock_guard lock(mutex_);
    const auto tab_index = linear_slot / kGuildBankSlotsPerTab;
    const auto slot_index = linear_slot % kGuildBankSlotsPerTab;
    if (tab_index >= bank_tabs_.size()) {
        return false;
    }

    auto& items = bank_tabs_[tab_index].items;
    if (slot_index >= items.size()) {
        return false;
    }

    const auto& item = items[slot_index];
    if (item.entry == 0 || item.entry != item_entry) {
        return false;
    }

    bank_tabs_[tab_index].item_locks[slot_index] = locked;
    return true;
}

void GuildSystem::SetGuildBankTabCount(std::uint8_t count) {
    std::lock_guard lock(mutex_);
    bank_tab_count_ = std::min<std::uint8_t>(
        count, static_cast<std::uint8_t>(kGuildBankMaxTabs));
    EnsureGuildBankTabStorageUnlocked(bank_tab_count_);
}

void GuildSystem::UpdateGuildBankTabCacheEntry(std::uint8_t tab_index,
                                               const std::string& icon,
                                               const std::string& name) {
    std::lock_guard lock(mutex_);
    if (tab_index >= bank_tabs_.size()) {
        EnsureGuildBankTabStorageUnlocked(
            static_cast<std::size_t>(tab_index) + 1);
    }
    if (tab_index < bank_tabs_.size()) {
        bank_tabs_[tab_index].icon = icon;
        bank_tabs_[tab_index].name = name;
    }
}

size_t GuildSystem::GetNumBankTabs() const {
    std::lock_guard lock(mutex_);
    return bank_tab_count_;
}

const GuildBankTab* GuildSystem::GetBankTab(size_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= bank_tab_count_ || index >= bank_tabs_.size()) return nullptr;
    return &bank_tabs_[index];
}

uint64_t GuildSystem::GetGuildBankMoney() const {
    std::lock_guard lock(mutex_);
    return bank_money_;
}

void GuildSystem::SetGuildBankMoney(uint64_t copper) {
    std::lock_guard lock(mutex_);
    bank_money_ = copper;
}

std::int32_t GuildSystem::GetGuildBankMoneyWithdrawRemaining() const {
    std::lock_guard lock(mutex_);
    return bank_money_withdraw_remaining_;
}

void GuildSystem::SetGuildBankMoneyWithdrawRemaining(std::int32_t copper) {
    std::lock_guard lock(mutex_);
    bank_money_withdraw_remaining_ = copper;
}

std::int32_t GuildSystem::GetLastGuildBankTabWithdrawalsRemaining() const {
    std::lock_guard lock(mutex_);
    return last_bank_tab_withdrawals_remaining_;
}

void GuildSystem::SetLastGuildBankTabWithdrawalsRemaining(
    std::int32_t remaining) {
    std::lock_guard lock(mutex_);
    last_bank_tab_withdrawals_remaining_ = remaining;
}

void GuildSystem::SetGuildBankLog(const GuildBankLog& log) {
    std::lock_guard lock(mutex_);
    if (log.tab >= bank_logs_.size()) {
        return;
    }

    auto& stored_entries = bank_logs_[log.tab];
    stored_entries.fill(GuildBankLogEntry{});

    const auto stored_count = std::min<std::size_t>(
        log.entries.size(), stored_entries.size());
    bank_log_counts_[log.tab] = static_cast<std::uint8_t>(stored_count);
    for (std::size_t i = 0; i < stored_count; ++i) {
        stored_entries[i] = log.entries[i];
    }
}

std::uint8_t GuildSystem::GetGuildBankLogEntryCount(std::uint8_t tab) const {
    std::lock_guard lock(mutex_);
    if (tab >= bank_log_counts_.size()) {
        return 0;
    }
    return bank_log_counts_[tab];
}

GuildBankLogEntry GuildSystem::GetGuildBankLogEntry(std::uint8_t tab,
                                                    std::uint8_t index) const {
    std::lock_guard lock(mutex_);
    if (tab >= bank_logs_.size() || index >= bank_logs_[tab].size()) {
        return {};
    }
    return bank_logs_[tab][index];
}

std::uint8_t GuildSystem::GetGuildBankMoneyLogEntryCount() const {
    return GetGuildBankLogEntryCount(
        static_cast<std::uint8_t>(kGuildBankMoneyLogPage));
}

GuildBankLogEntry GuildSystem::GetGuildBankMoneyLogEntry(std::uint8_t index) const {
    return GetGuildBankLogEntry(
        static_cast<std::uint8_t>(kGuildBankMoneyLogPage), index);
}

void GuildSystem::SetGuildBankTabText(std::uint8_t tab,
                                      const std::string& text) {
    std::lock_guard lock(mutex_);
    if (tab >= kGuildBankMaxTabs) {
        return;
    }
    bank_tab_texts_[tab] = text;
}

std::optional<std::string> GuildSystem::GetGuildBankTabText(
    std::uint8_t tab) const {
    std::lock_guard lock(mutex_);
    if (tab >= kGuildBankMaxTabs ||
        tab >= static_cast<std::uint8_t>(bank_tabs_.size())) {
        return std::nullopt;
    }
    return bank_tab_texts_[tab];
}

bool GuildSystem::BeginGuildBankTextQuery(std::uint8_t tab) {
    std::lock_guard lock(mutex_);
    if (!bank_frame_open_ || banker_guid_ == 0 || tab >= kGuildBankMaxTabs ||
        !bank_tab_text_refresh_pending_[tab]) {
        return false;
    }

    bank_tab_text_refresh_pending_[tab] = false;
    return true;
}

bool GuildSystem::IsGuildBankTabContentsRefreshPending(std::uint8_t tab) const {
    std::lock_guard lock(mutex_);
    if (tab >= kGuildBankMaxTabs) {
        return false;
    }
    return bank_tab_contents_refresh_pending_[tab];
}

void GuildSystem::SetGuildBankTabContentsRefreshPending(std::uint8_t tab,
                                                        bool pending) {
    std::lock_guard lock(mutex_);
    if (tab >= kGuildBankMaxTabs) {
        return;
    }
    bank_tab_contents_refresh_pending_[tab] = pending;
}

void GuildSystem::SetGuildBankTabTextRefreshPending(std::uint8_t tab,
                                                    bool pending) {
    std::lock_guard lock(mutex_);
    if (tab >= kGuildBankMaxTabs) {
        return;
    }
    bank_tab_text_refresh_pending_[tab] = pending;
}

void GuildSystem::SetCurrentGuildBankTabIndex(std::uint8_t tab_index) {
    std::lock_guard lock(mutex_);
    current_guild_bank_tab_index_ = tab_index;
}

std::uint8_t GuildSystem::GetCurrentGuildBankTabIndex() const {
    std::lock_guard lock(mutex_);
    return current_guild_bank_tab_index_;
}

void GuildSystem::SetPendingBankerGuid(uint64_t guid) {
    std::lock_guard lock(mutex_);
    pending_banker_guid_ = guid;
}

uint64_t GuildSystem::GetPendingBankerGuid() const {
    std::lock_guard lock(mutex_);
    return pending_banker_guid_;
}

uint64_t GuildSystem::PromotePendingBankerGuid() {
    std::lock_guard lock(mutex_);
    banker_guid_ = pending_banker_guid_;
    pending_banker_guid_ = 0;
    bank_frame_open_ = (banker_guid_ != 0);
    return banker_guid_;
}

void GuildSystem::SetBankerGuid(uint64_t guid) {
    std::lock_guard lock(mutex_);
    pending_banker_guid_ = 0;
    SetBankFrameGuidUnlocked(guid);
}

uint64_t GuildSystem::GetBankerGuid() const {
    std::lock_guard lock(mutex_);
    return banker_guid_;
}

void GuildSystem::CloseBankFrame() {
    std::lock_guard lock(mutex_);
    pending_banker_guid_ = 0;
    SetBankFrameGuidUnlocked(0);
}

bool GuildSystem::IsBankFrameOpen() const {
    std::lock_guard lock(mutex_);
    return bank_frame_open_;
}

GuildSystem::ControlState GuildSystem::GetControlState() const {
    std::lock_guard lock(mutex_);

    ControlState state;
    state.selected_rank_index = selected_control_rank_index_;
    if (selected_control_rank_index_ < 0 ||
        static_cast<std::size_t>(selected_control_rank_index_) >=
            kGuildControlMaxEditableRanks) {
        return state;
    }

    const auto rank_index = static_cast<std::size_t>(selected_control_rank_index_);
    state.rights = control_rank_words_[GuildControlWordOffset(
        rank_index, kGuildControlRightsWord)];
    state.money_withdraw_limit = control_rank_words_[GuildControlWordOffset(
        rank_index, kGuildControlMoneyWithdrawWord)];
    for (std::size_t tab = 0; tab < kGuildBankMaxTabs; ++tab) {
        state.bank_tab_flags[tab] = control_rank_words_[GuildControlWordOffset(
            rank_index, kGuildControlBankFlagsWordBase + tab)];
        state.bank_tab_withdraw_item_limits[tab] =
            control_rank_words_[GuildControlWordOffset(
                rank_index, kGuildControlBankWithdrawWordBase + tab)];
    }

    return state;
}

uint32_t GuildSystem::GetControlBankTabFlags(std::size_t rank_index,
                                             std::size_t tab_index) const {
    std::lock_guard lock(mutex_);
    if (rank_index >= kGuildControlMaxEditableRanks ||
        tab_index >= kGuildBankMaxTabs) {
        return 0;
    }

    return control_rank_words_[GuildControlWordOffset(
        rank_index, kGuildControlBankFlagsWordBase + tab_index)];
}

uint32_t GuildSystem::GetControlBankTabWithdrawLimit(
    std::size_t rank_index, std::size_t tab_index) const {
    std::lock_guard lock(mutex_);
    if (rank_index >= kGuildControlMaxEditableRanks ||
        tab_index >= kGuildBankMaxTabs) {
        return 0;
    }

    return control_rank_words_[GuildControlWordOffset(
        rank_index, kGuildControlBankWithdrawWordBase + tab_index)];
}

void GuildSystem::SetControlRankFlagMask(uint32_t mask, bool enabled) {
    std::lock_guard lock(mutex_);
    if (selected_control_rank_index_ < 0 ||
        static_cast<std::size_t>(selected_control_rank_index_) >=
            kGuildControlMaxEditableRanks) {
        return;
    }

    auto& rights = control_rank_words_[GuildControlWordOffset(
        static_cast<std::size_t>(selected_control_rank_index_),
        kGuildControlRightsWord)];
    if (enabled) {
        rights |= mask;
    } else {
        rights &= ~mask;
    }
}

void GuildSystem::SetControlBankTabFlagMask(std::size_t tab_index, uint32_t mask,
                                            bool enabled) {
    std::lock_guard lock(mutex_);
    if (selected_control_rank_index_ < 0 ||
        static_cast<std::size_t>(selected_control_rank_index_) >=
            kGuildControlMaxEditableRanks ||
        tab_index >= kGuildBankMaxTabs || mask == 0) {
        return;
    }

    auto& tab_flags = control_rank_words_[GuildControlWordOffset(
        static_cast<std::size_t>(selected_control_rank_index_),
        kGuildControlBankFlagsWordBase + tab_index)];
    if (enabled) {
        tab_flags |= mask;
    } else {
        tab_flags &= ~mask;
    }
}

void GuildSystem::SetControlBankTabWithdrawLimit(std::uint8_t tab_index,
                                                 uint32_t limit) {
    std::lock_guard lock(mutex_);
    if (selected_control_rank_index_ < 0 ||
        static_cast<std::size_t>(selected_control_rank_index_) >=
            kGuildControlMaxEditableRanks) {
        return;
    }

    const auto absolute_word = GuildControlWordOffset(
        static_cast<std::size_t>(selected_control_rank_index_),
        kGuildControlBankWithdrawWordBase + tab_index);
    if (absolute_word >= control_rank_words_.size()) {
        return;
    }

    control_rank_words_[absolute_word] = std::min(limit, 100000u);
}

void GuildSystem::SetControlMoneyWithdrawLimit(uint32_t limit) {
    std::lock_guard lock(mutex_);
    if (selected_control_rank_index_ < 0 ||
        static_cast<std::size_t>(selected_control_rank_index_) >=
            kGuildControlMaxEditableRanks) {
        return;
    }

    control_rank_words_[GuildControlWordOffset(
        static_cast<std::size_t>(selected_control_rank_index_),
        kGuildControlMoneyWithdrawWord)] = std::min(limit, 1000000000u);
}

void GuildSystem::ClearGuildBankTabCacheRuntimeState() {
    std::lock_guard lock(mutex_);
    ClearGuildBankTabCacheUnlocked();
}

void GuildSystem::ShutdownGuildBankRuntimeState() {
    std::lock_guard lock(mutex_);
    ResetGuildBankVisibleStateUnlocked();
}

void GuildSystem::ResetGuildBankRuntimeStateOnPlayerEnterWorld() {
    std::lock_guard lock(mutex_);
    ResetGuildBankVisibleStateUnlocked();
    last_bank_tab_withdrawals_remaining_ = -1;
    bank_log_counts_.fill(0);
    for (auto& page : bank_logs_) {
        page.fill(GuildBankLogEntry{});
    }
}

void GuildSystem::ResetGuildTextRuntimeStateOnPlayerEnterWorld() {
    std::lock_guard lock(mutex_);
    motd_.clear();
    info_.clear();
}

void GuildSystem::Reset() {
    std::lock_guard lock(mutex_);
    guild_id_ = 0;
    name_.clear();
    motd_.clear();
    info_.clear();
    ranks_.clear();
    members_.clear();
    bank_money_ = 0;
    bank_money_withdraw_remaining_ = -1;
    last_bank_tab_withdrawals_remaining_ = -1;
    bank_log_counts_.fill(0);
    for (auto& page : bank_logs_) {
        page.fill(GuildBankLogEntry{});
    }
    ResetGuildBankVisibleStateUnlocked();
    selected_control_rank_index_ = -1;
    control_rank_words_.fill(0);
    selected_guid_ = 0;
    roster_cooldown_ms_ = 0;
}

void GuildSystem::SetControlRankFromIndex(uint32_t rank_index) {
    std::lock_guard lock(mutex_);
    if (rank_index >= kGuildControlMaxEditableRanks) {
        selected_control_rank_index_ = -1;
        return;
    }

    selected_control_rank_index_ = static_cast<int>(rank_index);
}

int32_t GuildSystem::GetSelectedMemberIndex() const {
    std::lock_guard lock(mutex_);
    if (selected_guid_ == 0) return -1;
    if (members_.empty()) return -1;

    for (size_t i = 0; i < members_.size(); ++i) {
        if (members_[i].guid == selected_guid_) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

bool GuildSystem::TryBeginGuildRosterRequest(uint32_t current_time_ms) {
    std::lock_guard lock(mutex_);

    if (roster_cooldown_ms_ != 0 &&
        static_cast<std::int32_t>(current_time_ms - roster_cooldown_ms_) < 0) {
        return false;
    }

    roster_cooldown_ms_ = current_time_ms + 10000;
    return true;
}

void GuildSystem::MarkGuildRosterRequestSent(uint32_t current_time_ms) {
    std::lock_guard lock(mutex_);
    roster_cooldown_ms_ = current_time_ms + 10000;
}

void GuildSystem::ResetGuildRosterRequestCooldown() {
    std::lock_guard lock(mutex_);
    roster_cooldown_ms_ = 0;
}

void GuildSystem::HandleRosterUpdate(const uint8_t* data, size_t len) {
    GuildRoster roster;
    if (!detail::ParseGuildRosterPacket(data, len, &roster)) {
        return;
    }

    std::lock_guard lock(mutex_);
    motd_ = roster.motd;
    info_ = roster.info_text;

    ranks_.clear();
    ranks_.reserve(roster.ranks.size());
    for (std::size_t index = 0; index < roster.ranks.size(); ++index) {
        GuildRank rank;
        rank.id = static_cast<std::uint32_t>(index);
        rank.rights = roster.ranks[index].flags;
        rank.money_per_day = roster.ranks[index].withdraw_gold_limit;
        for (std::size_t tab = 0; tab < rank.bank_tab_flags.size(); ++tab) {
            rank.bank_tab_flags[tab] = roster.ranks[index].tab_flags[tab];
            rank.bank_tab_withdraw_item_limits[tab] =
                roster.ranks[index].tab_withdraw_item_limit[tab];
        }
        ranks_.push_back(std::move(rank));
    }

    control_rank_words_.fill(0);
    const auto cached_count =
        std::min(ranks_.size(), kGuildControlMaxEditableRanks);
    for (std::size_t index = 0; index < cached_count; ++index) {
        const auto& rank = ranks_[index];
        control_rank_words_[GuildControlWordOffset(index, kGuildControlRightsWord)] =
            rank.rights;
        control_rank_words_[GuildControlWordOffset(index, kGuildControlMoneyWithdrawWord)] =
            rank.money_per_day;
        for (std::size_t tab = 0; tab < kGuildBankMaxTabs; ++tab) {
            control_rank_words_[GuildControlWordOffset(
                index, kGuildControlBankFlagsWordBase + tab)] =
                rank.bank_tab_flags[tab];
            control_rank_words_[GuildControlWordOffset(
                index, kGuildControlBankWithdrawWordBase + tab)] =
                rank.bank_tab_withdraw_item_limits[tab];
        }
    }

    members_.clear();
    members_.reserve(roster.members.size());
    for (const auto& member : roster.members) {
        GuildSystemMember stored_member;
        stored_member.guid = member.guid.GetRawValue();
        stored_member.name = member.name;
        stored_member.rank_index =
            member.rank_id < 0 ? 0u : static_cast<std::uint32_t>(member.rank_id);
        stored_member.level = member.level;
        stored_member.class_id = member.class_id;
        stored_member.gender_id = member.gender;
        stored_member.area_id =
            member.area_id < 0 ? 0u : static_cast<std::uint32_t>(member.area_id);
        stored_member.last_online = member.status == 0 ? member.last_save : 0.0f;
        stored_member.public_note = member.note;
        stored_member.officer_note = member.officer_note;
        stored_member.status = member.status;
        stored_member.is_online = (member.status != 0);
        members_.push_back(std::move(stored_member));
    }
}

}
