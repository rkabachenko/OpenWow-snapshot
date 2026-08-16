
#include "openwow/game/raid_frame_layout.h"

#include <algorithm>

namespace openwow::game {

void RaidFrameLayout::SetGroups(std::vector<RaidFrameGroupData> groups) {
    std::lock_guard lock(mutex_);
    groups_ = std::move(groups);
}

std::vector<RaidFrameGroupData> RaidFrameLayout::GetGroups() const {
    std::lock_guard lock(mutex_);
    return groups_;
}

std::optional<RaidFrameGroupData> RaidFrameLayout::GetGroup(
    std::uint8_t groupIndex) const {
    std::lock_guard lock(mutex_);
    for (const auto& g : groups_) {
        if (g.groupIndex == groupIndex) return g;
    }
    return std::nullopt;
}

std::vector<GroupFrameMemberData> RaidFrameLayout::GetAllMembers() const {
    std::lock_guard lock(mutex_);
    std::vector<GroupFrameMemberData> all;
    for (const auto& g : groups_) {
        all.insert(all.end(), g.members.begin(), g.members.end());
    }
    return all;
}

std::size_t RaidFrameLayout::GetMemberCount() const {
    std::lock_guard lock(mutex_);
    std::size_t count = 0;
    for (const auto& g : groups_) {
        count += g.members.size();
    }
    return count;
}

std::vector<GroupFrameMemberData> RaidFrameLayout::SortBy(
    RaidFrameSortMode mode) const {
    auto all = GetAllMembers();
    switch (mode) {
        case RaidFrameSortMode::ByGroup:

            break;
        case RaidFrameSortMode::ByClass:
            std::sort(all.begin(), all.end(),
                      [](const auto& a, const auto& b) {
                          return a.classId < b.classId;
                      });
            break;
        case RaidFrameSortMode::ByRole:
            std::sort(all.begin(), all.end(),
                      [](const auto& a, const auto& b) {
                          return a.groupRole < b.groupRole;
                      });
            break;
        case RaidFrameSortMode::ByName:
            std::sort(all.begin(), all.end(),
                      [](const auto& a, const auto& b) {
                          return a.name < b.name;
                      });
            break;
    }
    return all;
}

std::vector<GroupFrameMemberData> RaidFrameLayout::GetMembersByClass(
    std::uint8_t classId) const {
    auto all = GetAllMembers();
    std::vector<GroupFrameMemberData> result;
    for (const auto& m : all) {
        if (m.classId == classId) result.push_back(m);
    }
    return result;
}

std::size_t RaidFrameLayout::GetDeadCount() const {
    auto all = GetAllMembers();
    return static_cast<std::size_t>(
        std::count_if(all.begin(), all.end(),
                      [](const auto& m) { return !m.isAlive; }));
}

std::size_t RaidFrameLayout::GetAliveCount() const {
    auto all = GetAllMembers();
    return static_cast<std::size_t>(
        std::count_if(all.begin(), all.end(),
                      [](const auto& m) { return m.isAlive; }));
}

bool RaidFrameLayout::IsRaid() const {
    std::lock_guard lock(mutex_);
    return !groups_.empty();
}

void RaidFrameLayout::SetShowPowerBars(bool show) {
    std::lock_guard lock(mutex_);
    showPowerBars_ = show;
}

bool RaidFrameLayout::GetShowPowerBars() const {
    std::lock_guard lock(mutex_);
    return showPowerBars_;
}

void RaidFrameLayout::SetCompactMode(bool compact) {
    std::lock_guard lock(mutex_);
    compactMode_ = compact;
}

bool RaidFrameLayout::IsCompactMode() const {
    std::lock_guard lock(mutex_);
    return compactMode_;
}

void RaidFrameLayout::Reset() {
    std::lock_guard lock(mutex_);
    groups_.clear();
    showPowerBars_ = true;
    compactMode_ = false;
}

}
