
#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct GroupFrameMemberData {
    ObjectGuid guid;
    std::string name;
    std::uint8_t classId = 0;
    std::uint8_t level = 0;
    std::int32_t healthCurrent = 0;
    std::int32_t healthMax = 0;
    std::int32_t powerCurrent = 0;
    std::int32_t powerMax = 0;
    std::uint8_t powerType = 0;
    bool isOnline = false;
    bool isAlive = true;
    bool isInRange = true;
    std::uint8_t groupRole = 0;
};

class GroupFrameLayout {
 public:
    void SetMembers(std::vector<GroupFrameMemberData> members);
    [[nodiscard]] std::vector<GroupFrameMemberData> GetMembers() const;
    [[nodiscard]] std::optional<GroupFrameMemberData> GetMember(ObjectGuid guid) const;
    [[nodiscard]] std::size_t GetMemberCount() const;

    void UpdateHealth(ObjectGuid guid, std::int32_t current, std::int32_t max);
    void UpdatePower(ObjectGuid guid, std::int32_t current, std::int32_t max,
                     std::uint8_t type);
    void SetMemberOnline(ObjectGuid guid, bool online);
    void SetMemberAlive(ObjectGuid guid, bool alive);

    [[nodiscard]] float GetHealthPercent(ObjectGuid guid) const;
    [[nodiscard]] float GetPowerPercent(ObjectGuid guid) const;
    [[nodiscard]] std::vector<GroupFrameMemberData> GetDeadMembers() const;
    [[nodiscard]] std::vector<GroupFrameMemberData> GetOfflineMembers() const;
    [[nodiscard]] bool IsInGroup() const;

    void Reset();

 private:
    GroupFrameMemberData* FindMember(ObjectGuid guid);
    const GroupFrameMemberData* FindMember(ObjectGuid guid) const;

    mutable std::mutex mutex_;
    std::vector<GroupFrameMemberData> members_;
};

}
