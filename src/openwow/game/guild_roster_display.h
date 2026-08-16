#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class GuildRosterSortMode : uint8_t {
    ByName,
    ByLevel,
    ByClass,
    ByRank,
    ByZone,
    ByNote,
};

struct GuildRosterMember {
    ObjectGuid guid{};
    std::string name;
    uint8_t level = 0;
    uint8_t classId = 0;
    std::string rankName;
    uint32_t rankIndex = 0;
    std::string zone;
    std::string note;
    std::string officerNote;
    float lastOnline = 0.0f;
    bool isOnline = false;
};

class GuildRosterDisplay {
 public:
    GuildRosterDisplay() = default;

    void SetMembers(const std::vector<GuildRosterMember>& members);
    void AddMember(const GuildRosterMember& member);
    void RemoveMember(ObjectGuid guid);

    [[nodiscard]] std::vector<GuildRosterMember> GetMembers() const;
    [[nodiscard]] std::optional<GuildRosterMember> GetMember(
        ObjectGuid guid) const;
    [[nodiscard]] std::vector<GuildRosterMember> GetOnlineMembers() const;
    [[nodiscard]] std::vector<GuildRosterMember> SortBy(
        GuildRosterSortMode mode, bool descending = false) const;
    [[nodiscard]] std::vector<GuildRosterMember> Search(
        const std::string& query) const;
    [[nodiscard]] size_t GetMemberCount() const;
    [[nodiscard]] size_t GetOnlineCount() const;
    [[nodiscard]] std::vector<GuildRosterMember> GetMembersByRank(
        uint32_t rankIndex) const;
    [[nodiscard]] std::vector<GuildRosterMember> GetMembersByClass(
        uint8_t classId) const;

    void SetShowOffline(bool show);
    [[nodiscard]] bool GetShowOffline() const;

    void Reset();

 private:
    std::vector<GuildRosterMember> members_;
    bool showOffline_ = true;
};

}
