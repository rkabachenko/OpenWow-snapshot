
#include "openwow/game/group_frame_layout.h"

#include <algorithm>

namespace openwow::game {

GroupFrameMemberData* GroupFrameLayout::FindMember(ObjectGuid guid) {
    auto it = std::find_if(members_.begin(), members_.end(),
                           [guid](const auto& m) { return m.guid == guid; });
    return it != members_.end() ? &(*it) : nullptr;
}

const GroupFrameMemberData* GroupFrameLayout::FindMember(ObjectGuid guid) const {
    auto it = std::find_if(members_.begin(), members_.end(),
                           [guid](const auto& m) { return m.guid == guid; });
    return it != members_.end() ? &(*it) : nullptr;
}

void GroupFrameLayout::SetMembers(std::vector<GroupFrameMemberData> members) {
    std::lock_guard lock(mutex_);
    members_ = std::move(members);
}

std::vector<GroupFrameMemberData> GroupFrameLayout::GetMembers() const {
    std::lock_guard lock(mutex_);
    return members_;
}

std::optional<GroupFrameMemberData> GroupFrameLayout::GetMember(ObjectGuid guid) const {
    std::lock_guard lock(mutex_);
    const auto* m = FindMember(guid);
    if (m) return *m;
    return std::nullopt;
}

std::size_t GroupFrameLayout::GetMemberCount() const {
    std::lock_guard lock(mutex_);
    return members_.size();
}

void GroupFrameLayout::UpdateHealth(ObjectGuid guid, std::int32_t current,
                                    std::int32_t max) {
    std::lock_guard lock(mutex_);
    if (auto* m = FindMember(guid)) {
        m->healthCurrent = current;
        m->healthMax = max;
    }
}

void GroupFrameLayout::UpdatePower(ObjectGuid guid, std::int32_t current,
                                   std::int32_t max, std::uint8_t type) {
    std::lock_guard lock(mutex_);
    if (auto* m = FindMember(guid)) {
        m->powerCurrent = current;
        m->powerMax = max;
        m->powerType = type;
    }
}

void GroupFrameLayout::SetMemberOnline(ObjectGuid guid, bool online) {
    std::lock_guard lock(mutex_);
    if (auto* m = FindMember(guid)) {
        m->isOnline = online;
    }
}

void GroupFrameLayout::SetMemberAlive(ObjectGuid guid, bool alive) {
    std::lock_guard lock(mutex_);
    if (auto* m = FindMember(guid)) {
        m->isAlive = alive;
    }
}

float GroupFrameLayout::GetHealthPercent(ObjectGuid guid) const {
    std::lock_guard lock(mutex_);
    const auto* m = FindMember(guid);
    if (!m || m->healthMax <= 0) return 0.0f;
    return static_cast<float>(m->healthCurrent) /
           static_cast<float>(m->healthMax) * 100.0f;
}

float GroupFrameLayout::GetPowerPercent(ObjectGuid guid) const {
    std::lock_guard lock(mutex_);
    const auto* m = FindMember(guid);
    if (!m || m->powerMax <= 0) return 0.0f;
    return static_cast<float>(m->powerCurrent) /
           static_cast<float>(m->powerMax) * 100.0f;
}

std::vector<GroupFrameMemberData> GroupFrameLayout::GetDeadMembers() const {
    std::lock_guard lock(mutex_);
    std::vector<GroupFrameMemberData> result;
    for (const auto& m : members_) {
        if (!m.isAlive) result.push_back(m);
    }
    return result;
}

std::vector<GroupFrameMemberData> GroupFrameLayout::GetOfflineMembers() const {
    std::lock_guard lock(mutex_);
    std::vector<GroupFrameMemberData> result;
    for (const auto& m : members_) {
        if (!m.isOnline) result.push_back(m);
    }
    return result;
}

bool GroupFrameLayout::IsInGroup() const {
    std::lock_guard lock(mutex_);
    return !members_.empty();
}

void GroupFrameLayout::Reset() {
    std::lock_guard lock(mutex_);
    members_.clear();
}

}
