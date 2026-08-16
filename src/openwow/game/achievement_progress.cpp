
#include "openwow/game/achievement_progress.h"

#include <algorithm>

namespace openwow::game {

void AchProgressDisplay::SetAchievement(const AchProgressEntry& entry) {
    std::lock_guard lock(mutex_);
    achievements_[entry.achievementId] = entry;
}

std::optional<AchProgressEntry> AchProgressDisplay::GetAchievement(
    uint32_t id) const {
    std::lock_guard lock(mutex_);
    auto it = achievements_.find(id);
    if (it == achievements_.end()) return std::nullopt;
    return it->second;
}

std::vector<AchProgressEntry> AchProgressDisplay::GetByCategory(
    uint32_t categoryId) const {
    std::lock_guard lock(mutex_);
    std::vector<AchProgressEntry> result;
    for (const auto& [id, entry] : achievements_) {
        if (entry.categoryId == categoryId) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<AchProgressEntry> AchProgressDisplay::GetCompleted() const {
    std::lock_guard lock(mutex_);
    std::vector<AchProgressEntry> result;
    for (const auto& [id, entry] : achievements_) {
        if (entry.status == AchProgressStatus::Completed) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<AchProgressEntry> AchProgressDisplay::GetInProgress() const {
    std::lock_guard lock(mutex_);
    std::vector<AchProgressEntry> result;
    for (const auto& [id, entry] : achievements_) {
        if (entry.status == AchProgressStatus::InProgress) {
            result.push_back(entry);
        }
    }
    return result;
}

uint32_t AchProgressDisplay::GetTotalPoints() const {
    std::lock_guard lock(mutex_);
    uint32_t total = 0;
    for (const auto& [id, entry] : achievements_) {
        if (entry.status == AchProgressStatus::Completed) {
            total += entry.points;
        }
    }
    return total;
}

size_t AchProgressDisplay::GetCompletedCount() const {
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (const auto& [id, entry] : achievements_) {
        if (entry.status == AchProgressStatus::Completed) {
            ++count;
        }
    }
    return count;
}

float AchProgressDisplay::GetAchievementPercent(uint32_t id) const {
    std::lock_guard lock(mutex_);
    auto it = achievements_.find(id);
    if (it == achievements_.end()) return 0.0f;
    const auto& e = it->second;
    if (e.requiredCount == 0) return 0.0f;
    float pct = static_cast<float>(e.currentCount) /
                static_cast<float>(e.requiredCount);
    if (pct > 1.0f) pct = 1.0f;
    return pct;
}

std::vector<AchProgressEntry> AchProgressDisplay::GetRecentCompleted(
    size_t count) const {
    std::lock_guard lock(mutex_);
    std::vector<AchProgressEntry> completed;
    for (const auto& [id, entry] : achievements_) {
        if (entry.status == AchProgressStatus::Completed) {
            completed.push_back(entry);
        }
    }

    std::sort(completed.begin(), completed.end(),
              [](const AchProgressEntry& a, const AchProgressEntry& b) {
                  return a.completionTime > b.completionTime;
              });
    if (completed.size() > count) {
        completed.resize(count);
    }
    return completed;
}

std::vector<AchProgressEntry> AchProgressDisplay::Search(
    const std::string& query) const {
    std::lock_guard lock(mutex_);
    std::vector<AchProgressEntry> result;

    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (const auto& [id, entry] : achievements_) {
        std::string lowerName = entry.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lowerName.find(lowerQuery) != std::string::npos) {
            result.push_back(entry);
        }
    }
    return result;
}

uint32_t AchProgressDisplay::GetCategoryPoints(uint32_t categoryId) const {
    std::lock_guard lock(mutex_);
    uint32_t total = 0;
    for (const auto& [id, entry] : achievements_) {
        if (entry.categoryId == categoryId &&
            entry.status == AchProgressStatus::Completed) {
            total += entry.points;
        }
    }
    return total;
}

void AchProgressDisplay::Reset() {
    std::lock_guard lock(mutex_);
    achievements_.clear();
}

}
