
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct RosterRankEntry {
    uint32_t rankId = 0;
    std::string rankName;
    uint32_t permissions = 0;
};

struct RosterMemberEntry {
    ObjectGuid guid{};
    std::string name;
    uint32_t classId = 0;
    uint32_t level = 0;
    uint32_t zone = 0;
    uint32_t rankIndex = 0;
    std::string note;
    std::string officerNote;
    bool online = false;
    uint64_t lastOnline = 0;
    uint32_t achievementPoints = 0;
};

class GuildRosterManager {
 public:
    static GuildRosterManager& Get();

    void SetGuildInfo(uint32_t guildId, const std::string& guildName,
                      const std::string& motd, const std::string& guildInfo,
                      uint32_t memberCount);
    [[nodiscard]] uint32_t GetGuildId() const;
    [[nodiscard]] std::string GetGuildName() const;
    [[nodiscard]] std::string GetMotd() const;
    void SetMotd(const std::string& motd);
    [[nodiscard]] std::string GetGuildInfo() const;
    [[nodiscard]] bool IsInGuild() const;

    void SetRanks(const std::vector<RosterRankEntry>& ranks);
    [[nodiscard]] std::vector<RosterRankEntry> GetRanks() const;
    [[nodiscard]] std::optional<RosterRankEntry> GetRank(
        uint32_t index) const;

    void SetMembers(const std::vector<RosterMemberEntry>& members);
    [[nodiscard]] std::vector<RosterMemberEntry> GetMembers() const;
    [[nodiscard]] std::optional<RosterMemberEntry> GetMember(ObjectGuid guid) const;
    [[nodiscard]] uint32_t GetMemberCount() const;
    [[nodiscard]] uint32_t GetOnlineCount() const;
    [[nodiscard]] std::vector<RosterMemberEntry> GetOnlineMembers() const;
    [[nodiscard]] std::vector<RosterMemberEntry> GetMembersByRank(
        uint32_t rankIndex) const;
    [[nodiscard]] std::optional<RosterMemberEntry> FindMemberByName(
        const std::string& name) const;
    [[nodiscard]] std::optional<RosterMemberEntry> GetLeader() const;
    [[nodiscard]] bool IsGuildLeader(ObjectGuid guid) const;

    void SetBankMoney(uint64_t copper);
    [[nodiscard]] uint64_t GetBankMoney() const;
    [[nodiscard]] uint32_t GetTabCount() const;
    void SetTabCount(uint32_t count);

    void Reset();

 private:
    GuildRosterManager() = default;

    uint32_t guild_id_ = 0;
    std::string guild_name_;
    std::string motd_;
    std::string guild_info_;
    uint32_t declared_member_count_ = 0;
    std::vector<RosterRankEntry> ranks_;
    std::vector<RosterMemberEntry> members_;
    uint64_t bank_money_ = 0;
    uint32_t tab_count_ = 0;
    mutable std::mutex mutex_;
};

}
