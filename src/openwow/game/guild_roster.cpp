
#include "openwow/game/guild_roster.h"

#include <algorithm>

namespace openwow::game {

GuildRosterManager& GuildRosterManager::Get() {
    static GuildRosterManager instance;
    return instance;
}

void GuildRosterManager::SetGuildInfo(uint32_t guildId,
                               const std::string& guildName,
                               const std::string& motd,
                               const std::string& guildInfo,
                               uint32_t memberCount) {
    std::lock_guard lock(mutex_);
    guild_id_ = guildId;
    guild_name_ = guildName;
    motd_ = motd;
    guild_info_ = guildInfo;
    declared_member_count_ = memberCount;
}

uint32_t GuildRosterManager::GetGuildId() const {
    std::lock_guard lock(mutex_);
    return guild_id_;
}

std::string GuildRosterManager::GetGuildName() const {
    std::lock_guard lock(mutex_);
    return guild_name_;
}

std::string GuildRosterManager::GetMotd() const {
    std::lock_guard lock(mutex_);
    return motd_;
}

void GuildRosterManager::SetMotd(const std::string& motd) {
    std::lock_guard lock(mutex_);
    motd_ = motd;
}

std::string GuildRosterManager::GetGuildInfo() const {
    std::lock_guard lock(mutex_);
    return guild_info_;
}

bool GuildRosterManager::IsInGuild() const {
    std::lock_guard lock(mutex_);
    return guild_id_ != 0;
}

void GuildRosterManager::SetRanks(const std::vector<RosterRankEntry>& ranks) {
    std::lock_guard lock(mutex_);
    ranks_ = ranks;
}

std::vector<RosterRankEntry> GuildRosterManager::GetRanks() const {
    std::lock_guard lock(mutex_);
    return ranks_;
}

std::optional<RosterRankEntry> GuildRosterManager::GetRank(uint32_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= ranks_.size()) return std::nullopt;
    return ranks_[index];
}

void GuildRosterManager::SetMembers(const std::vector<RosterMemberEntry>& members) {
    std::lock_guard lock(mutex_);
    members_ = members;
}

std::vector<RosterMemberEntry> GuildRosterManager::GetMembers() const {
    std::lock_guard lock(mutex_);
    return members_;
}

std::optional<RosterMemberEntry> GuildRosterManager::GetMember(ObjectGuid guid) const {
    std::lock_guard lock(mutex_);
    for (const auto& m : members_) {
        if (m.guid.GetRawValue() == guid.GetRawValue()) return m;
    }
    return std::nullopt;
}

uint32_t GuildRosterManager::GetMemberCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(members_.size());
}

uint32_t GuildRosterManager::GetOnlineCount() const {
    std::lock_guard lock(mutex_);
    uint32_t count = 0;
    for (const auto& m : members_) {
        if (m.online) ++count;
    }
    return count;
}

std::vector<RosterMemberEntry> GuildRosterManager::GetOnlineMembers() const {
    std::lock_guard lock(mutex_);
    std::vector<RosterMemberEntry> result;
    for (const auto& m : members_) {
        if (m.online) result.push_back(m);
    }
    return result;
}

std::vector<RosterMemberEntry> GuildRosterManager::GetMembersByRank(
    uint32_t rankIndex) const {
    std::lock_guard lock(mutex_);
    std::vector<RosterMemberEntry> result;
    for (const auto& m : members_) {
        if (m.rankIndex == rankIndex) result.push_back(m);
    }
    return result;
}

std::optional<RosterMemberEntry> GuildRosterManager::FindMemberByName(
    const std::string& name) const {
    std::lock_guard lock(mutex_);
    for (const auto& m : members_) {
        if (m.name == name) return m;
    }
    return std::nullopt;
}

std::optional<RosterMemberEntry> GuildRosterManager::GetLeader() const {
    std::lock_guard lock(mutex_);
    for (const auto& m : members_) {
        if (m.rankIndex == 0) return m;
    }
    return std::nullopt;
}

bool GuildRosterManager::IsGuildLeader(ObjectGuid guid) const {
    std::lock_guard lock(mutex_);
    for (const auto& m : members_) {
        if (m.guid.GetRawValue() == guid.GetRawValue() &&
            m.rankIndex == 0) {
            return true;
        }
    }
    return false;
}

void GuildRosterManager::SetBankMoney(uint64_t copper) {
    std::lock_guard lock(mutex_);
    bank_money_ = copper;
}

uint64_t GuildRosterManager::GetBankMoney() const {
    std::lock_guard lock(mutex_);
    return bank_money_;
}

uint32_t GuildRosterManager::GetTabCount() const {
    std::lock_guard lock(mutex_);
    return tab_count_;
}

void GuildRosterManager::SetTabCount(uint32_t count) {
    std::lock_guard lock(mutex_);
    tab_count_ = count;
}

void GuildRosterManager::Reset() {
    std::lock_guard lock(mutex_);
    guild_id_ = 0;
    guild_name_.clear();
    motd_.clear();
    guild_info_.clear();
    declared_member_count_ = 0;
    ranks_.clear();
    members_.clear();
    bank_money_ = 0;
    tab_count_ = 0;
}

}
