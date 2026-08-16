
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "openwow/game/lfg_system.h"

namespace openwow::game {

enum class DungeonRewardTier : uint8_t {
    Normal = 0,
    Heroic = 1,
};

struct RewardItemEntry {
    uint32_t itemId = 0;
    uint32_t count  = 0;
};

struct RandomDungeonRewardEntry {
    uint32_t dungeonId      = 0;
    uint32_t moneyReward    = 0;
    uint32_t xpReward       = 0;
    bool     firstCompletion = true;
    std::vector<RewardItemEntry> itemRewards;
    uint32_t emblemReward   = 0;
};

struct ShortageRewardInfo {
    LFGRoleFlag            role       = LFGRoleFlag::Tank;
    uint32_t               extraMoney = 0;
    std::vector<uint32_t>  bonusItems;
};

class RandomDungeonReward {
 public:
    void SetReward(uint32_t dungeonId, const RandomDungeonRewardEntry& entry);
    [[nodiscard]] std::optional<RandomDungeonRewardEntry> GetReward(
        uint32_t dungeonId) const;

    void SetCompletedToday(uint32_t dungeonId);
    [[nodiscard]] bool IsCompletedToday(uint32_t dungeonId) const;
    [[nodiscard]] uint32_t GetCompletedTodayCount() const;
    void ResetDailyCompletions();

    [[nodiscard]] uint32_t GetFirstCompletionBonusMoney(
        uint32_t dungeonId) const;
    [[nodiscard]] uint32_t GetFirstCompletionBonusXP(
        uint32_t dungeonId) const;

    [[nodiscard]] uint32_t GetRepeatMoney(uint32_t dungeonId) const;

    void SetShortageReward(LFGRoleFlag role, uint32_t extraMoney,
                           const std::vector<uint32_t>& bonusItems);
    [[nodiscard]] bool HasShortageReward(LFGRoleFlag role) const;
    [[nodiscard]] uint32_t GetShortageRole() const;
    void SetShortageRole(uint32_t role);

    void Reset();

 private:
    std::unordered_map<uint32_t, RandomDungeonRewardEntry> rewards_;
    std::unordered_set<uint32_t> completed_today_;
    std::unordered_map<uint32_t, ShortageRewardInfo> shortage_rewards_;
    uint32_t shortage_role_ = 0;
    mutable std::mutex mutex_;
};

}
