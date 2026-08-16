
#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class AchProgressStatus : uint8_t {
    NotStarted = 0,
    InProgress = 1,
    Completed  = 2,
};

struct AchProgressEntry {
    uint32_t        achievementId  = 0;
    std::string     name;
    std::string     description;
    uint32_t        points         = 0;
    uint32_t        categoryId     = 0;
    AchProgressStatus status       = AchProgressStatus::NotStarted;
    uint32_t        currentCount   = 0;
    uint32_t        requiredCount  = 0;
    uint64_t        completionTime = 0;
};

class AchProgressDisplay {
public:
    void SetAchievement(const AchProgressEntry& entry);
    [[nodiscard]] std::optional<AchProgressEntry> GetAchievement(uint32_t id) const;

    [[nodiscard]] std::vector<AchProgressEntry> GetByCategory(uint32_t categoryId) const;

    [[nodiscard]] std::vector<AchProgressEntry> GetCompleted() const;
    [[nodiscard]] std::vector<AchProgressEntry> GetInProgress() const;

    [[nodiscard]] uint32_t GetTotalPoints() const;
    [[nodiscard]] size_t GetCompletedCount() const;

    [[nodiscard]] float GetAchievementPercent(uint32_t id) const;

    [[nodiscard]] std::vector<AchProgressEntry> GetRecentCompleted(size_t count) const;

    [[nodiscard]] std::vector<AchProgressEntry> Search(const std::string& query) const;

    [[nodiscard]] uint32_t GetCategoryPoints(uint32_t categoryId) const;

    void Reset();

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, AchProgressEntry> achievements_;
};

}
