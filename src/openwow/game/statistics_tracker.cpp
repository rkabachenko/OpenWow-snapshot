#include "openwow/game/statistics_tracker.h"

#include <algorithm>

namespace openwow::game {

void StatisticsTracker::SetStatistic(std::uint32_t stat_id,
                                     const std::string& name,
                                     std::uint64_t value,
                                     StatCategory cat) {
  stats_[stat_id] = StatisticEntry{stat_id, name, value, cat, false};
}

std::optional<StatisticEntry> StatisticsTracker::GetStatistic(
    std::uint32_t stat_id) const {
  auto it = stats_.find(stat_id);
  if (it == stats_.end()) return std::nullopt;
  return it->second;
}

std::uint64_t StatisticsTracker::GetValue(std::uint32_t stat_id) const {
  auto it = stats_.find(stat_id);
  if (it == stats_.end()) return 0;
  return it->second.value;
}

void StatisticsTracker::IncrementStatistic(std::uint32_t stat_id,
                                           std::uint64_t amount) {
  auto it = stats_.find(stat_id);
  if (it != stats_.end()) {
    it->second.value += amount;
  }
}

std::vector<StatisticEntry> StatisticsTracker::GetStatisticsByCategory(
    StatCategory cat) const {
  std::vector<StatisticEntry> result;
  for (const auto& [id, entry] : stats_) {
    if (entry.category == cat) {
      result.push_back(entry);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const StatisticEntry& a, const StatisticEntry& b) {
              return a.stat_id < b.stat_id;
            });
  return result;
}

std::vector<StatisticEntry> StatisticsTracker::GetAllStatistics() const {
  std::vector<StatisticEntry> result;
  result.reserve(stats_.size());
  for (const auto& [id, entry] : stats_) {
    result.push_back(entry);
  }
  std::sort(result.begin(), result.end(),
            [](const StatisticEntry& a, const StatisticEntry& b) {
              return a.stat_id < b.stat_id;
            });
  return result;
}

std::string StatisticsTracker::GetCategoryName(StatCategory cat) {
  switch (cat) {
    case StatCategory::Character: return "Character";
    case StatCategory::Combat:    return "Combat";
    case StatCategory::Deaths:    return "Deaths";
    case StatCategory::Quests:    return "Quests";
    case StatCategory::Dungeons:  return "Dungeons";
    case StatCategory::Skills:    return "Skills";
    case StatCategory::Travel:    return "Travel";
    case StatCategory::Social:    return "Social";
    case StatCategory::PvP:       return "PvP";
  }
  return "Unknown";
}

std::uint32_t StatisticsTracker::GetCategoryCount() {
  return kStatCategoryCount;
}

std::uint32_t StatisticsTracker::GetStatCount() const {
  return static_cast<std::uint32_t>(stats_.size());
}

void StatisticsTracker::SetHidden(std::uint32_t stat_id, bool hidden) {
  auto it = stats_.find(stat_id);
  if (it != stats_.end()) {
    it->second.is_hidden = hidden;
  }
}

bool StatisticsTracker::IsHidden(std::uint32_t stat_id) const {
  auto it = stats_.find(stat_id);
  if (it == stats_.end()) return false;
  return it->second.is_hidden;
}

std::vector<StatisticEntry> StatisticsTracker::GetVisibleStatistics() const {
  std::vector<StatisticEntry> result;
  for (const auto& [id, entry] : stats_) {
    if (!entry.is_hidden) {
      result.push_back(entry);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const StatisticEntry& a, const StatisticEntry& b) {
              return a.stat_id < b.stat_id;
            });
  return result;
}

void StatisticsTracker::Reset() { stats_.clear(); }

}
