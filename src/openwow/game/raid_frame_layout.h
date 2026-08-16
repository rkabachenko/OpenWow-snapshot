
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/group_frame_layout.h"

namespace openwow::game {

struct RaidFrameGroupData {
    std::uint8_t groupIndex = 0;
    std::vector<GroupFrameMemberData> members;
};

enum class RaidFrameSortMode : std::uint8_t {
    ByGroup = 0,
    ByClass = 1,
    ByRole  = 2,
    ByName  = 3,
};

class RaidFrameLayout {
 public:
    void SetGroups(std::vector<RaidFrameGroupData> groups);
    [[nodiscard]] std::vector<RaidFrameGroupData> GetGroups() const;
    [[nodiscard]] std::optional<RaidFrameGroupData> GetGroup(
        std::uint8_t groupIndex) const;

    [[nodiscard]] std::vector<GroupFrameMemberData> GetAllMembers() const;
    [[nodiscard]] std::size_t GetMemberCount() const;

    [[nodiscard]] static constexpr std::size_t GetMaxGroups() { return 8; }
    [[nodiscard]] static constexpr std::size_t GetMaxPerGroup() { return 5; }
    [[nodiscard]] static constexpr std::size_t GetMaxRaidSize() { return 40; }

    [[nodiscard]] std::vector<GroupFrameMemberData> SortBy(
        RaidFrameSortMode mode) const;

    [[nodiscard]] std::vector<GroupFrameMemberData> GetMembersByClass(
        std::uint8_t classId) const;

    [[nodiscard]] std::size_t GetDeadCount() const;
    [[nodiscard]] std::size_t GetAliveCount() const;
    [[nodiscard]] bool IsRaid() const;

    void SetShowPowerBars(bool show);
    [[nodiscard]] bool GetShowPowerBars() const;
    void SetCompactMode(bool compact);
    [[nodiscard]] bool IsCompactMode() const;

    void Reset();

 private:
    mutable std::mutex mutex_;
    std::vector<RaidFrameGroupData> groups_;
    bool showPowerBars_ = true;
    bool compactMode_ = false;
};

}
