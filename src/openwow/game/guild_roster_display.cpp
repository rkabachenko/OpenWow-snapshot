
#include "openwow/game/guild_roster_display.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

void GuildRosterDisplay::SetMembers(
    const std::vector<GuildRosterMember>& members) {
    members_ = members;
}

void GuildRosterDisplay::AddMember(const GuildRosterMember& member) {

    for (auto& m : members_) {
        if (m.guid == member.guid) {
            m = member;
            return;
        }
    }
    members_.push_back(member);
}

void GuildRosterDisplay::RemoveMember(ObjectGuid guid) {
    members_.erase(
        std::remove_if(members_.begin(), members_.end(),
                       [&](const GuildRosterMember& m) {
                           return m.guid == guid;
                       }),
        members_.end());
}

std::vector<GuildRosterMember> GuildRosterDisplay::GetMembers() const {
    return members_;
}

std::optional<GuildRosterMember> GuildRosterDisplay::GetMember(
    ObjectGuid guid) const {
    for (const auto& m : members_) {
        if (m.guid == guid) return m;
    }
    return std::nullopt;
}

std::vector<GuildRosterMember> GuildRosterDisplay::GetOnlineMembers() const {
    std::vector<GuildRosterMember> result;
    for (const auto& m : members_) {
        if (m.isOnline) result.push_back(m);
    }
    return result;
}

std::vector<GuildRosterMember> GuildRosterDisplay::SortBy(
    GuildRosterSortMode mode, bool descending) const {
    std::vector<GuildRosterMember> sorted = members_;

    auto cmp = [&](const GuildRosterMember& a,
                    const GuildRosterMember& b) -> bool {
        switch (mode) {
            case GuildRosterSortMode::ByName:
                return descending ? (a.name > b.name) : (a.name < b.name);
            case GuildRosterSortMode::ByLevel:
                return descending ? (a.level > b.level) : (a.level < b.level);
            case GuildRosterSortMode::ByClass:
                return descending ? (a.classId > b.classId)
                                  : (a.classId < b.classId);
            case GuildRosterSortMode::ByRank:
                return descending ? (a.rankIndex > b.rankIndex)
                                  : (a.rankIndex < b.rankIndex);
            case GuildRosterSortMode::ByZone:
                return descending ? (a.zone > b.zone) : (a.zone < b.zone);
            case GuildRosterSortMode::ByNote:
                return descending ? (a.note > b.note) : (a.note < b.note);
            default:
                return false;
        }
    };

    std::sort(sorted.begin(), sorted.end(), cmp);
    return sorted;
}

std::vector<GuildRosterMember> GuildRosterDisplay::Search(
    const std::string& query) const {
    if (query.empty()) return members_;

    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::vector<GuildRosterMember> result;
    for (const auto& m : members_) {
        std::string lowerName = m.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lowerName.find(lowerQuery) != std::string::npos) {
            result.push_back(m);
        }
    }
    return result;
}

size_t GuildRosterDisplay::GetMemberCount() const {
    return members_.size();
}

size_t GuildRosterDisplay::GetOnlineCount() const {
    size_t count = 0;
    for (const auto& m : members_) {
        if (m.isOnline) ++count;
    }
    return count;
}

std::vector<GuildRosterMember> GuildRosterDisplay::GetMembersByRank(
    uint32_t rankIndex) const {
    std::vector<GuildRosterMember> result;
    for (const auto& m : members_) {
        if (m.rankIndex == rankIndex) result.push_back(m);
    }
    return result;
}

std::vector<GuildRosterMember> GuildRosterDisplay::GetMembersByClass(
    uint8_t classId) const {
    std::vector<GuildRosterMember> result;
    for (const auto& m : members_) {
        if (m.classId == classId) result.push_back(m);
    }
    return result;
}

void GuildRosterDisplay::SetShowOffline(bool show) {
    showOffline_ = show;
}

bool GuildRosterDisplay::GetShowOffline() const {
    return showOffline_;
}

void GuildRosterDisplay::Reset() {
    members_.clear();
    showOffline_ = true;
}

}
