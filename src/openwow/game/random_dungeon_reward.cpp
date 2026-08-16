
#include "openwow/game/random_dungeon_reward.h"

namespace openwow::game {

void RandomDungeonReward::SetReward(uint32_t dungeonId,
                                    const RandomDungeonRewardEntry& entry) {
    std::lock_guard lock(mutex_);
    rewards_[dungeonId] = entry;
}

std::optional<RandomDungeonRewardEntry> RandomDungeonReward::GetReward(
    uint32_t dungeonId) const {
    std::lock_guard lock(mutex_);
    auto it = rewards_.find(dungeonId);
    if (it != rewards_.end()) return it->second;
    return std::nullopt;
}

void RandomDungeonReward::SetCompletedToday(uint32_t dungeonId) {
    std::lock_guard lock(mutex_);
    completed_today_.insert(dungeonId);
}

bool RandomDungeonReward::IsCompletedToday(uint32_t dungeonId) const {
    std::lock_guard lock(mutex_);
    return completed_today_.count(dungeonId) > 0;
}

uint32_t RandomDungeonReward::GetCompletedTodayCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(completed_today_.size());
}

void RandomDungeonReward::ResetDailyCompletions() {
    std::lock_guard lock(mutex_);
    completed_today_.clear();
}

uint32_t RandomDungeonReward::GetFirstCompletionBonusMoney(
    uint32_t dungeonId) const {
    std::lock_guard lock(mutex_);
    auto it = rewards_.find(dungeonId);
    if (it == rewards_.end()) return 0;
    return it->second.firstCompletion ? it->second.moneyReward : 0;
}

uint32_t RandomDungeonReward::GetFirstCompletionBonusXP(
    uint32_t dungeonId) const {
    std::lock_guard lock(mutex_);
    auto it = rewards_.find(dungeonId);
    if (it == rewards_.end()) return 0;
    return it->second.firstCompletion ? it->second.xpReward : 0;
}

uint32_t RandomDungeonReward::GetRepeatMoney(uint32_t dungeonId) const {
    std::lock_guard lock(mutex_);
    auto it = rewards_.find(dungeonId);
    if (it == rewards_.end()) return 0;

    return it->second.firstCompletion ? 0 : it->second.moneyReward;
}

void RandomDungeonReward::SetShortageReward(
    LFGRoleFlag role, uint32_t extraMoney,
    const std::vector<uint32_t>& bonusItems) {
    std::lock_guard lock(mutex_);
    auto key = static_cast<uint32_t>(role);
    ShortageRewardInfo info;
    info.role = role;
    info.extraMoney = extraMoney;
    info.bonusItems = bonusItems;
    shortage_rewards_[key] = info;
}

bool RandomDungeonReward::HasShortageReward(LFGRoleFlag role) const {
    std::lock_guard lock(mutex_);
    return shortage_rewards_.count(static_cast<uint32_t>(role)) > 0;
}

uint32_t RandomDungeonReward::GetShortageRole() const {
    std::lock_guard lock(mutex_);
    return shortage_role_;
}

void RandomDungeonReward::SetShortageRole(uint32_t role) {
    std::lock_guard lock(mutex_);
    shortage_role_ = role;
}

void RandomDungeonReward::Reset() {
    std::lock_guard lock(mutex_);
    rewards_.clear();
    completed_today_.clear();
    shortage_rewards_.clear();
    shortage_role_ = 0;
}

}
